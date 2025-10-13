#include <math.h>
#include <stdio.h>
#include "../opl3/opl3_convert.h"
#include "opll2opl3_conv.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>  // getenv
#include <stdarg.h>
static OPLL2OPL3_Scheduler g_scheduler;
#define YM2413_REGS_SIZE 0x40

// リズムパートの仮想チャンネルID定義（通常chと区別しても良い）
#define CH_BD  6   // Bass Drum
#define CH_SD  7   // Snare Drum / Tom
#define CH_CYM 8   // Cymbal / HiHat

#define SAMPLE_RATE 44100.0
#define MIN_GATE_MS 2         /* ensure at least ~2ms key-on */
#define MIN_GATE_SAMPLES ((uint32_t)((MIN_GATE_MS * SAMPLE_RATE) / 1000.0 + 0.5))
#define MAX_PENDING_MS 50     /* max time to wait for missing params */
#define MAX_PENDING_SAMPLES ((uint32_t)((MAX_PENDING_MS * SAMPLE_RATE) / 1000.0 + 0.5))

static inline bool is_keyon_bit_set(uint8_t val) { return (val & 0x10) != 0; }

static void opll_debug_log(const CommandOptions *opts, const char *fmt, ...) {
    if (!opts || !opts->debug.verbose) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static inline void emit_debug(const char *tag1,const char *tag2, OPLL2OPL3_Scheduler *s, int ch, OPLL2OPL3_PendingChannel *p,const CommandOptions *p_opts) {
    if (p_opts->debug.verbose) {
        fprintf(stderr,
        "[OPLL2OPL3][%s][%s] virtual_time:%llu emit_time:%llu ch=%d TL(%d) Voice(%d) FnumL(%d) --- "
        "Active %d Pending %d PendingOff %d PrevKey %d KeyNow %d\n",
        tag1,tag2,
        (unsigned long long)s->virtual_time,
        (unsigned long long)s->emit_time,
        ch,
        p->has_tl, p->has_voice, p->has_fnum_low,
        p->is_active, p->is_pending, p->is_pending_keyoff,
        p->has_keybit_stamp, p->has_keybit
        );
    }
}

static inline void opll2opl3_debug_log(const char *tag1,const char *tag2, OPLL2OPL3_Scheduler *s, int ch, OPLL2OPL3_PendingChannel *p,const CommandOptions *p_opts) {
    if (p_opts->debug.verbose) {
        fprintf(stderr,
        "[OPLL2OPL3][%s][%s] virtual_time:%llu emit_time:%llu ch=%d TL(%d) Voice(%d) FnumL(%d) --- "
        "Active %d Pending %d PendingOff %d PrevKey %d KeyNow %d\n",
        tag1,tag2,
        (unsigned long long)s->virtual_time,
        (unsigned long long)s->emit_time,
        ch,
        p->has_tl, p->has_voice, p->has_fnum_low,
        p->is_active, p->is_pending, p->is_pending_keyoff,
        p->has_keybit_stamp, p->has_keybit
        );
    }
}

void opll2opl3_init_scheduler() {
    OPLL2OPL3_Scheduler *s = &g_scheduler;
    memset(s, 0, sizeof(*s));
    s->virtual_time = 0;
    s->emit_time = 0;

    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ch++) {
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];
        // Register-arrival flags
        p->has_fnum_low   = false; // 1n (0x10..0x18)
        p->has_fnum_high  = false; // 2n (0x20..0x28 includes key bit and block)
        p->has_tl         = false; // 0x30..0x38
        p->has_voice      = false; // 0x30..0x38 upper bits or 0x00..0x07 user patch

        // Register cache (for edge detect + diagnostics)
        uint8_t last_reg_10 = 0;
        uint8_t last_reg_20 = 0;
        uint8_t last_reg_30 = 0;

        // Key/edge state
        p->has_keybit_stamp = false;  // last observed register key bit (register state)
        p->has_keybit      = false;  // last seen register's key bit presence in current update
        p->is_pending     = false ; // we delayed keyon because not all pieces arrived
        p->is_active      = false ; // we have emitted KeyOn for this channel in OPL output
        p->is_pending_keyoff = false; //  a KeyOff is pending to be flushed (used to ensure min gate)
        p->is_keyoff_forced  = false; // internal marker for forced (retrigger) off

    /* stored param values (latest seen while pending) */
        p->fnum_low = 0;
        p->fnum_high = 0;   // lower 2 bits used
        p->fnum_comb = 0;  // assembled 10-bit fnum (maintain)
        p->block = 0;
        p->tl = 0;
        p->voice_id = 0;

    /* time stamp */
        p->keyon_time = 0;    // virtual time when KeyOn was first seen
    }
}

