#include "opl3_convert.h"
#include "opl3_voice.h"
#include "../opll/opll_to_opl3_wrapper.h"
#include "../vgm/vgm_helpers.h"
#include "../vgm/vgm_header.h"       /* OPL3_CLOCK */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <stdlib.h>  // getenv

typedef enum {
    FREQSEQ_BAB = 0, // B(OFF) -> A -> B(POST)
    FREQSEQ_AB  = 1  // A -> B(POST), for hardware experiment use
} FreqSeqMode;

static FreqSeqMode g_freqseq_mode = FREQSEQ_AB;
static int g_micro_wait_ab   = 0;  // Interval between B(pre)->A and A->B(post)
static int g_micro_wait_port = 0;  // Additional wait between port0 and port1 (added to opts->opl3_keyon_wait)
static int g_debug_freq = 0;

struct OPL3State;
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune,
                  uint8_t *p_outA, uint8_t *p_outB);

static inline int reg_is_An(uint8_t r){ return (r & 0xF0) == 0xA0 && (r & 0x0F) <= 0x08; }
static inline int reg_is_Bn(uint8_t r){ return (r & 0xF0) == 0xB0 && (r & 0x0F) <= 0x08; }
static inline int reg_to_ch(uint8_t r){ return r & 0x0F; }

/** Stage the F-Number LSB for a given channel */
static inline void stage_fnum_lsb(OPL3State *st, int ch9){
    st->staged_fnum_lsb[ch9] = st->reg[0xA0 + ch9];
    st->staged_fnum_valid[ch9] = true;
}

/** Flush the frequency pair for a given channel */
static void flush_freq_pair(OPL3State *st, VGMBuffer *buf, int ch9, uint8_t regB_val, double detune){
    if (!st->staged_fnum_valid[ch9]) {
        st->staged_fnum_lsb[ch9] = st->reg[0xA0 + ch9];
    }
    uint8_t A = st->staged_fnum_lsb[ch9];
    uint8_t B = regB_val;
    // port0
    uint8_t A0p0=A, B0p0=B;
    detune_if_fm(st, ch9, A0p0, B0p0, 0.0, &A0p0, &B0p0);
    opl3_write_reg(st, buf, 0, 0xB0 + ch9, B0p0);
    opl3_write_reg(st, buf, 0, 0xA0 + ch9, A0p0);
    opl3_write_reg(st, buf, 0, 0xB0 + ch9, B0p0);
    // port1
    uint8_t A0p1=A, B0p1=B;
    detune_if_fm(st, ch9+9, A0p1, B0p1, detune, &A0p1, &B0p1);
    opl3_write_reg(st, buf, 1, 0xA0 + ch9, A0p1);
    opl3_write_reg(st, buf, 1, 0xB0 + ch9, B0p1);
    st->staged_fnum_valid[ch9] = false;
}

/**
 * Calculate frequency in Hz for given chip, clock, block, fnum.
 */
double calc_fmchip_frequency(FMChipType chip,
                             double clock,
                             unsigned char block,
                             unsigned short fnum)
{
    switch (chip) {
        case FMCHIP_YM2413: // OPLL
            // f ≈ (clock / 72) / 2^18 * fnum * 2^block
            return (clock / 72.0) / 262144.0 * (double)fnum * ldexp(1.0, block);
        case FMCHIP_YMF262: // OPL3
            // f ≈ (clock / 72) / 2^20 * fnum * 2^block
            return (clock / 72.0) / 1048576.0 * (double)fnum * ldexp(1.0, block);
        case FMCHIP_YM3812: // OPL2
        case FMCHIP_YM3526:
        case FMCHIP_Y8950:
            // f ≈ (clock / 72) / 2^20 * fnum * 2^block (OPL2 family also uses 2^20)
            return (clock / 72.0) / 1048576.0 * (double)fnum * ldexp(1.0, block);
        default:
            return 0.0;
    }
}

/**
 * Calculate frequency in Hz for OPL3 chip, clock, block, fnum.
 */
