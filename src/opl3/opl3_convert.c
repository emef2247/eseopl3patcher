#include "opl3_convert.h"
#include "opl3_voice.h"
#include "../opll/opll_to_opl3_wrapper.h"
#include "../vgm/vgm_helpers.h"
#include "../vgm/vgm_header.h"       /* OPL3_CLOCK */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

/**
 * Calculate frequency in Hz for given chip, clock, block, fnum.
 * @param chip Chip type (OPLL / OPL3)
 * @param clock Chip clock in Hz (from VGM header)
 * @param block Block value
 * @param fnum F-Number
 */
double calc_fmchip_frequency(FMChipType chip,
                             double clock,
                             unsigned char block,
                             unsigned short fnum)
{
    printf("[DEBUG] calc_fmchip_frequency: chip=%d clock=%.0f block=%u fnum=%u\n", chip, clock, block, fnum);
    switch (chip) {
        case FMCHIP_YM2413: // OPLL
            // f_out = (clock / 72) * 2^(block - 1) * (FNUM / 512)
            // freq = base * 2^(block-1) * (fnum / 512)
            return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);

        case FMCHIP_YMF262: // OPL3
            // f_out = (clock / 288) * 2^(block - 1) * (FNUM / 1024)
            // freq = fnum * base / 2^(20 - block)
            return  (clock / 288) * (fnum / 1024.0) * ldexp(1.0, block);
        case FMCHIP_YM3812: // OPL2
        case FMCHIP_YM3526:
        case FMCHIP_Y8950:
            // f_out = (clock / 72) * 2^(block - 1) * (FNUM / 1024)
            return (clock / 72.0) *
                   ldexp(1.0, block - 1) *
                   ((double)fnum / 1024.0);

        default:
            return 0.0;
    }
}

double calc_opl3_frequency (double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opl3_frequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
            // f_out = (clock / 288) * 2^(block - 1) * (FNUM / 1024)
            // freq = fnum * base / 2^(20 - block)
    return (clock / 288) * (fnum / 1024.0) * ldexp(1.0, block);
}

void opl3_calc_fnum_block_from_freq(double freq, double clock, unsigned char *out_block, unsigned short *out_fnum) {
    if (freq <= 0.0 || clock <= 0.0) {
        if (out_block) *out_block = 0;
        if (out_fnum) *out_fnum = 0;
        return;
    }

    // Calculate the base frequency
    double base = clock / 288.0;

    // Find the block and FNUM
    unsigned char block = 0;
    unsigned short fnum = 0;

    for (block = 0; block < 8; ++block) {
        fnum = (unsigned short)(freq * 1024.0 / (base * (1 << block)) + 0.5);
        if (fnum > 1023) continue;

        // Check if this is a valid block/FNUM combination
        double calc_freq = base * (1 << block) * ((double)fnum / 1024.0);
        if (fabs(calc_freq - freq) < 1e-12) break;
    }

    if (out_block) *out_block = block;
    if (out_fnum) *out_fnum = fnum;
}

/**
 * Calculate OPL3 FNUM and block for a target frequency.
 * Output values are clipped to valid OPL3 ranges.
 * @param freq Frequency in Hz
 * @param clock OPL3 clock in Hz
 * @param[out] block Pointer to integer for resulting block
 * @param[out] fnum Pointer to integer for resulting FNUM
 */

/* ----------------------
   OPL3 inverse (we assume existing function but include it here)
   freq -> OPL3 FNUM/BLOCK. prefer minimal absolute freq error; tie-breaker prefer smaller block.
*/
void opl3_calc_fnum_block_from_freq_ldexp(double freq, double clock,unsigned char *out_block,unsigned short *out_fnum, double *out_err)
{
    if (freq <= 0.0 || clock <= 0.0) {
        if (out_block) *out_block = 0;
        if (out_fnum) *out_fnum = 0;
        if (out_err) *out_err = 0.0;
        return;
    }
    unsigned char best_b = 0;
    unsigned short best_f = 0;
    double best_err = DBL_MAX;
    double base = clock / 72.0;

    for (int b = 0; b < 8; ++b) {
        // raw_fnum = freq * 2^(20-b) / base
        double raw = ldexp(freq / base, 20 - b);
        long cand = (long)(raw + 0.5);
        if (cand < 0 || cand > 1023) continue;
        double calc_freq = ldexp((double)cand * base, -(20 - b));
        double err = fabs(calc_freq - freq);
        if (err + 1e-12 < best_err || (fabs(err - best_err) < 1e-12 && b < best_b)) {
            best_err = err; best_b = (unsigned char)b; best_f = (unsigned short)cand;
        }
    }
    if (best_err == DBL_MAX) {
        *out_block = 0; *out_fnum = 0; return;
    }
    if (out_block) *out_block = best_b;
    if (out_fnum)  *out_fnum  = best_f;
    if (out_err)   *out_err   = best_err;
}