/* Convert OPLL fnum/block to OPL3 fnum/block (10-bit fnum, 3bit block)
   returns true if conversion within range */
bool convert_fnum_block_from_opll_to_opl3(double opll_clock, double opl3_clock, uint8_t opll_block, uint16_t opll_fnum, uint16_t *best_f, uint8_t *best_b)
{
    double freq = calc_opll_frequency(opll_clock, opll_block, opll_fnum);
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_ml_cents(freq, opl3_clock, best_b, best_f, &best_err_cents, (int)opll_block, 0.0, 0.0);

    return true;
}

/**
 * OPLL to OPL3 frequency mapping with optional fast-path.
 * Fast-path heuristics:
 * - OPLL→OPL3 (dst≈4×src): passthrough (FNUM/BLOCK unchanged)
 * - OPLL→OPL2/Y8950 (dst≈src): FNUM×2, BLOCK unchanged, clamp to 1023
 * - Otherwise: precise mapping using opl3_find_fnum_block_with_ml_cents
 */
static void opll_to_opl3_freq_mapping(uint8_t reg1n, uint8_t reg2n, double src_clock, double dst_clock, uint8_t *p_best_f, uint8_t *p_best_block)
{
    uint8_t  opll_block = (uint8_t)((reg2n >> 1) & 0x07);
    uint16_t opll_fnum9 = (uint16_t)(((reg2n & 0x01) << 8) | reg1n);

    // Skip if fnum==0 (prevents 0Hz KeyOn)
    if (opll_fnum9 == 0) {
        if (p_best_block) *p_best_f = reg1n;
        if (p_best_block) {
            uint8_t fnum_msb_2b = (reg2n & 0x01);
            uint8_t block_3b    = (reg2n >> 1) & 0x07;
            *p_best_block = (uint8_t)(fnum_msb_2b | (block_3b << 2));
        }
        if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
            fprintf(stderr, "[FREQMAP] (skip: fnum==0) OPLL blk=%u fnum=%u\n",
                    opll_block, opll_fnum9);
        }
        return;
    }

    // Precise mapping (default behavior)
    double freq = calc_opll_frequency(src_clock, opll_block, opll_fnum9);
    unsigned char best_b = 0;
    unsigned short best_f = 0;
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_ml_cents(freq, dst_clock, &best_b, &best_f, &best_err_cents,
                                       (int)opll_block, 0.0, 0.0);

    *p_best_f      = best_f;
    *p_best_block =  best_b;

    if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
        fprintf(stderr,
            "[FREQMAP] OPLL blk=%u fnum=%u Hz=%.6f -> OPL3 blk=%u fnum=%u (err_cents=%.2f)\n",
            opll_block, opll_fnum9, freq, (unsigned)best_b, (unsigned)best_f, best_err_cents);
    }
}


void emit_opl3_reg_write(VGMContext *p_vgmctx, uint8_t addr, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s, OPLL2OPL3_PendingChannel *p) 
{
    // Skip redundant writes: same address and value as last emitted
    if (p->last_emitted_reg_val[addr] == val)
        return;

    // Emit actual OPL3 write (handles dual port if needed)
    duplicate_write_opl3(&p_vgmctx, addr, val, p_opts);

    // Cache the last emitted value for redundancy suppression
    p->last_emitted_reg_val[addr] = val;

    if (p_opts->debug.verbose) {
        printf("[EMIT] time=%u addr=%02X val=%02X emit_time=%u\n", (unsigned)s->virtual_time, addr, val, (unsigned)s->emit_time);
    }
}

void emit_wait(VGMContext *p_vgmctx, uint16_t samples, OPLL2OPL3_Scheduler *s,const CommandOptions *p_opts) {
    if (samples == 0)
        return;
    vgm_wait_samples(p_vgmctx, samples);

    // Advance emitted timeline
    s->emit_time += samples;

    if (p_opts->debug.verbose) {
        printf("[WAIT] emit_time advanced by %u -> %u\n", samples, (unsigned)s->emit_time);
    }
}

// FNUMレジスタ判定 (ピッチ下位8bit)
static inline bool is_opll_fnum_reg(uint8_t reg)
{
    return (reg >= 0x10 && reg <= 0x18);
}

// KeyOn判定
static inline bool is_opll_keyon_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) != 0;  // bit4=KeyOn
    return false;
}

// KeyOff判定
static inline bool is_opll_keyoff_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) == 0;  // bit4=0ならKeyOff
    return false;
}