double calc_opl3_frequency(double clock, unsigned char block, unsigned short fnum) {
    // f ≈ (clock / 72) / 2^20 * fnum * 2^block
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0; // 2^20
    return baseHzPerFnum * (double)fnum * ldexp(1.0, block);
}

/**
 * Calculate OPL3 FNUM and block for a target frequency.
 * Output values are clipped to valid OPL3 ranges.
   OPL3 inverse (we assume existing function but include it here)
   freq -> OPL3 FNUM/BLOCK. prefer minimal absolute freq error; tie-breaker prefer smaller block.
*/
void opl3_calc_fnum_block_from_freq(double freq, double clock,
                                    unsigned char *out_block, unsigned short *out_fnum) {
    if (freq <= 0.0 || clock <= 0.0) { if (out_block) *out_block=0; if (out_fnum) *out_fnum=0; return; }
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0; // 2^20
    unsigned char best_b = 0;
    unsigned short best_f = 0;
    double best_err = DBL_MAX;

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        long   cand       = (long)floor(ideal_fnum + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double err = fabs(calc_freq - freq);
        if (err < best_err) { best_err = err; best_b = (unsigned char)b; best_f = (unsigned short)cand; }
    }
    if (out_block) *out_block = best_b;
    if (out_fnum)  *out_fnum  = best_f;
}


/** Find the best OPL3 FNUM and block for a given frequency. */
void opl3_calc_fnum_block_from_freq_ldexp(double freq, double clock,
                                          unsigned char *out_block, unsigned short *out_fnum, double *out_err)
{
    if (freq <= 0.0 || clock <= 0.0) { if (out_block) *out_block=0; if (out_fnum) *out_fnum=0; if (out_err) *out_err=0.0; return; }
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0;
    unsigned char best_b = 0; unsigned short best_f = 0; double best_err = DBL_MAX;

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        long cand = (long)floor(ideal_fnum + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double err = fabs(calc_freq - freq);
        if (err < best_err) { best_err = err; best_b = (unsigned char)b; best_f = (unsigned short)cand; }
    }
    if (out_block) *out_block = best_b;
    if (out_fnum)  *out_fnum  = best_f;
    if (out_err)   *out_err   = best_err;
}

/** Find the best OPL3 FNUM and block for a given frequency. */
void opl3_find_fnum_block_with_pref_block(double freq, double clock,
                                          unsigned char *best_block, unsigned short *best_fnum,
                                          double *best_err, int pref_block) {
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0;
    double min_cost = DBL_MAX; unsigned char b_best=0; unsigned short f_best=0; double e_best=DBL_MAX;

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        int cand = (int)floor(ideal_fnum + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double freq_err  = fabs(calc_freq - freq);
        double block_pen = (pref_block >= 0) ? fabs((double)b - (double)pref_block) * 0.5 : 0.0;
        double cost = freq_err + block_pen;
        if (cost < min_cost) { min_cost = cost; e_best = freq_err; b_best=(unsigned char)b; f_best=(unsigned short)cand; }
    }
    *best_block = b_best; *best_fnum = f_best; *best_err = e_best;
}

/** Find the best OPL3 FNUM and block for a given frequency, with adjustable block penalty weight. */
void opl3_find_fnum_block_with_weight(double freq, double clock,
                                      unsigned char *best_block, unsigned short *best_fnum,
                                      double *best_err, int pref_block, double mult_weight) {
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0;
    double min_cost=DBL_MAX, e_best=DBL_MAX; unsigned char b_best=0; unsigned short f_best=0;

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        int cand = (int)floor(ideal_fnum + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double freq_err = fabs(calc_freq - freq);
        double weight = 0.25 + (mult_weight * 0.25);
        double block_pen = (pref_block >= 0) ? fabs((double)b - (double)pref_block) * weight : 0.0;
        double cost = freq_err + block_pen;
        if (cost < min_cost) { min_cost = cost; e_best=freq_err; b_best=(unsigned char)b; f_best=(unsigned short)cand; }
    }
    *best_block=b_best; *best_fnum=f_best; *best_err=e_best;
}