/** Find the best OPL3 FNUM and block for a given frequency. */
void opl3_find_fnum_block_with_pref_block(double freq, double clock,
                                          unsigned char *best_block,
                                          unsigned short *best_fnum,
                                          double *best_err, int pref_block) {
    double min_cost = DBL_MAX;
    unsigned char b_best = 0;
    unsigned short f_best = 0;
    for (unsigned char b = 0; b < 8; ++b) {
        double base = (clock / 288.0) * (1 << b);
        int cand_fnum = (int)(freq * 1024.0 / base + 0.5);
        if (cand_fnum < 0 || cand_fnum > 1023) continue;
        double calc_freq = base * ((double)cand_fnum / 1024.0);
        double freq_err  = fabs(calc_freq - freq);
        double block_penalty = fabs((double)b - (double)pref_block) * 0.5;
        double cost = freq_err + block_penalty;
        if (cost < min_cost) {
            min_cost = cost;
            b_best = b;
            f_best = (unsigned short)cand_fnum;
        }
    }
    *best_block = b_best;
    *best_fnum  = f_best;
    *best_err   = min_cost;
}

void opl3_find_fnum_block_with_weight(double freq, double clock,
                                      unsigned char *best_block, unsigned short *best_fnum,
                                      double *best_err, int pref_block, double mult_weight) {
    double min_cost = DBL_MAX;
    double best_freq_err = DBL_MAX;
    unsigned char b_best = 0;
    unsigned short f_best = 0;
    for (unsigned char b = 0; b < 8; ++b) {
        double base = (clock / 288.0) * (1 << b);
        int cand_fnum = (int)(freq * 1024.0 / base + 0.5);
        if (cand_fnum < 0 || cand_fnum > 1023) continue;
        double calc_freq = base * ((double)cand_fnum / 1024.0);
        double freq_err = fabs(calc_freq - freq);
        double block_penalty = 0.0;
        if (pref_block >= 0) {
            double weight = 0.25 + (mult_weight * 0.25);
            block_penalty = fabs((double)b - (double)pref_block) * weight;
        }
        double cost = freq_err + block_penalty;
        if (cost < min_cost) {
            min_cost = cost;
            best_freq_err = freq_err;
            b_best = b;
            f_best = (unsigned short)cand_fnum;
        }
    }
    *best_block = b_best;
    *best_fnum  = f_best;
    *best_err   = best_freq_err;
}

void opl3_find_fnum_block_with_ml(double freq, double clock,
                                  unsigned char *best_block, unsigned short *best_fnum,
                                  double *best_err,int pref_block,
                                  double mult0,double mult1) {
    double min_cost = DBL_MAX;
    double best_freq_err = DBL_MAX;
    unsigned char b_best = 0;
    unsigned short f_best = 0;
    double ml_weight = 1.0 + 0.1 * ((mult0 + mult1) / 2.0);
    for (unsigned char b = 0; b < 8; ++b) {
        double base = (clock / 288.0) * (1 << b);
        int cand_fnum = (int)(freq * 1024.0 / base + 0.5);
        if (cand_fnum < 0 || cand_fnum > 1023) continue;
        double calc_freq = base * ((double)cand_fnum / 1024.0);
        double freq_err = fabs(calc_freq - freq);
        double block_penalty = 0.0;
        if (pref_block >= 0) {
            block_penalty = fabs((double)b - (double)pref_block) * ml_weight * 0.1;
        }
        double cost = freq_err + block_penalty;
        if (cost < min_cost) {
            min_cost = cost;
            best_freq_err = freq_err;
            b_best = b;
            f_best = (unsigned short)cand_fnum;
        }
    }
    *best_block = b_best;
    *best_fnum  = f_best;
    *best_err   = best_freq_err;
}

