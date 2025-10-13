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
#include <stdarg.h>


typedef enum {
    FREQSEQ_BAB = 0, // B(OFF) -> A -> B(POST)
    FREQSEQ_AB  = 1  // A -> B(POST), for hardware experiment use
} FreqSeqMode;

static FreqSeqMode g_freqseq_mode = FREQSEQ_AB;
static int g_micro_wait_ab   = 0;  // Interval between B(pre)->A and A->B(post)
static void opl3_debug_log(const CommandOptions *opts, const char *fmt, ...) {
    if (!opts || !opts->debug.verbose) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static inline bool opl3_should_account_port1(const VGMStatus *p_vstatus) {
        return (p_vstatus && p_vstatus->is_adding_port1_bytes);
}

struct OPL3State;
void detune_if_fm(VGMContext *p_vpmctx, int ch, uint8_t regA, uint8_t regB, double detune,
                  uint8_t *p_outA, uint8_t *p_outB, const CommandOptions *p_opts);

static inline int reg_is_An(uint8_t r){ return (r & 0xF0) == 0xA0 && (r & 0x0F) <= 0x08; }
static inline int reg_is_Bn(uint8_t r){ return (r & 0xF0) == 0xB0 && (r & 0x0F) <= 0x08; }
static inline int reg_to_ch(uint8_t r){ return r & 0x0F; }

/** Stage the F-Number LSB for a given channel */
static inline void stage_fnum_lsb(OPL3State *st, int ch9){
    st->staged_fnum_lsb[ch9] = st->reg[0xA0 + ch9];
    st->staged_fnum_valid[ch9] = true;
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
void opl3_write_reg(VGMContext *p_vpmctx, int port, uint8_t reg, uint8_t value) {
    int reg_addr = reg + (port ? 0x100 : 0x000);
    p_vpmctx->opl3_state.reg_stamp[reg_addr] = p_vpmctx->opl3_state.reg[reg_addr];
    p_vpmctx->opl3_state.reg[reg_addr] = value;
    // Write to VGM stream
    forward_write(p_vpmctx, port, reg, value);
}

// fnum_min より下はscale=1.0。fnum_maxより上はscale=min_scale。
double get_detune_scale_liner(uint16_t fnum) {
    const int fnum_min = 200, fnum_max = 640;
    const double min_scale = 0.05;
    if (fnum < fnum_min) return 1.0;
    if (fnum > fnum_max) return min_scale;
    double t = (double)(fnum - fnum_min) / (fnum_max - fnum_min);
    return 1.0 - (1.0 - min_scale) * t;
}

double get_detune_scale_step(uint16_t fnum) {
    if (fnum < 400) return 1.0;   // 100%
    if (fnum < 600) return 0.8;   // 80%
    if (fnum < 800) return 0.5;   // 50%
    return 0.2;                   // 20%（高音ほぼ無効）
}

double get_detune_scale_exp(uint16_t fnum) {
    const int fnum_min = 200, fnum_max = 895;
    const double min_scale = 0.01;
    if (fnum < fnum_min) return 1.0;
    if (fnum > fnum_max) return min_scale;
    double t = (double)(fnum - fnum_min) / (fnum_max - fnum_min);
    return pow(min_scale, t); // 1.0→0.05へ指数的に減る
}

double get_detune_scale_from_block(int block) {
    // 0:最低音、7:最高音
    static const double scale_table[8] = {(double)(0.0), (double)(1.0),(double)(1.0), (double)(1.0), (double)(0.0), (double)(0.0), (double)(0.0), (double)(0.0)};
    return scale_table[block < 0 ? 0 : (block > 7 ? 7 : block)];
}

/**
 * Detune helper for FM channels (used for frequency detune effects).
 * Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode.
 * block=0または高block・高fnumでは自動的にdetuneを弱める/無効化。
 * detune絶対値は±4で制限。
 */
double get_detune_scale(int block, uint16_t fnum) {
    // よくある範囲指定例
    // block: 0=最低音, 7=最高音
    static const double block_scale[8] = {0.0, 1.0, 0.7, 0.5, 0.3, 0.15, 0.05, 0.0};
    double scale = block_scale[block < 0 ? 0 : (block > 7 ? 7 : block)];

    // blockで0なら即OFF
    if (scale == 0.0) return 0.0;

    // fnumによる追加減衰（高fnumほど弱める: 800以上なら半減、900以上なら1/5）
    if (fnum > 900) scale *= 0.2;
    else if (fnum > 800) scale *= 0.5;

    return scale;
}

void detune_if_fm(VGMContext *p_vpmctx, int ch, uint8_t regA, uint8_t regB, double detune_percent, uint8_t *p_outA, uint8_t *p_outB,const CommandOptions *p_opts) {
    // Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode
    if ((ch >= 6 && ch <= 8 && p_vpmctx->opl3_state.rhythm_mode) || (ch >= 15 && ch <= 17 && p_vpmctx->opl3_state.rhythm_mode)) {
        *p_outA = regA;
        *p_outB = regB;
        return;
    }
    uint16_t fnum = ((regB & 3) << 8) | regA;
    uint8_t block = (regB >> 1) & 0x07;

    // detuneスケール決定
    double scale = get_detune_scale(block, fnum);

    // detune計算
    double delta = fnum * (detune_percent / 100.0) * scale;

    // detune絶対値の上限
    double limit = (p_opts && p_opts->detune_limit > 0.0) ? p_opts->detune_limit : 4.0;
    if (delta > limit) delta = limit;
    if (delta < -limit) delta = -limit;

    int fnum_detuned = (int)(fnum + delta + 0.5);

    // FNUM範囲でclamp
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



int opl3_write_block_fnum_key (
    VGMContext *p_vpmctx,
    uint8_t    ch,
    uint8_t    block,
    uint16_t   fnum,
    int       keyon,
    const CommandOptions *opts) {

    int bytes_written = 0;
    uint8_t value_a = fnum & 0xFF;
    uint8_t value_b = ((fnum >> 8) & 0x03) | (block << 2) | (keyon ? 0x20 : 0x00);

    // Write FNUM LSB (A0..A8)
    bytes_written += duplicate_write_opl3(p_vpmctx, 0xA0 + ch, value_a, opts);
    // Write FNUM MSB (B0..B8)
    bytes_written += duplicate_write_opl3(p_vpmctx, 0xB0 + ch, value_b, opts);
    return bytes_written;
}

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * Returns the number of additional bytes written (beyond the initial 3-byte write).
 */
int duplicate_write_opl3(
    VGMContext *p_vpmctx,
    uint8_t reg, uint8_t val, const CommandOptions *p_opts
    // double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1
) {
    int addtional_bytes = 0;
    if (reg == 0x01 || reg == 0x02 || reg == 0x03 || reg == 0x04) {
        // Write only to port 0
        opl3_write_reg(p_vpmctx, 0, reg, val);
    } else if (reg == 0x05) {
        // Handle mode register (only port 1)
        // Always OPL3 mode
        p_vpmctx->opl3_state.opl3_mode_initialized = (val & 0x01) != 0;
        opl3_write_reg(p_vpmctx, 1, 0x05, val & 0x1);
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

        // Update port 1 reg
        int port_1_reg_addr = reg + 0x100;
        p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
        p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
    } else if (reg >= 0x40 && reg <= 0x55) {
        int ch = reg - 0x40;

        uint8_t val0 = apply_tl_with_ratio(val, p_opts->v_ratio0);
        opl3_write_reg(p_vpmctx, 0, reg, val0);
        if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
            uint8_t val1 = apply_tl_with_ratio(val, p_opts->v_ratio1);
            opl3_write_reg(p_vpmctx, 1, reg, val1);
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
            opl3_debug_log(p_opts, "[OPL3] Write reg=%02X val=%02X ch=%d (port0/port1)\n", reg, val, ch);
        }
    } else if (reg >= 0x60 && reg <= 0x75) {
        int ch = reg - 0x60;

        opl3_write_reg(p_vpmctx, 0, reg, val);
        if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
            opl3_write_reg(p_vpmctx, 1, reg, val); 
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
        }
        opl3_debug_log(p_opts, "[OPL3] Write reg=%02X val=%02X ch=%d (60h block)\n", reg, val, ch);
    } else if (reg >= 0x80 && reg <= 0x95) {
        int ch = reg - 0x80;

        opl3_write_reg(p_vpmctx, 0, reg, val);
        if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
            opl3_write_reg(p_vpmctx, 1, reg, val); 
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
        }
        opl3_debug_log(p_opts, "[OPL3] Write reg=%02X val=%02X ch=%d (80h block)\n", reg, val, ch);
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        // Only write port0 for A0..A8
        int ch = reg - 0xA0;

        // KeyOn判定
        uint8_t keyon = (p_vpmctx->opl3_state.reg[0xB0 + ch]) & 0x20;

        if (keyon) {
            opl3_write_reg(p_vpmctx, 0, 0xA0 + ch, val);
            opl3_write_reg(p_vpmctx, 1, 0xA0 + ch, val); 
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
            opl3_debug_log(p_opts, "[SEQ0] ch=%d %s A=%02X (rhythm=%d) port0: A(%02X)\n",
                ch, (keyon) ? "KeyOn" : "KeyOff", val, p_vpmctx->opl3_state.rhythm_mode, val);
        } else {
            // Only update the register buffer (No dump to vgm)
            p_vpmctx->opl3_state.reg_stamp[reg] = p_vpmctx->opl3_state.reg[reg];
            p_vpmctx->opl3_state.reg[reg] = val;
        }
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        // forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        int ch = reg - 0xB0;
        uint8_t A_lsb = p_vpmctx->opl3_state.reg[0xA0 + ch];

        // KeyOn判定
        uint8_t prev_val = p_vpmctx->opl3_state.reg_stamp[reg];
        uint8_t keyon_prev = prev_val & 0x20;
        uint8_t keyon_new  = val & 0x20;

        if (!keyon_prev && keyon_new) {
            // KeyOff -> KeyOn（posedge）：A>B
            opl3_debug_log(p_opts, "[SEQ0] ch=%d KeyOff -> KeyOn A=%02X B=%02X (rhythm=%d) port0: A(%02X)->B(%02X)\n",
                ch, A_lsb, val, p_vpmctx->opl3_state.rhythm_mode, A_lsb, val);
            opl3_write_reg(p_vpmctx, 0, 0xA0 + ch, A_lsb);
            opl3_write_reg(p_vpmctx, 0, 0xB0 + ch, val);
        } else if (keyon_prev && !keyon_new) {
            // KeyOn -> KeyOff（negedge）：B>A
            opl3_debug_log(p_opts, "[SEQ0] ch=%d KeyOn -> KeyOff A=%02X B=%02X (rhythm=%d) port0: B(%02X)->A(%02X)\n",
                ch, A_lsb, val, p_vpmctx->opl3_state.rhythm_mode, val, A_lsb);
            opl3_write_reg(p_vpmctx, 0, 0xB0 + ch, val);
            opl3_write_reg(p_vpmctx, 0, 0xA0 + ch, A_lsb);
        } else {
            opl3_debug_log(p_opts, "[SEQ0] ch=%d %s mode=%s A=%02X B=%02X (rhythm=%d) ",
                ch, (keyon_prev) ? "KeyOn" : "KeyOff", 
                g_freqseq_mode == FREQSEQ_BAB ? "BAB" : "AB", A_lsb, val, p_vpmctx->opl3_state.rhythm_mode);
            if (g_freqseq_mode == FREQSEQ_BAB) {
                opl3_debug_log(p_opts, "port0: B(%02X)->A(%02X)->B(%02X)\n", val, A_lsb, val);
                opl3_write_reg(p_vpmctx, 0, 0xB0 + ch, val);
                opl3_write_reg(p_vpmctx, 0, 0xA0 + ch, A_lsb);
                opl3_write_reg(p_vpmctx, 0, 0xB0 + ch, val);
            } else {
                opl3_debug_log(p_opts, "port0: A(%02X)->B(%02X)\n", A_lsb, val);
                opl3_write_reg(p_vpmctx, 0, 0xA0 + ch, A_lsb);
                opl3_write_reg(p_vpmctx, 0, 0xB0 + ch, val);
            }
        }
       
        if ( p_opts->opl3_keyon_wait > 0)
            vgm_wait_samples(p_vpmctx, p_opts->opl3_keyon_wait);

        // Detune 計算
        uint8_t detunedA, detunedB;
        detune_if_fm(p_vpmctx, ch, A_lsb, val, p_opts->detune, &detunedA, &detunedB,p_opts);
        if (!keyon_prev && keyon_new) {
            // KeyOff -> KeyOn（posedge）：A>B
            opl3_debug_log(p_opts, "[SEQ1] ch=%d KeyOff -> KeyOn A=%02X B=%02X (rhythm=%d) port1: A(%02X)->B(%02X)\n",
                ch, detunedA, detunedB, p_vpmctx->opl3_state.rhythm_mode, detunedA, detunedB);
            opl3_write_reg(p_vpmctx, 1, 0xA0 + ch, detunedA);
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            opl3_write_reg(p_vpmctx, 1, 0xB0 + ch, detunedB);
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            // Update port 1 reg
            int port_1_reg_addr = 0xA0 + ch + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;

            port_1_reg_addr = 0xB0 + ch + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
        } else if (keyon_prev && !keyon_new) {
            // KeyOn -> KeyOff（negedge）：B>A
            opl3_debug_log(p_opts, "[SEQ1] ch=%d KeyOn -> KeyOff A=%02X B=%02X (rhythm=%d) port1: B(%02X)->A(%02X)\n",
                ch, detunedA, detunedB, p_vpmctx->opl3_state.rhythm_mode, detunedB, detunedA);
            opl3_write_reg(p_vpmctx, 1, 0xB0 + ch, detunedB);
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            opl3_write_reg(p_vpmctx, 1, 0xA0 + ch, detunedA);
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            // Update port 1 reg
            int port_1_reg_addr = 0xA0 + ch + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;

            port_1_reg_addr = 0xB0 + ch + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
        } else {
            // Supposing OPL3 Extend mode
            if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
                opl3_debug_log(p_opts, "[SEQ1] ch=%d %s mode=%s A=%02X B=%02X (rhythm=%d) ",
                    ch, (keyon_prev) ? "KeyOn" : "KeyOff", 
                    g_freqseq_mode == FREQSEQ_BAB ? "BAB" : "AB", detunedA, detunedB, p_vpmctx->opl3_state.rhythm_mode);
                if (g_freqseq_mode == FREQSEQ_BAB) {
                    opl3_debug_log(p_opts, "port1: B(%02X)->A(%02X)->B(%02X)\n", detunedB, detunedA, detunedB);
                    opl3_write_reg(p_vpmctx, 1, 0xB0 + ch, detunedB);
                    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
                    opl3_write_reg(p_vpmctx, 1, 0xA0 + ch, detunedA);
                    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
                    opl3_write_reg(p_vpmctx, 1, 0xB0 + ch, detunedB);
                    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

                    // Update port 1 reg
                    int port_1_reg_addr = 0xA0 + ch + 0x100;
                    p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
                    p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;

                    port_1_reg_addr = 0xB0 + ch + 0x100;
                    p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
                    p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
                } else {
                    opl3_debug_log(p_opts, "port1: A(%02X)->B(%02X)\n", detunedA, detunedB);
                    opl3_write_reg(p_vpmctx, 1, 0xA0 + ch, detunedA);
                    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
                    opl3_write_reg(p_vpmctx, 1, 0xB0 + ch, detunedB);
                    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
                    // Update port 1 reg
                    int port_1_reg_addr = 0xA0 + ch + 0x100;
                    p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
                    p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;

                    port_1_reg_addr = 0xB0 + ch + 0x100;
                    p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
                    p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
                }
            }
        }
        if ( p_opts->opl3_keyon_wait > 0)
            vgm_wait_samples(p_vpmctx, p_opts->opl3_keyon_wait);
        
        // p_vpmctx->opl3_state.reg_stamp[reg]更新
        p_vpmctx->opl3_state.reg_stamp[reg] = val;
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        // Stereo panning implementation based on channel number
        // Even channels: port0->right, port1->left
        // Odd channels: port0->left, port1->right
        // This creates alternating stereo placement for a stereo effect
        uint8_t port0_panning, port1_panning;
        if (p_opts->ch_panning) {
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

        opl3_write_reg(p_vpmctx, 0, 0xC0 + ch, (0xF & val) | port0_panning);
        // C0 is always copy to port 1 because DAM and DVB should be applied to port 1 even if it is in Rhythm mode
        //if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
        opl3_write_reg(p_vpmctx, 1, 0xC0 + ch, (0xF & val) | port1_panning); 
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
        //}
        // Update port 1 reg
        int port_1_reg_addr = reg + 0x100;
        p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
        p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
    } else if (reg == 0xBD) {
        p_vpmctx->opl3_state.rhythm_mode = (val & 0x20) != 0;
        opl3_write_reg(p_vpmctx, 0, reg, val);
        opl3_write_reg(p_vpmctx, 1, reg, val); 
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
    } else if (reg >= 0xE0 && reg <= 0xF5) {
        int ch = reg - 0xE0;
        opl3_write_reg(p_vpmctx, 0, reg, val);
        if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
            opl3_write_reg(p_vpmctx, 1, reg, val); 
            if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
            // Update port 1 reg
            int port_1_reg_addr = reg + 0x100;
            p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
            p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
        }
    } else {
        // Write to both ports
        opl3_write_reg(p_vpmctx, 0, reg, val);
        opl3_write_reg(p_vpmctx, 1, reg, val); 
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
        // Update port 1 reg
        int port_1_reg_addr = reg + 0x100;
        p_vpmctx->opl3_state.reg_stamp[port_1_reg_addr] = p_vpmctx->opl3_state.reg[port_1_reg_addr];
        p_vpmctx->opl3_state.reg[port_1_reg_addr] = val;
    }
    return addtional_bytes;
}