/** Find the best OPL3 FNUM and block for a given frequency, with machine learning-based block penalty. */
void opl3_find_fnum_block_with_ml(double freq, double clock,
                                  unsigned char *best_block, unsigned short *best_fnum,
                                  double *best_err,int pref_block,
                                  double mult0,double mult1) {
    const double baseHzPerFnum = (clock / 72.0) / 1048576.0;
    double min_cost=DBL_MAX, e_best=DBL_MAX; unsigned char b_best=0; unsigned short f_best=0;
    double ml_weight = 1.0 + 0.1 * ((mult0 + mult1) * 0.5);

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        int cand = (int)floor(ideal_fnum + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double freq_err = fabs(calc_freq - freq);
        double block_pen = (pref_block >= 0) ? fabs((double)b - (double)pref_block) * ml_weight * 0.1 : 0.0;
        double cost = freq_err + block_pen;
        if (cost < min_cost) { min_cost = cost; e_best=freq_err; b_best=(unsigned char)b; f_best=(unsigned short)cand; }
    }
    *best_block=b_best; *best_fnum=f_best; *best_err=e_best;
}

/*
 * freq : Target frequency (Hz) - pass the return value of calc_fmchip_frequency(FMCHIP_YM2413, ...)
 * clock: Destination chip clock (Hz) - pass the OPL3 clock (get_fm_target_clock())
 * pref_block : OPLL block (-1 to ignore)
 * mult0, mult1: Carrier/modulator MULT (0..15). For 2op, set mult1 = 0.
 * best_err returns the finally selected (frequency error in cents) (note: not Hz)
 * Usage: This function prioritizes "frequency matching," while allowing slight block bias by ML.
 */
static inline double hz_to_cents(double a, double b) {
    if (a <= 0.0 || b <= 0.0) return DBL_MAX;
    return 1200.0 * log2(a / b);
}

void opl3_find_fnum_block_with_ml_cents(double freq, double clock,
                                        unsigned char *best_block, unsigned short *best_fnum,
                                        double *best_err,
                                        int pref_block,
                                        double mult0, double mult1)
{
    const double PENALTY_CENTS_PER_BLOCK = 50.0;
    const double ML_ALPHA = 0.08;

    const double baseHzPerFnum = (clock / 72.0) / 1048576.0;
    double min_cost = DBL_MAX, chosen_cents_err = DBL_MAX;
    unsigned char b_best=0; unsigned short f_best=0;

    double ml_mean = (mult0 + mult1) * 0.5;
    double ml_factor = 1.0 + ML_ALPHA * ml_mean;

    for (int b=0; b<8; ++b) {
        double ideal_fnum = freq / (baseHzPerFnum * ldexp(1.0, b));
        int cand = (int)floor(ideal_fnum + 0.5);
        if (cand < 0) cand = 0;
        if (cand > 1023) cand = 1023;

        double calc_freq = baseHzPerFnum * (double)cand * ldexp(1.0, b);
        double cents_err = fabs(hz_to_cents(calc_freq, freq));

        double block_penalty = (pref_block >= 0)
            ? fabs((double)b - (double)pref_block) * PENALTY_CENTS_PER_BLOCK * ml_factor
            : 0.0;

        double cost = cents_err + block_penalty;
        if (cost < min_cost) {
            min_cost = cost;
            chosen_cents_err = cents_err;
            b_best=(unsigned char)b; f_best=(unsigned short)cand;
        }
    }
    *best_block = b_best;
    *best_fnum  = f_best;
    *best_err   = chosen_cents_err;
}


/**
 * Attenuate TL (Total Level) using volume ratio.
 */
static uint8_t apply_tl_with_ratio(uint8_t orig_val, double v_ratio) {
    if (v_ratio == 1.0) return orig_val;
    // TL is lower 6 bits
    uint8_t tl = orig_val & 0x3F;
    double dB = 0.0;
    if (v_ratio < 1.0) {
        dB = -20.0 * log10(v_ratio); // 20*log10(ratio) [dB]
    }
    // 1 step = 0.75dB
    int add = (int)(dB / 0.75 + 0.5); // to round off
    int new_tl = tl + add;
    if (new_tl > 63) new_tl = 63;
    return (orig_val & 0xC0) | (new_tl & 0x3F);
}