/*
 * freq : 目標周波数 (Hz) - ここには calc_fmchip_frequency(FMCHIP_YM2413, ...) の戻り値を渡す
 * clock: 変換先チップのクロック (Hz) - ここには OPL3 のクロック (get_fm_target_clock()) を渡す
 *
 * pref_block : OPLL 側の block（-1 なら無視）
 * mult0, mult1: キャリア/モジュレータの MULT (0..15)。2op の場合 mult1 = 0 で OK
 *
 * best_err は 最終的に選ばれた (周波数誤差 in cents) を返します（Hz ではない点に注意）
 *
 * 使い方: この関数は「周波数整合を第一優先」にして、その上で ML による僅かな
 *        block 偏向を許容する設計です。
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
    const double PENALTY_CENTS_PER_BLOCK = 50.0; /* ブロック差1につき何セント分ペナルティを与えるか */
    const double ML_ALPHA = 0.08; /* ML の影響係数（0.0 = 無視、大きいと ML を強く反映） */

    double min_cost = DBL_MAX;
    double chosen_cents_err = DBL_MAX;
    unsigned char b_best = 0;
    unsigned short f_best = 0;

    /* ML合成 (2opなら mult1=0). 平均をとっておく */
    double ml_mean = (mult0 + mult1) * 0.5;

    /* ml_factor: 1.0 が基本。ML が大きいほど block ペナルティを少し増やす */
    double ml_factor = 1.0 + ML_ALPHA * ml_mean; /* 例: mult_mean=4 -> 1 + 0.08*4 = 1.32 */

    /* 基底の周波数比 (clock/288) を先に計算（高精度に ldexp を使う） */
    for (int b = 0; b < 8; ++b) {
        /* base = (clock / 288.0) * 2^b */
        double base = (clock / 288.0) * ldexp(1.0, b);

        /* 理想的な fnum (実数) */
        double ideal_fnum = freq * 1024.0 / base;

        /* 四捨五入して候補 FNUM を得る */
        int cand_fnum = (int)floor(ideal_fnum + 0.5);

        /* 範囲チェック */
        if (cand_fnum < 0) cand_fnum = 0;
        if (cand_fnum > 1023) cand_fnum = 1023;

        /* 計算後の周波数 */
        double calc_freq = base * ((double)cand_fnum / 1024.0);

        /* 周波数差を cents で評価（対数差） */
        double cents_err = fabs(hz_to_cents(calc_freq, freq)); /* >=0 */

        /* ブロック差のペナルティ（cents 単位で） */
        double block_penalty = 0.0;
        if (pref_block >= 0) {
            block_penalty = fabs((double)b - (double)pref_block) * PENALTY_CENTS_PER_BLOCK * ml_factor;
        }

        /* 合成コスト（cents） */
        double cost = cents_err + block_penalty;

        /* デバッグ: 各候補の中身を出力したいときは有効にする */
        #if 0
        printf("DEBUG b=%d base=%.6f ideal_fnum=%.3f cand=%d calc_freq=%.3f cents_err=%.2f block_penalty=%.2f cost=%.2f\n",
               b, base, ideal_fnum, cand_fnum, calc_freq, cents_err, block_penalty, cost);
        #endif

        if (cost < min_cost) {
            min_cost = cost;
            chosen_cents_err = cents_err;
            b_best = (unsigned char)b;
            f_best = (unsigned short)cand_fnum;
        }
    }

    *best_block = b_best;
    *best_fnum  = f_best;
    *best_err   = chosen_cents_err; /* cents 単位 */
}


/**
 * Attenuate TL (Total Level) using volume ratio.
 * @param orig_val Original TL register value.
 * @param v_ratio Volume ratio (0.0 .. 1.0).
 * @return TL value modified by ratio.
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
 * Always writes to the register mirror (reg[]).
 * @param p_state Pointer to OPL3State.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param port OPL3 port (0 or 1).
 * @param reg Register address.
 * @param value Value to write.
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
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @param regA Frequency LSB.
 * @param regB Frequency MSB.
 * @param detune Detune amount (percent).
 * @param p_outA Pointer to output detuned LSB.
 * @param p_outB Pointer to output detuned MSB.
 */