// 音量 (Total Level) 判定
// OPLLでは 0x30〜0x38 に TL(5bit) + Inst(4bit) が格納
// TL更新時は KeyOn中にもボリューム変化がある
static inline bool is_opll_tl_reg(uint8_t reg)
{
    return (reg >= 0x30 && reg <= 0x38);
}

// 音色(Voice/Instrument)レジスタ判定
// 0x00〜0x07 : ユーザー音色パラメータ
// 0x30〜0x38 の上位ビットでもプリセット音色指定が入る
static inline bool is_opll_voice_reg(uint8_t reg)
{
    // 音色定義そのもの
    if (reg <= 0x07)
        return true;
    // チャンネル別の音色番号設定 (bit5〜bit7部分)
    if (reg >= 0x30 && reg <= 0x38)
        return true;
    return false;
}

/* Extract channel index given register and rhythm flag (simple melodic mapping) */
int opll_reg_to_channel(uint8_t reg, int is_rhythm_mode) {
    // --- Rhythm mode handling ---
    if (is_rhythm_mode) {
        if (reg == 0x36) return CH_BD;   // Bass Drum
        if (reg == 0x37) return CH_SD;   // Snare/Tom
        if (reg == 0x38) return CH_CYM;  // Cymbal/HiHat
    }

    // --- Standard melodic channels ---
    if ((reg >= 0x10 && reg <= 0x18)) {
        return reg - 0x10;  // FNUM low
    }
    if ((reg >= 0x20 && reg <= 0x28)) {
        return reg - 0x20;  // BLOCK + KeyOn
    }
    if ((reg <= 0x07) || (reg >= 0x30 && reg <= 0x38)) {
        return reg - 0x30;  // TL + voice
    }

    // Not a per-channel register
    return -1;
}

OPLL_ChannelInfo opll_reg_to_channel_ex(uint8_t reg, int rhythm_mode) {
    OPLL_ChannelInfo info = { .ch_index = -1, .type = OPLL_CH_TYPE_INVALID };

    if (reg >= 0x10 && reg <= 0x18) {
        info.ch_index = reg - 0x10;
        info.type = OPLL_CH_TYPE_MELODIC;
        return info;
    }
    if (reg >= 0x20 && reg <= 0x28) {
        info.ch_index = reg - 0x20;
        info.type = OPLL_CH_TYPE_MELODIC;
        return info;
    }
    if ((reg <= 0x07) || (reg >= 0x30 && reg <= 0x38)) {
        int ch = reg - 0x30;
        if (rhythm_mode && ch >= 6) {
            info.type = OPLL_CH_TYPE_RHYTHM;
        } else {
            info.type = OPLL_CH_TYPE_MELODIC;
        }
        info.ch_index = ch;
        return info;
    }

    return info; // invalid
}

// YM2413 (OPLL) KL → YM3812/YMF262 (OPL2/3) KSL変換テーブル
// OPLL KL: 0,1,2,3 → OPL KSL: 0,2,1,3
static const uint8_t OPLL_KL_TO_OPL_KSL[4] = { 0, 2, 1, 3 };

// AR/DR/RR変換テーブル
// OPLL(0-15) → OPL2(0-15) へのレートマッピング
static const uint8_t OPLL2OPL_AR_DR_RR[16] = {
    2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15
};

// SL変換テーブル（3dB/stepは一致するのでそのまま）
static const uint8_t OPLL2OPL_SL[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

// OPLL音量nibble(0-15) → OPL TL加算値（2dB/step相当）
static const uint8_t OPLL_VOL_NIBBLE_TO_OPL_TL[16] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
};