/**
 * Write a value to the OPL3 register mirror and update internal state flags.
 * Always writes to the register mirror (reg[]). Also writes to VGMBuffer.
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value) {
    int reg_addr = reg + (port ? 0x100 : 0x000);
    p_state->reg_stamp[reg_addr] = p_state->reg[reg_addr];
    p_state->reg[reg_addr] = value;
    // Update rhythm mode or OPL3 mode initialized flags if needed
    if (reg_addr == 0x105) {
        p_state->opl3_mode_initialized = (value & 0x01) != 0;
    }
    if (reg_addr == 0x0BD) {
        p_state->rhythm_mode = (value & 0x20) != 0;
    }
    // Write to VGM stream
    forward_write(p_music_data, port, reg, value);
}

/**
 * Detune helper for FM channels (used for frequency detune effects).
 * Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode.
 */
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB) {
    // Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode
    if ((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode)) {
        *p_outA = regA;
        *p_outB = regB;
        return;
    }
    uint16_t fnum = ((regB & 3) << 8) | regA;
    double delta = fnum * (detune / 100.0);
    int fnum_detuned = (int)(fnum + delta + 0.5);

    if (fnum_detuned < 0) fnum_detuned = 0;
    if (fnum_detuned > 1023) fnum_detuned = 1023;

    *p_outA = (uint8_t)(fnum_detuned & 0xFF);
    *p_outB = (regB & 0xFC) | ((fnum_detuned >> 8) & 3);
}

/** Calculate frequency in Hz for OPL3 chip, clock, block, fnum. */
static inline double opl3_calc_hz_dbg(uint8_t block, uint16_t fnum) {
    // General approximation for OPL3 (YM3812/262 family):
    // f ≈ (clock / 72) * (fnum * 2^block) / 2^20
    // Example: when clock=14318180, the constant is ≈ 0.1897 Hz/LSB@block=0
    const double base = (OPL3_CLOCK / 72.0) / 1048576.0; // 2^20
    return base * (double)fnum * ldexp(1.0, block);
}

/**
 * Merge two FNUM values (LSB from A, MSB from B).
 */
static inline uint16_t merge_fnum_dbg(uint8_t A_lsb, uint8_t B_msb) { return (uint16_t)(((B_msb & 0x03) << 8) | A_lsb); }

/** Create a KeyOff message for a given OPL3 register value. */
uint8_t opl3_make_keyoff(uint8_t val) {
    return (val & ~(1 << 6)); //  Clear bit 6 (KeyOn)
}