void get_detuned_value(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB) {
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
    bytes_written += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xA0 + ch, value_a, opts);
    // Write FNUM MSB (B0..B8)
    bytes_written += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, value_b, opts);
    return bytes_written;
}

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus.
 * @param p_state Pointer to OPL3State.
 * @param reg Register address.
 * @param val Value to write.
 * @param detune Detune amount.
 * @param opl3_keyon_wait KeyOn/Off wait (in samples).
 * @param ch_panning Channel panning mode.
 * @param v_ratio0 Volume ratio for port 0.
 * @param v_ratio1 Volume ratio for port 1.
 * @return Bytes written to port 1.
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val, const CommandOptions *opts
    // double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1
) {
    int addtional_bytes = 0;

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
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, p_state->reg[0xA0 + ch]);
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        // Extra 3 bytes
        addtional_bytes += 3;
        vgm_wait_samples(p_music_data, p_vstat, opts->opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        get_detuned_value(p_state, ch, p_state->reg[0xA0 + ch], val, opts->detune, &detunedA, &detunedB);
        opl3_write_reg(p_state, p_music_data, 1, 0xA0 + ch, detunedA); addtional_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode))) {
            opl3_write_reg(p_state, p_music_data, 1, reg, detunedB); addtional_bytes += 3;
        }
        vgm_wait_samples(p_music_data, p_vstat, opts->opl3_keyon_wait);
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

     // Initialize OPL3VoiceDB
    p_state->source_fmchip = source_fmchip;

     // Initialize OPL3VoiceDB
    opl3_voice_db_init(&p_state->voice_db);

    // OPL3 global registers (Port 1 only)
    opl3_write_reg(p_state, p_music_data, 1, 0x05, 0x01);  // OPL3 enable
    opl3_write_reg(p_state, p_music_data, 1, 0x04, 0x00);  // Waveform select

    // Port 0 general init
    opl3_write_reg(p_state, p_music_data, 0, 0x01, 0x00);  // LSI TEST
    opl3_write_reg(p_state, p_music_data, 0, 0x08, 0x00);  // NTS

    // Port 1 general init
    // opl3_write_reg(p_state, p_music_data, 1, 0x01, 0x00);

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

    /* last_key は memset で 0 済み。念のためフラグ */
    p_state->opl3_mode_initialized = 1;
}


/* 
 * YM2413 音量 nibble -> OPL3 TL 加算値マッピング
 *   0..15 (0=最大, 15=最小)
 *   MODE 1: TL_add = vol * 4        (シンプル 4dB 近似)
 *   MODE 2: TL_add = vol * 3        (やや細かく 3dB 近似)
 *   MODE 3: 手動テーブル (例示: 実測/参考差し替え用)
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
/* カスタムテーブル: 必要なら実測値に差し替え */
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
 * YM2413 の $3n レジスタ (reg3n) 下位 4bit (VOL) を OPL3 Carrier TL に反映して
 * 0x40 + slotCar へ書く値 (KSL/TL) を返す。
 *  - vp->op[1].ksl: 上位 2bit (KSL)
 *  - vp->op[1].tl : 基本 TL
 *  - VOL nibble   : TL に加算 (0=加算0, 15=最大加算)
 * クリップ: 63 を超えたら 63。
 */
/* 旧 make_carrier_40_from_vol を完全に置換 */
uint8_t make_carrier_40_from_vol(const OPL3VoiceParam *vp, uint8_t reg3n)
{
    /* YM2413 volume nibble: 0=loudest .. 15=softest */
    uint8_t vol = reg3n & 0x0F;          // 0..15
    uint8_t base_tl = (uint8_t)(vp->op[1].tl & 0x3F);

    /* STEP: 2 => 3dB弱/段 (0.75dB * 2 ≈1.5dB)。必要ならオプション化 */
    const uint8_t STEP = YM2413_VOL_MAP_STEP;

    uint16_t tl = (uint16_t)base_tl + (uint16_t)vol * STEP;
    if (tl > 63) tl = 63;

    /* KSL bits preserved */
    uint8_t ksl_bits = (vp->op[1].ksl & 0x03) << 6;

    /* デバッグ用 (最初数回だけ) */
    static int dbg_cnt = 0;
    if (dbg_cnt < 8) {
        fprintf(stderr,
            "[VOLMAP] reg3n=%02X vol=%u baseTL=%u => newTL=%u (STEP=%u)\n",
            reg3n, vol, base_tl, (unsigned)tl, STEP);
        dbg_cnt++;
    }

    return (uint8_t)(ksl_bits | (uint8_t)tl);
}