// MULT変換テーブル（0→1で安全な変換）
static const uint8_t OPLL2OPL_MULT_SAFE[16] = {
    1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

/**
 * Convert OPLL 1n register value to OPL3 format.
 */
static inline uint8_t opll_to_opl3_an(uint8_t reg1n) { return reg1n; }

/**
 * Convert OPLL 2n register value to OPL3 format.
 */
static inline uint8_t opll_to_opl3_bn(uint8_t reg2n) {
    uint8_t fnum_msb_2b = (reg2n & 0x01);
    uint8_t block_3b    = (reg2n >> 1) & 0x07;
    uint8_t ko_bit      = (reg2n & 0x10) ? 0x20 : 0x00;
    return (fnum_msb_2b) | (block_3b << 2) | ko_bit;
}

/**
 * Get operator slot number (modulator) for OPL3.
 */
static inline uint8_t opl3_local_mod_slot(uint8_t ch_local) {
    return (uint8_t)((ch_local % 3) + (ch_local / 3) * 8);
}

/**
 * Get operator slot number (carrier) for OPL3.
 */
static inline uint8_t opl3_local_car_slot(uint8_t ch_local) {
    return (uint8_t)(opl3_local_mod_slot(ch_local) + 3);
}

/**
 * Get OPL3 operator register address.
 */
static inline uint8_t opl3_opreg_addr(uint8_t base, uint8_t ch_local, int is_carrier) {
    uint8_t slot = is_carrier ? opl3_local_car_slot(ch_local) : opl3_local_mod_slot(ch_local);
    return (uint8_t)(base + slot);
}

#ifndef OPLL_FORCE_MIN_ATTACK_RATE
#define OPLL_FORCE_MIN_ATTACK_RATE 2
#endif
static inline uint8_t enforce_min_attack(uint8_t ar, const char* stage, int inst, int op_index) {
#if OPLL_FORCE_MIN_ATTACK_RATE > 0
    if (ar < OPLL_FORCE_MIN_ATTACK_RATE) {
        // デバッグ用出力
        #ifdef OPLL_DEBUG_ENFORCE_MIN_AR
        printf("[DEBUG] AR-MinClamp inst=%d op=%d %s rawAR=%u -> %u\n",
               inst, op_index, stage, ar, (unsigned)OPLL_FORCE_MIN_ATTACK_RATE);
        #endif
        return (uint8_t)OPLL_FORCE_MIN_ATTACK_RATE;
    }
#endif
    return ar;
}

void opll_load_voice(int inst, const uint8_t *p_ym2413_regs, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp) return;
    memset(p_vp, 0, sizeof(*p_vp));

    /* ソース選択 */
    const uint8_t *src;
    if (inst == 0 && p_ym2413_regs)         src = p_ym2413_regs;
    else if (inst >= 1 && inst <= 15)       src = YM2413_VOICES[inst - 1];
    else if (inst >= 16 && inst <= 20)      src = YM2413_RHYTHM_VOICES[inst - 16];
    else                                    src = YM2413_VOICES[0];

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] inst=%d RAW:"
            " %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            inst, src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
    }

    /* --- Modulator (0..3) --- */
    uint8_t m_mult_raw =  src[0]       & 0x0F;
    uint8_t m_ksl_raw  = (src[1] >> 6) & 3;
    uint8_t m_tl_raw   =  src[1]       & 0x3F;
    uint8_t m_ar_raw   = (src[2] >> 4) & 0x0F;
    uint8_t m_dr_raw   =  src[2]       & 0x0F;
    uint8_t m_sl_raw   = (src[3] >> 4) & 0x0F;
    uint8_t m_rr_raw   =  src[3]       & 0x0F;

    p_vp->op[0].am    = (src[0] >> 7) & 1;
    p_vp->op[0].vib   = (src[0] >> 6) & 1;
    p_vp->op[0].egt   = (src[0] >> 5) & 1;
    p_vp->op[0].ksr   = (src[0] >> 4) & 1;
    p_vp->op[0].mult  = OPLL2OPL_MULT_SAFE[m_mult_raw];
    p_vp->op[0].ksl   = OPLL_KL_TO_OPL_KSL[m_ksl_raw];
    p_vp->op[0].tl    = OPLL_VOL_NIBBLE_TO_OPL_TL[m_tl_raw];
    p_vp->op[0].ar    = OPLL2OPL_AR_DR_RR[m_ar_raw];
    p_vp->op[0].dr    = OPLL2OPL_AR_DR_RR[m_dr_raw];
    p_vp->op[0].sl    = OPLL2OPL_SL[m_sl_raw];
    p_vp->op[0].rr    = OPLL2OPL_AR_DR_RR[m_rr_raw];
    p_vp->op[0].ws    = 0;

    p_vp->op[0].ar = enforce_min_attack(p_vp->op[0].ar, "Mod", inst, 0);

    /* --- Carrier (4..7) --- */
    uint8_t c_mult_raw =  src[4]       & 0x0F;
    uint8_t c_ar_raw   = (src[6] >> 4) & 0x0F;
    uint8_t c_dr_raw   =  src[6]       & 0x0F;
    uint8_t c_sl_raw   = (src[7] >> 4) & 0x0F;
    uint8_t c_rr_raw   =  src[7]       & 0x0F;
    uint8_t c_ksl_raw  = (src[5] >> 6) & 3;
    uint8_t c_ksr_raw  = (src[4] >> 4) & 1; // キャリアのKSRはsrc[4]のbit4
    // キャリアにはTLレジスタがないので0固定

    p_vp->op[1].am    = (src[4] >> 7) & 1;
    p_vp->op[1].vib   = (src[4] >> 6) & 1;
    p_vp->op[1].egt   = (src[4] >> 5) & 1;
    p_vp->op[1].ksr   = c_ksr_raw;
    p_vp->op[1].mult  = OPLL2OPL_MULT_SAFE[c_mult_raw];
    p_vp->op[1].ksl   = OPLL_KL_TO_OPL_KSL[c_ksl_raw];
    p_vp->op[1].tl    = 0; /* キャリアにはTL無し */
    p_vp->op[1].ar    = OPLL2OPL_AR_DR_RR[c_ar_raw];
    p_vp->op[1].dr    = OPLL2OPL_AR_DR_RR[c_dr_raw];
    p_vp->op[1].sl    = OPLL2OPL_SL[c_sl_raw];
    p_vp->op[1].rr    = OPLL2OPL_AR_DR_RR[c_rr_raw];
    p_vp->op[1].ws    = 0;

    p_vp->op[1].ar = enforce_min_attack(p_vp->op[1].ar, "Car", inst, 1);

    /* Feedback: YM2413 は byte0 下位 3bit */
    uint8_t fb = src[0] & 0x07;
    p_vp->fb[0]  = fb;
    p_vp->cnt[0] = 0;
    p_vp->is_4op = 0;
    p_vp->voice_no = inst;
    p_vp->source_fmchip = FMCHIP_YM2413;

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] inst=%d MOD TL=%u AR=%u DR=%u SL=%u RR=%u | "
            "CAR TL(base)=%u AR=%u DR=%u SL=%u RR=%u FB=%u\n",
            inst,
            p_vp->op[0].tl, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[0].sl, p_vp->op[0].rr,
            p_vp->op[1].tl, p_vp->op[1].ar, p_vp->op[1].dr, p_vp->op[1].sl, p_vp->op[1].rr,
            fb);
    }
}