int opl3_write_block_fnum_key (VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t    ch,
    uint8_t    block,
    uint16_t   fnum,
    int       keyon,
    const CommandOptions *opts) {

    int bytes_written = 0;
    uint8_t value_a = fnum & 0xFF;
    uint8_t value_b = ((fnum >> 8) & 0x03) | (block << 2) | (keyon ? 0x20 : 0x00);

    // Write FNUM LSB (A0..A8)
    bytes_written += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xA0 + ch, value_a, opts, 0);
    // Write FNUM MSB (B0..B8)
    bytes_written += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, value_b, opts, 0);
    return bytes_written;
}

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * Returns the number of additional bytes written (beyond the initial 3-byte write).
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val, const CommandOptions *opts,
    uint16_t next_wait_samples
    // double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1
) {
    int addtional_bytes = 0;
    uint32_t keyon_wait_inserted = 0;  // Track waits inserted by this function

    if (p_state->pair_an_bn_enabled) {
        uint8_t detunedA, detunedB;        
        if (reg_is_An(reg)) {
            int ch = reg_to_ch(reg);
            p_state->reg[(0x000 + reg)] = val;
            stage_fnum_lsb(p_state, ch);
            return 0;
        }
        if (reg_is_Bn(reg)) {
            int ch = reg_to_ch(reg);
            flush_freq_pair(p_state, p_music_data, ch, val, opts->detune);
            return 6;
        }
    }

    if (reg == 0x01 || reg == 0x02 || reg == 0x03 || reg == 0x04) {
        // Write only to port 0
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg == 0x05) {
        // Handle mode register (only port 1)
        // Always OPL3 mode
        opl3_write_reg(p_state, p_music_data, 1, 0x05, val & 0x1);
    } else if (reg >= 0x40 && reg <= 0x55) {
        uint8_t val0 = apply_tl_with_ratio(val, opts->v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(val, opts->v_ratio1);
        opl3_write_reg(p_state, p_music_data, 0, reg, val0);
        opl3_write_reg(p_state, p_music_data, 1, reg, val1); addtional_bytes += 3;
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        // Only write port0 for A0..A8
        int ch = reg - 0xA0;

        if ((p_state->reg[0xB0 + ch]) & 0x20) {
            opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, val);
        } else {
            // Only update the register buffer (No dump to vgm)
            p_state->reg_stamp[reg] = p_state->reg[reg];
            p_state->reg[reg] = val;
        }
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        // Only perform voice registration on KeyOn event (when writing to FREQ_MSB and KEYON bit transitions 0->1)
        // Get the previous and new KEYON bit values
        uint8_t prev_val = p_state->reg_stamp[reg];
        uint8_t keyon_prev = prev_val & 0x20;
        uint8_t keyon_new  = val & 0x20;
        // KeyOn occurs: extract and register voice parameters for this channel
        if (!keyon_prev && keyon_new) {
            OPL3VoiceParam vp;
            // Always zero-initialize the whole structure before extracting parameters
            memset(&vp, 0, sizeof(OPL3VoiceParam));
            // Extract voice parameters from the OPL3 state
            extract_voice_param(p_state, &vp);
            // Set additional fields as needed before DB registration
            vp.source_fmchip = p_state->source_fmchip;
            // Register or find voice in the database
            opl3_voice_db_find_or_add(&p_state->voice_db, &vp);
        }
        
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        // forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        int ch = reg - 0xB0;
        uint8_t A_lsb = p_state->reg[0xA0 + ch];
        fprintf(stderr, "[SEQ0] ch=%d mode=%s A=%02X B=%02X (rhythm=%d) ",
                ch, g_freqseq_mode==FREQSEQ_BAB?"BAB":"AB", A_lsb, val, p_state->rhythm_mode);
        if (g_freqseq_mode == FREQSEQ_BAB) {
            fprintf(stderr, "port0: B(%02X)->A(%02X)->B(%02X)\n",val, A_lsb, val);
            opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
            opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, A_lsb);
            opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
             // Extra 3 bytes
            addtional_bytes += 3;
        } else {
            fprintf(stderr, "port0: A(%02X)->B(%02X)\n", A_lsb, val);
            opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, A_lsb);
            opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
            // Extra 2 bytes
            addtional_bytes += 2;
        }
       
        if ( opts->opl3_keyon_wait > 0) {
            vgm_wait_samples(p_music_data, p_vstat, opts->opl3_keyon_wait);
            keyon_wait_inserted += opts->opl3_keyon_wait;
        }

        uint8_t detunedA, detunedB;
        detune_if_fm(p_state, ch, A_lsb, val, opts->detune, &detunedA, &detunedB);
        fprintf(stderr, "[SEQ1] ch=%d mode=%s A=%02X B=%02X (rhythm=%d) ",
                ch, g_freqseq_mode==FREQSEQ_BAB?"BAB":"AB", detunedA, detunedB, p_state->rhythm_mode);
        fprintf(stderr, "port1: A(%02X)->B(%02X)\n", detunedA, detunedB);
        opl3_write_reg(p_state, p_music_data, 1, 0xA0 + ch, detunedA); addtional_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode))) {
            opl3_write_reg(p_state, p_music_data, 1, reg, detunedB); addtional_bytes += 3;
        }
        if ( opts->opl3_keyon_wait > 0) {
            vgm_wait_samples(p_music_data, p_vstat, opts->opl3_keyon_wait);
            keyon_wait_inserted += opts->opl3_keyon_wait;
        }
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        // Stereo panning implementation based on channel number
        // Even channels: port0->right, port1->left
        // Odd channels: port0->left, port1->right
        // This creates alternating stereo placement for a stereo effect
        uint8_t port0_panning, port1_panning;
        if (opts->ch_panning) {
            // Apply stereo panning
            if (ch % 2 == 0) {
                // Even channel: port0 gets right channel, port1 gets left channel
                port0_panning = 0x50;  // Right channel (bit 4 and bit 6)
                port1_panning = 0xA0;  // Left channel (bit 5 and bit 7)
            } else {
                // Odd channel: port0 gets left channel, port1 gets right channel
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
            }
        } else {
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
        }
        opl3_write_reg(p_state, p_music_data, 0, 0xC0 + ch, (0xF & val) | port0_panning);
        opl3_write_reg(p_state, p_music_data, 1, 0xC0 + ch, (0xF & val) | port1_panning); addtional_bytes += 3;
    } else if (reg == 0xBD) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg >= 0xE0 && reg <= 0xF5) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    } else {
        // Write to both ports
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    }
    
    // Tail compensation: add any keyon_wait we inserted to the gate compensation debt
    // and compensate from next_wait_samples if available
    uint32_t *p_debt = opll_get_gate_comp_debt_ptr();
    if (keyon_wait_inserted > 0) {
        *p_debt += keyon_wait_inserted;
    }
    
    // Compensate next_wait_samples from accumulated debt
    if (next_wait_samples > 0 && *p_debt > 0) {
        uint16_t compensation = (next_wait_samples >= *p_debt) ? (uint16_t)(*p_debt) : next_wait_samples;
        uint16_t adjusted = next_wait_samples - compensation;
        *p_debt -= compensation;
        
        if (opts && opts->debug.verbose) {
            fprintf(stderr, "[GATE COMP] next_wait=%u -> adjusted=%u, debt_left=%u\n",
                    next_wait_samples, adjusted, *p_debt);
        }
        
        if (adjusted > 0) {
            vgm_wait_samples(p_music_data, p_vstat, adjusted);
        }
    } else if (next_wait_samples > 0) {
        // No debt to compensate, emit the wait as-is
        vgm_wait_samples(p_music_data, p_vstat, next_wait_samples);
    }
    
    return addtional_bytes;
}