/**
 * OPL3 initialization sequence for both ports.
 * Sets FM chip type in OPL3State and initializes register mirror.
 */
int opl3_init(VGMContext *p_vpmctx, FMChipType source_fmchip, const CommandOptions *p_opts) {
    if (!p_vpmctx) return 0;

    memset(p_vpmctx->opl3_state.reg, 0, sizeof(p_vpmctx->opl3_state.reg));
    memset(p_vpmctx->opl3_state.reg_stamp, 0, sizeof(p_vpmctx->opl3_state.reg_stamp));
    p_vpmctx->opl3_state.rhythm_mode = false;
    p_vpmctx->opl3_state.opl3_mode_initialized = false;
    p_vpmctx->opl3_state.source_fmchip = source_fmchip;

    // Get 'ESEOPL3_FREQSEQ' enviornement variable
    const char *seq = getenv("ESEOPL3_FREQSEQ");
    if (p_opts->debug.verbose) {
        if (seq && (seq[0]=='a' || seq[0]=='A')) g_freqseq_mode = FREQSEQ_AB;
        fprintf(stderr, "[FREQSEQ] selected=%s (ESEOPL3_FREQSEQ=%s)\n",
                g_freqseq_mode==FREQSEQ_BAB ? "BAB" : "AB",
                seq ? seq : "(unset)");
    }

    int addtional_bytes = 0;

    // Initialize OPL3VoiceDB
    opl3_voice_db_init(&p_vpmctx->opl3_state.voice_db);

    // OPL3 global registers (Port 1 only)
    opl3_write_reg(p_vpmctx, 1, 0x05, 0x01);  // OPL3 enable
    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

    opl3_write_reg(p_vpmctx, 1, 0x04, 0x00);  // Waveform select
    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

    // Port 0 general init
    opl3_write_reg(p_vpmctx, 0, 0x01, 0x00);  // LSI TEST
    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

    opl3_write_reg(p_vpmctx, 0, 0x08, 0x00);  // NTS
    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

    // Port 1 general init
    opl3_write_reg(p_vpmctx, 1, 0x01, 0x00);
    if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;

    // Channel-level control
    for (uint8_t ch = 0; ch < 9; ++ch) {
        uint8_t port0_panning, port1_panning;
        if (p_opts->ch_panning) {
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

        opl3_write_reg(p_vpmctx, 0, 0xC0 + ch, port0_panning);
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
        // C0 is always copy to port 1 because DAM and DVB should be applied to port 1 even if it is in Rhythm mode
        //if (!(p_vpmctx->opl3_state.rhythm_mode && ch >= 6 && ch <= 8)) {
        opl3_write_reg(p_vpmctx, 1, 0xC0 + ch, port1_panning); 
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
    }

    // Waveform select registers (port 0 and 1)
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs) / sizeof(ext_regs[0]); ++i) {
        opl3_write_reg(p_vpmctx, 0, ext_regs[i], 0x00);
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
        opl3_write_reg(p_vpmctx, 1, ext_regs[i], 0x00);
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
    }
    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        opl3_write_reg(p_vpmctx, 1, reg, 0x00);
        if (opl3_should_account_port1(&(p_vpmctx->status))) addtional_bytes += 3;
    }

    return addtional_bytes;
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
uint8_t make_carrier_40_from_vol(VGMContext *p_vgmctx,const OPL3VoiceParam *vp, uint8_t reg3n, const CommandOptions *p_opts)
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
        if(p_opts->debug.verbose) {
            fprintf(stderr,
            "[VOLMAP] reg3n=%02X vol=%u baseTL=%u => newTL=%u (STEP=%u)\n",
            reg3n, vol, base_tl, (unsigned)tl, STEP);
        }
        dbg_cnt++;
    }

    return (uint8_t)(ksl_bits | (uint8_t)tl);
}