/**
 * Apply OPL3VoiceParam to a channel.
 */
int apply_opl3_voiceparam(VGMContext *p_vgmctx, int ch, const OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = opl3_opreg_addr(0, ch, 0);
    int slot_car = opl3_opreg_addr(0, ch, 1);

    printf("[DEBUG] Apply OPL3 VoiceParam to ch=%d\n", ch);
    // AM/VIB/EGT/KSR/MULT
    uint8_t opl3_2n_mod = (uint8_t)((p_vp->op[0].am << 7) | (p_vp->op[0].vib << 6) | (p_vp->op[0].egt << 5) | (p_vp->op[0].ksr << 4) | (p_vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((p_vp->op[1].am << 7) | (p_vp->op[1].vib << 6) | (p_vp->op[1].egt << 5) | (p_vp->op[1].ksr << 4) | (p_vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_vgmctx, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0x20 + slot_car, opl3_2n_car, p_opts);

    // KSL/TL
    uint8_t opl3_4n_mod = (uint8_t)(((p_vp->op[0].ksl & 0x03) << 6) | (p_vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((p_vp->op[1].ksl & 0x03) << 6) | (p_vp->op[1].tl & 0x3F));
    bytes += duplicate_write_opl3(p_vgmctx, 0x40 + slot_mod, opl3_4n_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0x40 + slot_car, opl3_4n_car, p_opts);

    // AR/DR
    uint8_t opl3_6n_mod = (uint8_t)((p_vp->op[0].ar << 4) | (p_vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((p_vp->op[1].ar << 4) | (p_vp->op[1].dr & 0x0F));
    bytes += duplicate_write_opl3(p_vgmctx, 0x60 + slot_mod, opl3_6n_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0x60 + slot_car, opl3_6n_car, p_opts);

    // SL/RR
    uint8_t opl3_8n_mod = (uint8_t)((p_vp->op[0].sl << 4) | (p_vp->op[0].rr & 0x0F));
    uint8_t opl3_8n_car = (uint8_t)((p_vp->op[1].sl << 4) | (p_vp->op[1].rr & 0x0F));
    bytes += duplicate_write_opl3(p_vgmctx, 0x80 + slot_mod, opl3_8n_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0x80 + slot_car, opl3_8n_car, p_opts);

    // FB/CNT
    uint8_t c0_val = (uint8_t)(0xC0 | ((p_vp->fb[0] & 0x07) << 1) | (p_vp->cnt[0] & 0x01));
    bytes += duplicate_write_opl3(p_vgmctx, 0xC0 + ch, c0_val, p_opts);

    // WS
    uint8_t opl3_en_mod = (uint8_t)((p_vp->op[0].ws & 0x07));
    uint8_t opl3_en_car = (uint8_t)((p_vp->op[1].ws & 0x07));
    bytes += duplicate_write_opl3(p_vgmctx, 0xE0 + slot_mod, opl3_en_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0xE0 + slot_car, opl3_en_car, p_opts);

    return bytes;
}

/* Flush pending/key state for a single OPL channel.
 * Assumes scheduler s and pending channel s->ch[ch] are valid.
 *
 * Behavior:
 *  - Aligns virtual_time -> emit_time by emitting waits first so emitted
 *    register writes occur at the intended virtual time.
 *  - If a pending KeyOff exists, ensure minimum gate length before emitting KeyOff.
 *  - If a pending KeyOn exists and all required params are present (or max wait elapsed),
 *    convert frequency to OPL3, emit operator params, A-reg, TL, then B-reg with key bit.
 */
void flush_channel_for_OPL(VGMContext *p_vgmctx, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s, int ch)
{
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];

   /* Quick exit: nothing to do */
    if (!p->is_pending && !p->has_keybit && !p->is_pending_keyoff) return;

    /* 1) Align virtual_time and emit_time: if virtual_time advanced, emit wait now.
     *    This ensures subsequent register writes happen at correct output time.
     */
    int64_t diff = (int64_t)s->virtual_time - (int64_t)s->emit_time;
    if (diff > 0) {
        /* emit prior accumulated waits */
        opll2opl3_debug_log("FLUSH","(1)ALIGN TO VIRTUAL TIME", s,  ch, p, p_opts);
        emit_wait(p_vgmctx, (uint32_t)diff, s, p_opts);
    }

    /* 2) Handle pending KeyOff first (enforce minimum gate length).
     *    When KeyOff is requested we must ensure the note was held for at least MIN_GATE_SAMPLES.
     */
    if (p->is_pending_keyoff) {
        sample_t gate_len = (p->keyon_time <= s->virtual_time) ? (s->virtual_time - p->keyon_time) : 0;
        if (gate_len < (sample_t)MIN_GATE_SAMPLES) {
            uint32_t add_wait = (uint32_t)((sample_t)MIN_GATE_SAMPLES - gate_len);
            /* Emit additional wait so we respect minimum gate */
            opll2opl3_debug_log("FLUSH","(2)ADD WAIT FOR MIN_GATE_SAMPLES", s,  ch, p, p_opts);
            emit_wait(p_vgmctx, add_wait, s, p_opts);
        }
        /* Emit KeyOff: write B-register (0xB0 + channel) with key bit cleared.
         * Keep FNUM high bits and block as currently stored in p->fnum_comb / p->block.
         */
        uint8_t b_reg = (uint8_t)(0xB0 + ch);
        uint8_t b_val = (uint8_t)(((p->fnum_comb >> 8) & 0x03) | ((p->block & 0x07) << 2));
        opll2opl3_debug_log("FLUSH","(3)EMIT KEYOFF PENDED", s,  ch, p, p_opts);
        emit_opl3_reg_write(p_vgmctx, b_reg, b_val, p_opts, s, p);

        /* Mark as emitted */
        p->is_pending_keyoff = false;
        p->is_active = false;
        p->has_keybit = false;
        p->has_keybit_stamp = false;
        p->is_keyoff_forced = false;
        return;
    }

    /* 3) Handle pending KeyOn */
    if (p->is_pending) {
        /* Are required params present? */
        bool ready = (p->has_fnum_low && p->has_fnum_high && p->has_tl && p->has_voice && p->has_keybit);

        if (!ready) {
            /* If this KeyOn has been pending for too long, force flush anyway. */
            sample_t waited = s->virtual_time - p->keyon_time;
            if (waited < (sample_t)MAX_PENDING_SAMPLES) {
                /* Not ready and not timed out -> postpone flush */
                return;
            }
            /* otherwise force emit with whatever we have */
            ready = true;
        }

        /* Now emit the full sequence for KeyOn */
        if (ready) {
            /* 3a) Convert OPLL fnum/block -> OPL3 fnum/block */
            uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
            uint8_t dst_block = p->block & 0x07;
            /* try precise conversion; convert_fnum_block_from_opll_to_opl3 returns bool (true=ok) */
            (void)convert_fnum_block_from_opll_to_opl3(
                p_vgmctx->source_fm_clock,
                p_vgmctx->target_fm_clock,
                p->block,
                p->fnum_comb,
                &dst_fnum,
                &dst_block);
            /* store converted values locally (we may keep original OPLL values if desired) */
            p->fnum_comb = dst_fnum & 0x3FF;
            p->block = dst_block & 0x07;

            /* 3b) Emit operator voice params (apply mapped OPL3 voice) */
            /* Load voice mapping from YM2413 patch table or user patch */
            OPL3VoiceParam vp;
            opll_load_voice((int)p->voice_id, &(p_vgmctx->opl3_state.reg), &vp, p_opts);

            /* Apply voice params to OPL3 registers for this channel.
             * We assume opl3_voiceparam_apply writes operator registers (eg 0x20/0x40/0x60 etc).
             * Use p_vgm_context->buffer / status for the write helpers.
             */
            /* NOTE: adapt function call to your actual signature if different. */
            opl3_voiceparam_apply(p_vgmctx, ch, &vp, p_opts);

            /* 3c) Emit A register (FNUM low) */
            uint8_t a_reg = (uint8_t)(0xA0 + ch);
            uint8_t a_val = (uint8_t)(p->fnum_comb & 0xFF);
            opll2opl3_debug_log("FLUSH","(5)FNUML", s,  ch, p, p_opts);
            emit_opl3_reg_write(p_vgmctx, a_reg, a_val, p_opts, s, p);

            /* 3d) Emit TL(s) - simplified: write carrier TL slot (0x40 + ch) from mapped TL.
             * In accurate OPL3 you must write TL per operator (two ops). This demo writes
             * a combined/derived value; adapt to full per-operator TL writes if you implement.
             */
            {
                OPL3VoiceParam vp_cache;
                uint8_t tl40 = make_carrier_40_from_vol(p_vgmctx, &vp_cache, p_vgmctx->opl3_state.reg[0x40 + ch], p_opts);
                /* If you want to use p->tl -> map via OPLL->OPL TL mapping before writing */
                (void)tl40; /* if tl40 not used, keep to avoid unused warning */
            }
            uint8_t tl_reg = (uint8_t)(0x40 + ch);
            uint8_t tl_val = (uint8_t)(p->tl & 0x3F);
            opll2opl3_debug_log("FLUSH","(6)TL", s,  ch, p, p_opts);
            emit_opl3_reg_write(p_vgmctx, tl_reg, tl_val, p_opts, s, p);

            /* 3e) Emit B register (FNUM high + block + key bit set) */
            uint8_t b_reg = (uint8_t)(0xB0 + ch);
            uint8_t b_val = (uint8_t)(((p->fnum_comb >> 8) & 0x03) | ((p->block & 0x07) << 2) | 0x20); /* set key bit */
            opll2opl3_debug_log("FLUSH","(7)KEYON", s,  ch, p, p_opts);
            emit_opl3_reg_write(p_vgmctx, b_reg, b_val, p_opts, s, p);

            /* finalize: mark channel as active and clear pending */
            p->is_pending = false;
            p->is_active = true;
            /* keep has_* flags so subsequent events reuse current param values */
        }
    }
}
 
/* flush all channels */
void flush_all_channels(VGMContext *p_vgmctx, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    int64_t diff = (int64_t)s->virtual_time - (int64_t)s->emit_time;
    if (diff > 0) {
        emit_wait(p_vgmctx, diff, s, p_opts);
    } 
    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ++ch) {
        /* ensure any pending keyoff processed */
        if (s->ch[ch].is_pending_keyoff || s->ch[ch].is_pending) flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
    }
}

void handle_opll_write (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s) 
{
    bool is_rhythm_mode = false;
    int ch = opll_reg_to_channel(reg, is_rhythm_mode);
    if (ch < 0 || ch >= OPLL_NUM_CHANNELS)
        return; /* ignore non-channel writes (rhythm etc.) */

    OPLL2OPL3_PendingChannel *p = &s->ch[ch];

    /* --- Handle FNUM Low (0x10..0x18) ---------------------- */
    if (reg >= 0x10 && reg <= 0x18) {
        p->fnum_low = val;
        p->fnum_comb = (uint16_t)((p->fnum_comb & 0x300) | val);
        p->has_fnum_low = true;
        p->last_reg_10 = val;
        opll2opl3_debug_log("HANDLE","FNUM low", s,  ch, p, p_opts);
        /* If this channel was pending, we might now have all params. */
        if (p->is_pending)
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        return;
    }

    /* --- Handle FNUM High + Block + Key (0x20..0x28) -------- */
    if (reg >= 0x20 && reg <= 0x28) {
        uint8_t fhi   = val & 0x03;
        uint8_t block = (val >> 2) & 0x07;
        bool keybit   = (val & 0x10) != 0;

        /* Cache registers and detect edge */
        bool prev_keybit = p->has_keybit_stamp;
        p->fnum_high = fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->fnum_comb & 0xFF));
        p->block = block;
        p->has_fnum_high = true;
        p->last_reg_20 = val;
        p->has_keybit_stamp = keybit;

        /* --- Edge detection --- */
        bool rising_edge  = (!prev_keybit && keybit);
        bool falling_edge = (prev_keybit && !keybit);

        if (rising_edge) {
            /* Start KeyOn sequence */
            p->has_keybit = true;
            p->keyon_time = s->virtual_time;
            p->is_pending = true;

            opll2opl3_debug_log("HANDLE","FNUM High + Block + Key (0->1)", s,  ch, p, p_opts);
            /* If parameters incomplete, mark as pending */
            if (p->has_tl && p->has_voice && p->has_fnum_low) {
                /* All parameters ready — issue flush immediately */
                flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
            }
        } 
        else if (falling_edge) {
            /* Handle KeyOff */
            if (p->is_active) {
                p->is_pending = true;
                p->is_pending_keyoff = true;
                opll2opl3_debug_log("HANDLE","FNUM High + Block + Key (1->0)", s,  ch, p, p_opts);
                flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
            } else {
                /* If not active, just clear keybit flags */
                p->has_keybit = false;
                p->is_pending = false;
                opll2opl3_debug_log("HANDLE","FNUM High + Block + Key (1->0)", s,  ch, p, p_opts);
            }
        }
        return;
    }

    /* --- Handle INST/TL (0x30..0x38) ---------------------------- */
    if  (reg >= 0x30 && reg <= 0x38) {
        p->tl = val & 0x3F;
        p->voice_id =  ((val >> 4) & 0x0F);
        p->has_tl = true;
        p->last_reg_30 = val;
        opll2opl3_debug_log("HANDLE","INST/TL", s,  ch, p, p_opts);
        if (p->is_pending)
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        return;
    }

    /* --- Handle Voice / Instrument selection ----------------
     * For simplicity: treat low registers 0x00–0x07 as user-patch area,
     * and 0x30–0x38 as instrument selection nibble.
     */
    if ((reg <= 0x07)) {
        p->voice_id = (val & 0x0F);
        p->has_voice = true;
        opll2opl3_debug_log("HANDLE","USER VOICE", s,  ch, p, p_opts);
        if (p->is_pending)
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        return;
    }

    /* --- Otherwise: unhandled register group --------------- */
}

void schedule_wait(VGMContext *p_vgmctx, uint32_t wait_samples, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    /* if some channels have been pending too long, force flush */
    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ++ch) {
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];
        if (p->is_pending) {
            sample_t waited = s->virtual_time - p->keyon_time;
            if (waited >= MAX_PENDING_SAMPLES) {
                flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
            }
        }
        /* If a forced keyoff was scheduled (for re-trigger), ensure it is flushed if enough time passed */
        if (p->is_keyoff_forced && p->is_pending_keyoff) {
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        }
    }

    /* Now emit wait to catch up emit_time to virtual_time */
    int64_t diff_time = (int64_t)s->virtual_time - (int64_t)s->emit_time;
    uint32_t to_emit = (diff_time > 0) ? (uint32_t)diff_time : 0u;

    if (to_emit > 0) {
        emit_wait(p_vgmctx, to_emit, s, p_opts);
    }
    // emit_time 更新は emit_wait 内で行われる
}

/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll2opl_command_handler (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, uint16_t wait_samples, const CommandOptions *p_opts)
 {
    // Update timestamp
    g_scheduler.virtual_time = p_vgmctx->timestamp.current_sample;

    if (p_vgmctx->cmd_type == VGMCommandType_RegWrite) {
        p_vgmctx->opl3_state.reg_stamp[reg] = p_vgmctx->opl3_state.reg_stamp[reg];
        p_vgmctx->opl3_state.reg[reg] = val;
        handle_opll_write(p_vgmctx, reg, val, p_opts, &g_scheduler );
    } else if (p_vgmctx->cmd_type == VGMCommandType_Wait) {
        //fprintf(stderr,"[OPLL2OPL3] opll2opl_command_handler: wait_samples=%d\n",wait_samples);
        g_scheduler.virtual_time += wait_samples;
        schedule_wait(p_vgmctx, wait_samples, p_opts, &g_scheduler);
    } else {
        // Should not be occured here
    }
}