/**
 * OPL3 initialization sequence for both ports.
 * Sets FM chip type in OPL3State and initializes register mirror.
 */
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip) {
    if (!p_state) return;
    memset(p_state->reg, 0, sizeof(p_state->reg));
    memset(p_state->reg_stamp, 0, sizeof(p_state->reg_stamp));
    p_state->rhythm_mode = false;
    p_state->opl3_mode_initialized = false;
    p_state->source_fmchip = source_fmchip;

    const char *seq = getenv("ESEOPL3_FREQSEQ");
    if (seq && (seq[0]=='a' || seq[0]=='A')) g_freqseq_mode = FREQSEQ_AB;
    fprintf(stderr, "[FREQSEQ] selected=%s (ESEOPL3_FREQSEQ=%s)\n",
            g_freqseq_mode==FREQSEQ_BAB ? "BAB" : "AB",
            seq ? seq : "(unset)");

    // Initialize OPL3VoiceDB
    opl3_voice_db_init(&p_state->voice_db);

    // OPL3 global registers (Port 1 only)
    opl3_write_reg(p_state, p_music_data, 1, 0x05, 0x01);  // OPL3 enable
    opl3_write_reg(p_state, p_music_data, 1, 0x04, 0x00);  // Waveform select

    // Port 0 general init
    opl3_write_reg(p_state, p_music_data, 0, 0x01, 0x00);  // LSI TEST
    opl3_write_reg(p_state, p_music_data, 0, 0x08, 0x00);  // NTS

    // Port 1 general init
    opl3_write_reg(p_state, p_music_data, 1, 0x01, 0x00);

    // Channel-level control
    for (uint8_t i = 0; i < 9; ++i) {
        // Port 0: channels 0-8
        if (stereo_mode) {
            uint8_t value0 = (i % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, value0);
        } else {
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, 0x50);
        }
        // Port 1: channels 9-17 (registers 0xC0-0xC8)
        if (stereo_mode) {
            uint8_t value1 = ((i + 9) % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, value1);
        } else {
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, 0xA0);
        }
    }

    // Waveform select registers (port 0 and 1)
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs) / sizeof(ext_regs[0]); ++i) {
        opl3_write_reg(p_state, p_music_data, 0, ext_regs[i], 0x00);
        opl3_write_reg(p_state, p_music_data, 1, ext_regs[i], 0x00);
    }
    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        opl3_write_reg(p_state, p_music_data, 1, reg, 0x00);
    }
}

/* 
 * YM2413 volume nibble -> OPL3 TL add value mapping
 *   0..15 (0=maximum, 15=minimum)
 *   MODE 1: TL_add = vol * 4        (simple approximation, about 4dB per step)
 *   MODE 2: TL_add = vol * 3        (slightly finer, about 3dB per step)
 *   MODE 3: Manual table (example: for measured/reference substitution)
 */
#ifndef Y2413_VOL_MAP_MODE
#define Y2413_VOL_MAP_MODE 1
#endif

#if Y2413_VOL_MAP_MODE == 1
static inline uint8_t ym2413_vol_to_tl_add(uint8_t vol) {
    return (uint8_t)((vol & 0x0F) * 4);
}
#elif Y2413_VOL_MAP_MODE == 2
static inline uint8_t ym2413_vol_to_tl_add(uint8_t vol) {
    return (uint8_t)((vol & 0x0F) * 3);
}
#else
/* Custom table: replace with measured values if needed */
static const uint8_t kYM2413VolToTLAdd[16] = {
    0, 3, 6, 9, 12, 15, 18, 21,
    24,27,30,33,36,39,42,45
};
static inline uint8_t ym2413_vol_to_tl_add(uint8_t vol) {
    return kYM2413VolToTLAdd[vol & 0x0F];
}
#endif

#ifndef YM2413_VOL_MAP_STEP
#define YM2413_VOL_MAP_STEP 2
#endif
/*
 * make_carrier_40_from_vol
 * Reflect YM2413 $3n register (reg3n) lower 4 bits (VOL) into OPL3 Carrier TL,
 * and return the value to write to 0x40 + slotCar (KSL/TL).
 *  - vp->op[1].ksl: upper 2 bits (KSL)
 *  - vp->op[1].tl : base TL
 *  - VOL nibble   : added to TL (0=add 0, 15=maximum add)
 * Clip: if over 63, set to 63.
 */
/* Fully replaces old make_carrier_40_from_vol */
uint8_t make_carrier_40_from_vol(const OPL3VoiceParam *vp, uint8_t reg3n)
{
    /* YM2413 volume nibble: 0=loudest .. 15=softest */
    uint8_t vol = reg3n & 0x0F;          // 0..15
    uint8_t base_tl = (uint8_t)(vp->op[1].tl & 0x3F);

    /* STEP: 2 => slightly less than 3dB per step (0.75dB * 2 ≈1.5dB). Make this configurable if needed */
    const uint8_t STEP = YM2413_VOL_MAP_STEP;

    uint16_t tl = (uint16_t)base_tl + (uint16_t)vol * STEP;
    if (tl > 63) tl = 63;

    /* KSL bits preserved */
    uint8_t ksl_bits = (vp->op[1].ksl & 0x03) << 6;

    /* For debugging (only first few times) */
    static int dbg_cnt = 0;
    if (dbg_cnt < 8) {
        fprintf(stderr,
            "[VOLMAP] reg3n=%02X vol=%u baseTL=%u => newTL=%u (STEP=%u)\n",
            reg3n, vol, base_tl, (unsigned)tl, STEP);
        dbg_cnt++;
    }

    return (uint8_t)(ksl_bits | (uint8_t)tl);
}