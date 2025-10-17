#include <math.h>
#include <stdio.h>
#include "../opl3/opl3_convert.h"
#include "opll2opl3_conv.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include "../opll/opll_to_opl3_wrapper.h"
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
        memset(&s->ch[ch], 0, sizeof(OPLL2OPL3_PendingChannel));
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];

        // Register-arrival flags
        p->has_fnum_low   = false; // 1n (0x10..0x18)
        p->has_fnum_high  = false; // 2n (0x20..0x28 includes key bit and block)
        p->has_tl         = false; // 0x30..0x38
        p->has_voice      = false; // 0x30..0x38 upper bits or 0x00..0x07 user patch

        p->ignore_first_tl = true; /* 常に初期は破棄モード */

        // Register cache (for edge detect + diagnostics)
        uint8_t last_reg_10 = 0xFF;
        uint8_t last_reg_20 = 0xFF;
        uint8_t last_reg_30 = 0xFF;

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
    fprintf(stderr, "[DEBUG] convert_fnum_block_from_opll_to_opl3: opll_block=%d, opll_fnum=%d\n", opll_block, opll_fnum);
    double freq = calc_opll_frequency(opll_clock, opll_block, opll_fnum);
    fprintf(stderr, "[DEBUG] OPLL freq: %.6f Hz\n", freq);
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_pref_block(freq, opl3_clock, best_b, best_f, &best_err_cents, (int)opll_block);
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
    //f (addr < sizeof(p->last_emitted_reg_val) && p->last_emitted_reg_val[addr] == val)
    //    return;

    // Emit actual OPL3 write (handles dual port if needed)
    uint8_t last_val =  p->last_emitted_reg_val[addr];
    bool first_access = !p->accessed[addr];

    if (p_opts->debug.verbose) {
        fprintf(stderr, "[EMIT][Reg Write] time=%u addr=%02X val=%02X emit_time=%u\n", (unsigned)s->virtual_time, addr, val, (unsigned)s->emit_time);
    }
   if (first_access || val != last_val) {
        duplicate_write_opl3(p_vgmctx, addr, val, p_opts);
        p->accessed[addr] = true;
    }
    p->last_emitted_reg_val[addr] = val;


}

void emit_wait(VGMContext *p_vgmctx, uint16_t samples, OPLL2OPL3_Scheduler *s,const CommandOptions *p_opts) {
    if (samples == 0) {
        if (p_opts->debug.verbose) {
            fprintf(stderr,"[EMIT][WAIT] emit_time was ignored by samples = %u\n", samples);
        }
        return;
    }

    vgm_wait_samples(p_vgmctx, samples);
    // Advance emitted timeline
    s->emit_time += samples;

    if (p_opts->debug.verbose) {
        fprintf(stderr,"[EMIT][WAIT] emit_time advanced by %u -> %u\n", samples, (unsigned)s->emit_time);
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
        fprintf("[DEBUG] AR-MinClamp inst=%d op=%d %s rawAR=%u -> %u\n",
               inst, op_index, stage, ar, (unsigned)OPLL_FORCE_MIN_ATTACK_RATE);
        #endif
        return (uint8_t)OPLL_FORCE_MIN_ATTACK_RATE;
    }
#endif
    return ar;
}

// vgm-conv style YM2413→OPL(3) conversion, with canonical lookup tables

// AR/DR/RR (Attack/Decay/Release Rate) conversion tables
static const uint8_t opll2opl_ar[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };
static const uint8_t opll2opl_dr[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };
static const uint8_t opll2opl_rr[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

// Key scaling level
static const uint8_t opll2opl_ksl[4] = { 0, 2, 1, 3 };

// Multiplier
static const uint8_t opll2opl_mult[16] = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

// Total Level (TL) 4bit YM2413 to 6bit OPL (matches vgm-conv: (TL_nibble << 1))
static inline uint8_t opll2opl_tl(uint8_t tl) {
    return (tl & 0x0F) << 1;
}

// Sustain Level (SL) passthrough (0-15)
static inline uint8_t opll2opl_sl(uint8_t sl) { return sl & 0x0F; }

// --- YM2413→OPL(3) voice conversion ---
void opll_load_voice(int inst, const uint8_t *p_ym2413_regs, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp) return;
    memset(p_vp, 0, sizeof(*p_vp));

    // Source selection (vgm-conv compatible index)
    const uint8_t *src;
    if (inst == 0 && p_ym2413_regs)
        src = p_ym2413_regs;
    else if (inst >= 1 && inst <= 15)
        src = YM2413_VOICES[inst - 1];
    else if (inst >= 16 && inst <= 20)
        src = YM2413_RHYTHM_VOICES[inst - 16];
    else
        src = YM2413_VOICES[0];

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr, "[YM2413->OPL3] inst=%d RAW: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            inst, src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
    }

    // --- Modulator ---
    uint8_t m_am   = (src[0] >> 7) & 1;
    uint8_t m_vib  = (src[0] >> 6) & 1;
    uint8_t m_egt  = (src[0] >> 5) & 1;
    uint8_t m_ksr  = (src[0] >> 4) & 1;
    uint8_t m_mult = src[0] & 0x0F;
    uint8_t m_kl   = (src[2] >> 6) & 0x03;
    uint8_t m_tl   = src[2] & 0x3F;
    uint8_t m_ar   = (src[4] >> 4) & 0x0F;
    uint8_t m_dr   = src[4] & 0x0F;
    uint8_t m_sl   = (src[6] >> 4) & 0x0F;
    uint8_t m_rr   = src[6] & 0x0F;
    uint8_t m_ws   = (src[3] >> 3) & 0x01; // Modulator waveform select

    // --- Carrier ---
    uint8_t c_am   = (src[1] >> 7) & 1;
    uint8_t c_vib  = (src[1] >> 6) & 1;
    uint8_t c_egt  = (src[1] >> 5) & 1;
    uint8_t c_ksr  = (src[1] >> 4) & 1;
    uint8_t c_mult = src[1] & 0x0F;
    uint8_t c_kl   = (src[3] >> 6) & 0x03;
    uint8_t c_ar   = (src[5] >> 4) & 0x0F;
    uint8_t c_dr   = src[5] & 0x0F;
    uint8_t c_sl   = (src[7] >> 4) & 0x0F;
    uint8_t c_rr   = src[7] & 0x0F;
    uint8_t c_ws   = (src[3] >> 4) & 0x01; // Carrier waveform select

    // Feedback
    uint8_t fb     = src[3] & 0x07;

    // --- Conversion tables (vgm-conv compatible) ---
    // Modulator
    p_vp->op[0].am   = m_am;
    p_vp->op[0].vib  = m_vib;
    p_vp->op[0].egt  = m_egt;
    p_vp->op[0].ksr  = m_ksr;
    p_vp->op[0].mult = m_mult;//opll2opl_mult[m_mult];
    p_vp->op[0].ksl  = opll2opl_ksl[m_kl];
    p_vp->op[0].tl   = m_tl;//opll2opl_tl(m_tl);
    p_vp->op[0].ar   = m_ar;//opll2opl_ar[m_ar];
    p_vp->op[0].dr   = m_dr;//opll2opl_dr[m_dr];
    p_vp->op[0].sl   = m_sl;//opll2opl_sl(m_sl);
    p_vp->op[0].rr   = m_rr;//opll2opl_rr[m_rr];
    p_vp->op[0].ws   = m_ws;

    // Carrier
    p_vp->op[1].am   = c_am;
    p_vp->op[1].vib  = c_vib;
    p_vp->op[1].egt  = c_egt;
    p_vp->op[1].ksr  = c_ksr;
    p_vp->op[1].mult = c_mult;//opll2opl_mult[c_mult];
    p_vp->op[1].ksl  = opll2opl_ksl[c_kl];
    p_vp->op[1].tl   = 0; // always zero for carrier
    p_vp->op[1].ar   = c_ar;// opll2opl_ar[c_ar];
    p_vp->op[1].dr   = c_dr;// opll2opl_dr[c_dr];
    p_vp->op[1].sl   = c_sl;// opll2opl_sl(c_sl);
    p_vp->op[1].rr   = c_rr;// opll2opl_rr[c_rr];
    p_vp->op[1].ws   = c_ws;

    p_vp->fb[0] = fb;
    p_vp->cnt[0] = 0;
    p_vp->is_4op = 0;
    p_vp->voice_no = inst;
    p_vp->source_fmchip = 0x01; // YM2413

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr, "[YM2413->OPL3] inst=%d RAW: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            inst, src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
        fprintf(stderr, "[YM2413->OPL3] MOD: AM=%u VIB=%u EGT=%u KSR=%u MULT=%u KSL=%u TL=%u AR=%u DR=%u SL=%u RR=%u WS=%u\n",
            p_vp->op[0].am, p_vp->op[0].vib, p_vp->op[0].egt, p_vp->op[0].ksr, p_vp->op[0].mult,
            p_vp->op[0].ksl, p_vp->op[0].tl, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[0].sl, p_vp->op[0].rr, p_vp->op[0].ws
        );
        fprintf(stderr, "[YM2413->OPL3] CAR: AM=%u VIB=%u EGT=%u KSR=%u MULT=%u KSL=%u TL=%u AR=%u DR=%u SL=%u RR=%u WS=%u\n",
            p_vp->op[1].am, p_vp->op[1].vib, p_vp->op[1].egt, p_vp->op[1].ksr, p_vp->op[1].mult,
            p_vp->op[1].ksl, p_vp->op[1].tl, p_vp->op[1].ar, p_vp->op[1].dr, p_vp->op[1].sl, p_vp->op[1].rr, p_vp->op[1].ws
        );
        fprintf(stderr, "[YM2413->OPL3] FB=%u\n", p_vp->fb[0]);
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
    uint8_t tl = p_vgmctx->opll_state.reg[0x30 + ch] & 0xF;

    printf("[DEBUG] Apply OPL3 VoiceParam to ch=%d\n", ch);
    // AM/VIB/EGT/KSR/MULT
    uint8_t opl3_2n_mod = (uint8_t)((p_vp->op[0].am << 7) | (p_vp->op[0].vib << 6) | (p_vp->op[0].egt << 5) | (p_vp->op[0].ksr << 4) | (p_vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((p_vp->op[1].am << 7) | (p_vp->op[1].vib << 6) | (p_vp->op[1].egt << 5) | (p_vp->op[1].ksr << 4) | (p_vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_vgmctx, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    bytes += duplicate_write_opl3(p_vgmctx, 0x20 + slot_car, opl3_2n_car, p_opts);

    // KSL/TL
    uint8_t opl3_4n_mod = (uint8_t)(((p_vp->op[0].ksl & 0x03) << 6) | (p_vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((p_vp->op[1].ksl & 0x03) << 6) | (tl & 0x3F));
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

    if (p_opts->debug.verbose ) {
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 AM: %s, Vibrato: %s, KSR: %s, EG Type: %d, Freq Multipler: %d\n",ch,opl3_2n_mod,(p_vp->op[0].am) ? "On":"Off",(p_vp->op[0].vib << 6) ? "On":"Off",(p_vp->op[0].ksr << 4) ? "On":"Off",(p_vp->op[0].egt << 5),(p_vp->op[0].mult & 0x0F));
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 AM: %s, Vibrato: %s, KSR: %s, EG Type: %d, Freq Multipler: %d\n",ch,opl3_2n_car,(p_vp->op[1].am) ? "On":"Off",(p_vp->op[1].vib << 6) ? "On":"Off",(p_vp->op[1].ksr << 4) ? "On":"Off",(p_vp->op[1].egt << 5),(p_vp->op[1].mult & 0x0F));
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Key Scaling: %d, Total Level: 0x%02x\n",ch,opl3_4n_mod,(p_vp->op[0].ksl & 0x03), (p_vp->op[0].tl & 0x3F));
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Key Scaling: %d, Total Level: 0x%02x\n",ch,opl3_4n_car,(p_vp->op[1].ksl & 0x03), (p_vp->op[1].tl & 0x3F));
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Attack Rate: %X, Decay Rate: %X\n",ch,opl3_6n_mod,p_vp->op[0].ar,p_vp->op[0].dr);
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Attack Rate: %X, Decay Rate: %X\n",ch,opl3_6n_car,p_vp->op[1].ar,p_vp->op[1].dr);
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Sustain Level: %X, Release Rate: %X\n",ch,opl3_8n_mod,p_vp->op[0].sl,p_vp->op[0].rr);
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Sustain Level: %X, Release Rate: %X\n",ch,opl3_8n_car,p_vp->op[1].sl,p_vp->op[1].rr);
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Waveform Select: %X\n",ch,opl3_en_mod,(p_vp->op[0].ws & 0x07));
    fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Waveform Select: %X\n",ch,opl3_en_car,(p_vp->op[0].ws & 0x07));
    }
    return bytes;
}

/* ---------- flush_channel_for_OPL: updated MIN_GATE + ordering ---------- */
/* Replace your current implementation with this version (adapt helper calls as necessary) */

/* NOTE:
 * - Use p->last_emit_time (set when KeyOn was emitted) for MIN_GATE enforcement.
 * - Align emit_time -> virtual_time once at the top (so any writes are timed correctly).
 * - Compute MIN_GATE as how long the note has been sounding in the EMITTED stream:
 *     emitted_gate_len = s->emit_time - p->last_emit_time
 *   If too short, emit wait = MIN_GATE - emitted_gate_len.
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
        opll2opl3_debug_log("FLUSH","(1)ALIGN TO VIRTUAL TIME", s, ch, p, p_opts);
        emit_wait(p_vgmctx, (uint32_t)diff, s, p_opts);
    }

    /* 2) Handle pending KeyOff first (enforce minimum gate length) */
    if (p->is_pending_keyoff) {
        /* If KeyOn was emitted earlier, p->last_emit_time holds the emit-time when KeyOn was written.
         * The currently emitted time is s->emit_time. Use those to compute actual emitted gate length.
         */
        if (p->last_emit_time == 0) {
            /* if last_emit_time==0, we never actually emitted KeyOn (defensive). Just clear states. */
            p->is_pending_keyoff = false;
            p->is_active = false;
            p->has_keybit = false;
            p->has_keybit_stamp = false;
            p->is_keyoff_forced = false;
            return;
        }

        if (p->is_pending_keyoff && p->is_active) {
             sample_t emitted_gate_len = (s->emit_time >= p->last_emit_time) ? (s->emit_time - p->last_emit_time) : 0;
            if (emitted_gate_len < (sample_t)OPLL_MIN_GATE_SAMPLES) {
                uint32_t add_wait = (uint32_t)((sample_t)OPLL_MIN_GATE_SAMPLES - emitted_gate_len);
                opll2opl3_debug_log("FLUSH","(2)ADD WAIT FOR MIN_GATE_SAMPLES (emitted)", s, ch, p, p_opts);
                /* Emit additional wait on output stream to satisfy min gate on output timeline */
                emit_wait(p_vgmctx, add_wait, s, p_opts);
            }
        }

        /* Now we are safe to emit KeyOff on the output timeline */
        uint8_t b_reg = (uint8_t)(0xB0 + ch);
        uint8_t b_val = (uint8_t)(((p->fnum_comb >> 8) & 0x03) | ((p->block & 0x07) << 2)); /* key bit cleared */
        opll2opl3_debug_log("FLUSH","(3)EMIT KEYOFF PENDED", s, ch, p, p_opts);
        emit_opl3_reg_write(p_vgmctx, b_reg, b_val, p_opts, s, p);

        /* Mark as emitted and clear edge state */
        p->is_pending_keyoff = false;
        p->is_active = false;
        p->has_keybit = false;
        p->has_keybit_stamp = false;
        p->is_keyoff_forced = false;

        /* record last_emit_time (reflects when we did this emit) */
        p->last_emit_time = s->emit_time;

        return;
    }

    /* 3) Handle pending KeyOn */
    if (p->is_pending) {
        /* Are required params present? */
        bool ready = (p->has_fnum_low && p->has_fnum_high && p->has_tl && p->has_voice && p->has_keybit);

        if (!ready) {
            /* If this KeyOn has been pending for too long (input-side time), force flush anyway.
             * We use input-side timeout so that lost/missing parts won't block forever.
             */
            sample_t waited = (s->virtual_time >= p->keyon_time) ? (s->virtual_time - p->keyon_time) : 0;
            if (waited < (sample_t)OPLL_MAX_PENDING_SAMPLES) {
                /* Not ready and not timed out -> postpone flush */
                return;
            }
            /* otherwise force emit with whatever we have */
            ready = true;
        }

        if (ready) {
            /* --- Apply voice params before KeyOn so operator registers are set at KeyOn time --- */
            OPL3VoiceParam vp;
            if (p->voice_id == 0) {
                opll_load_voice(0, p_vgmctx->ym2413_user_patch, &vp, p_opts);
            } else {
                opll_load_voice(p->voice_id, NULL, &vp, p_opts);
            }
            apply_opl3_voiceparam(p_vgmctx, ch, &vp, p_opts);

            /* --- Convert fnum/block to OPL3 and update cached values --- */
            uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
            uint8_t dst_block = p->block & 0x07;
            (void)convert_fnum_block_from_opll_to_opl3(
                p_vgmctx->source_fm_clock,
                p_vgmctx->target_fm_clock,
                p->block,
                p->fnum_comb,
                &dst_fnum,
                &dst_block);
            p->fnum_comb = dst_fnum & 0x3FF;
            p->block = dst_block & 0x07;

            /* --- Emit A register (FNUM low) --- */
            uint8_t a_reg = (uint8_t)(0xA0 + ch);
            uint8_t a_val = (uint8_t)(p->fnum_comb & 0xFF);
            opll2opl3_debug_log("FLUSH","(5)FNUML", s, ch, p, p_opts);
            emit_opl3_reg_write(p_vgmctx, a_reg, a_val, p_opts, s, p);

            /* --- Emit B register (FNUM high + block + KEY=1) --- */
            uint8_t b_reg = (uint8_t)(0xB0 + ch);
            uint8_t b_val = (uint8_t)(((p->fnum_comb >> 8) & 0x03) | ((p->block & 0x07) << 2) | 0x20);
            opll2opl3_debug_log("FLUSH","(7)KEYON", s, ch, p, p_opts);
            emit_opl3_reg_write(p_vgmctx, b_reg, b_val, p_opts, s, p);

            /* finalize: mark channel as active and clear pending */
            p->is_pending = false;
            p->is_active = true;

            /* record when KeyOn actually emitted (output time) */
            p->last_emit_time = s->emit_time;
        }
    }
}

 
/* flush all channels */
void flush_all_channels(VGMContext *p_vgmctx, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    int64_t diff = (int64_t)s->virtual_time - (int64_t)s->emit_time;
    if (diff > 0) {
        fprintf(stderr,
        "\n[OPLL2OPL3][ALL FLUSH][diff] virtual_time:%llu emit_time:%llu --- diff:%d\n",
        (unsigned long long)g_scheduler.virtual_time,
        (unsigned long long)g_scheduler.emit_time,diff
        );
        emit_wait(p_vgmctx, diff, s, p_opts);
    } 
    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ++ch) {
        /* ensure any pending keyoff processed */
        if (s->ch[ch].is_pending_keyoff || s->ch[ch].is_pending) 
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
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

        /* FLUSH policy:
         * - If the channel is pending (KeyOn seen but not yet emitted),
         *   an arriving fnum low may complete the params and should trigger flush.
         * - If channel is already active, do NOT force a full KeyOn re-flush here
         *   unless you intentionally want pitch-bend behavior.
         */
        if (p->is_pending &&
            p->has_fnum_high &&
            p->has_tl &&
            p->has_voice &&
            p->has_keybit) {
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        }

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

        /* Edge detection */
        bool rising_edge  = (!prev_keybit && keybit);
        bool falling_edge = (prev_keybit && !keybit);

        if (rising_edge) {
            /* Start KeyOn sequence: mark pending (waiting for TL/voice/fn low) */
            p->has_keybit = true;
            p->keyon_time = s->virtual_time; /* input-side time observed */
            p->is_pending = true;
            p->is_pending_keyoff = false; /* clear any earlier keyoff pending */

            // /* If params are already present, flush immediately */
            // if (p->has_tl && p->has_voice && p->has_fnum_low) {
            //     flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
            // }
            // --- remove immediate flush ---
            // Instead of flushing right now, wait until TL/Voice arrive.
            return;
        } else if (falling_edge) {
            /* KeyOff observed. If we previously emitted KeyOn (is_active),
             * schedule KeyOff emission (may be delayed by min gate).
             */
            if (p->is_active) {
                p->is_pending_keyoff = true;
                p->is_pending = false; /* not a keyon pending anymore */
                opll2opl3_debug_log("HANDLE","FNUM High + Block + Key (1->0) -> schedule KEYOFF", s,  ch, p, p_opts);
                /* Immediately try to flush (flush will enforce gate using emitted timeline) */
                flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
            } else {
                /* If channel not active (KeyOn never emitted) just clear pending flags. */
                p->has_keybit = false;
                p->is_pending = false;
                p->is_pending_keyoff = false;
                opll2opl3_debug_log("HANDLE","FNUM High + Block + Key (1->0) -> clear (not active)", s,  ch, p, p_opts);
            }
        }
        return;
    }

     /* --- Handle INST/TL (0x30..0x38) ---------------------------- */
    if  (reg >= 0x30 && reg <= 0x38) {
        p->tl = val & 0x3F;
        p->voice_id = ((val >> 4) & 0x0F);

        /* --- 初回TLを完全に無視する制御 --- */
        if (p->ignore_first_tl) {
            /* ログ残すなら残すが、出力コマンドは出さない・状態としても採用しない */
            /* ここで has_tl を立てない/更新しない = 「なかったことにする」 */
            p->ignore_first_tl = false; /* 1回のみ破棄。次からは通常扱い */
            opll2opl3_debug_log("HANDLE","First INST/TL --- ignored", s,  ch, p, p_opts);
            return; /* 破棄して終わり */
        }
        p->has_tl = true;
        /* ensure voice presence is signalled */
        p->has_voice = true;
        p->last_reg_30 = val;
        opll2opl3_debug_log("HANDLE","INST/TL", s,  ch, p, p_opts);
        // is_pendingが立っていない段階（初期設定やKeyOff中）は「まだ発音中ではない」
        // のでここではflushしない。
        if (p->is_pending && p->has_fnum_high && p->has_fnum_low && p->has_voice) {
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        }
        return;
    }

    /* --- Handle user patch area 0x00..0x07 ---------------------- */
    if (reg <= 0x07) {
        p->voice_id = (val & 0x0F);
        p->has_voice = true;
        p_vgmctx->ym2413_user_patch[reg] = val;
        opll2opl3_debug_log("HANDLE","USER VOICE", s,  ch, p, p_opts);
        if (p->is_pending)
            flush_channel_for_OPL(p_vgmctx, p_opts, s, ch);
        return;
    }

    /* --- Otherwise: unhandled register group --------------- */
}

uint32_t compute_opll_voice_hash(uint8_t voice_id, uint8_t tl, uint8_t algo_fb, uint8_t wave_bits)
{
    return ((uint32_t)voice_id << 24)
         | ((uint32_t)tl << 16)
         | ((uint32_t)algo_fb << 8)
         | (uint32_t)wave_bits;
}

/* --- YM2413→OPL変換補助 --- */
static inline uint8_t _R(uint8_t rate) {
    if (rate > 15) return 15;
    if (rate == 0) return 0;
    return rate + 1;
}

static inline uint8_t _KLFix(uint8_t kl) {
    switch (kl) {
        case 1: return 2;
        case 2: return 1;
        case 3: return 3;
        default: return kl;
    }
}

/* YM2413チャンネル→OPL3スロットオフセット */
static inline int get_mod_offset(int ch) {
    static const int tbl[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    return tbl[ch];
}

/* --- OPL3レジスタ書き込み（VGM出力） --- */
static inline void write_opl3_reg(VGMContext *p_vgmctx, uint8_t addr, uint8_t data) {
    /* Portは0固定（9ch分） */
    forward_write(p_vgmctx, 0, addr, data);
    /* ステート反映 */
    p_vgmctx->opll_state.reg[addr] = data;
}

/* --- _writeVoice 等価 --- */
static void write_voice(VGMContext *p_vgmctx, int ch, const OPL3VoiceParam *p_vp, int modVolume, int carVolume, bool key, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    int modOffset = get_mod_offset(ch);
    int carOffset = modOffset + 3;
    const OPL3OpParam *mod = &p_vp->op[0];
    const OPL3OpParam *car = &p_vp->op[1];
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];

    uint8_t d;

    /* AM/VIB/EGT/KSR/MULT */
    emit_opl3_reg_write(p_vgmctx, 0x20 + modOffset, (mod->am << 7) | (mod->vib << 6) | (mod->egt << 5) | (mod->ksr << 4) | mod->mult, p_opts, s, p);
    emit_opl3_reg_write(p_vgmctx, 0x20 + carOffset, (car->am << 7) | (car->vib << 6) | (car->egt << 5) | (car->ksr << 4) | car->mult, p_opts, s, p);

    /* KSL/TL */
    emit_opl3_reg_write(p_vgmctx, 0x40 + modOffset,(_KLFix(mod->ksl) << 6) | (modVolume >= 0 ? modVolume : mod->tl), p_opts, s, p);
    emit_opl3_reg_write(p_vgmctx, 0x40 + carOffset,(_KLFix(car->ksl) << 6) | (carVolume >= 0 ? carVolume : car->tl), p_opts, s, p);

    /* AR/DR */
    emit_opl3_reg_write(p_vgmctx, 0x60 + modOffset,(_R(mod->ar) << 4) | _R(mod->dr), p_opts, s, p);
    emit_opl3_reg_write(p_vgmctx, 0x60 + carOffset,(_R(car->ar) << 4) | _R(car->dr), p_opts, s, p);

    /* SL/RR */
    emit_opl3_reg_write(p_vgmctx, 0x80 + modOffset,(mod->sl << 4) | (!mod->egt ? _R(mod->rr) : 0), p_opts, s, p);
    d = (car->sl << 4) | _R((car->egt || key) ? _R(car->rr) : _R(6));
    emit_opl3_reg_write(p_vgmctx, 0x80 + carOffset, d, p_opts, s, p);

    /* FB/CNT (C0系) */
    emit_opl3_reg_write(p_vgmctx, 0xC0 + ch,(p_vp->fb[0] << 1) | (p_vp->cnt[0] & 1), p_opts, s, p);

    /* WS (E0系) */
    emit_opl3_reg_write(p_vgmctx, 0xE0 + modOffset, mod->ws, p_opts, s, p);
    emit_opl3_reg_write(p_vgmctx, 0xE0 + carOffset, car->ws, p_opts, s, p);
}

static int toTL(int vol, int off) {
    int t = (vol << 2) - off;
    return (t > 0) ? t : 0;
}

/* --- _updateVoice 等価 --- */
void ym2413_update_voice(VGMContext *p_vgmctx, int ch, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    uint8_t *regs = p_vgmctx->opll_state.reg;
    bool rflag = (p_vgmctx->opll_state.is_rhythm_mode != 0);
    uint8_t d = regs[0x30 + ch];
    int inst = (d & 0xF0) >> 4;
    int volume = d & 0x0F;
    bool key = (regs[0x20 + ch] & 0x10) ? true : false;
    
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];
    if (rflag && ch >= 6) {
        OPL3VoiceParam *p_vp = NULL;
        switch (ch) {
            case 6:
                p_vp = &(p_vgmctx->opl3_state.voice_db.p_voices[15]); // index = voice_id - 1
                write_voice(p_vgmctx, 6,p_vp, -1, toTL(volume, 0), key, p_opts, s);

                break;
            case 7:
               p_vp = &(p_vgmctx->opl3_state.voice_db.p_voices[16]); // index = voice_id - 1
                write_voice(p_vgmctx, 7,p_vp, toTL(inst, 0), toTL(volume, 0), key, p_opts, s);
                break;
            case 8:
                p_vp = &(p_vgmctx->opl3_state.voice_db.p_voices[17]); // index = voice_id - 1
                write_voice(p_vgmctx, 8, p_vp, toTL(inst, 0), toTL(volume, 0), key, p_opts, s);
                break;
        }
    } else {
        const OPL3VoiceParam *voice = (inst == 0)
            ?  &(p_vgmctx->opll_state.patches)/* ユーザーボイスを decode(regs) して渡す想定 */
            : &(p_vgmctx->opl3_state.voice_db.p_voices[inst -1]);
        write_voice(p_vgmctx, ch, voice, -1, toTL(volume, 0), key, p_opts, s);
    }

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] ch=%d inst=%d vol=%d key=%d rhythm=%d\n",
            ch, inst, volume, key, rflag);
    }
}



void flush_channel_ym2413_to_opl(VGMContext *p_vgmctx, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s, int ch)
{
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];

    if (p->is_pending_keyoff) {
        // 即時 KeyOff
        if (p_vgmctx->opll_state.reg[0xB0 + ch] != p_vgmctx->opll_state.reg_stamp[0xB0 + ch]) {
            emit_opl3_reg_write(p_vgmctx, 0xB0 + ch, ((p->block << 2) | ((p->fnum_high & 0x03))), p_opts, s, p);
        }
        p->is_active = false;
        p->is_pending_keyoff = false;
        p->is_pending = false;
        opll2opl3_debug_log("FLUSH", "KeyOff", s, ch, p, p_opts);
        return;
    }

    if (p->is_pending) {

        uint8_t port0_panning = 0xF0;
        //if (p_vgmctx->opll_state.reg[0xC0 + ch] != p_vgmctx->opll_state.reg_stamp[0xC0 + ch]) {
        //    emit_opl3_reg_write(p_vgmctx, 0xC0 + ch, ( (0xF & p->tl) | port0_panning), p_opts, s, p);
        //}
        // 音色適用
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3変換前: block=%u, fnum=0x%03X (dec %u) src_clock=%.1f dst_clock=%.1f\n",
                p->block, p->fnum_comb & 0x3FF, p->fnum_comb & 0x3FF,
                p_vgmctx->source_fm_clock, p_vgmctx->target_fm_clock);
        }
        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;
            (void)convert_fnum_block_from_opll_to_opl3(
                p_vgmctx->source_fm_clock,
                p_vgmctx->target_fm_clock,
                p->block,
                p->fnum_comb,
                &dst_fnum,
                &dst_block);

        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3変換後: dst_block=%u, dst_fnum=0x%03X (dec %u)\n",
                dst_block, dst_fnum, dst_fnum);
        }

        // FNUM LSB/MSB 書き込み
        if (p_vgmctx->opll_state.reg[0xA0 + ch] != p_vgmctx->opll_state.reg_stamp[0xA0 + ch]) {
            (p_vgmctx, 0xA0 + ch, (dst_fnum & 0xFF), p_opts, s, p);
        }
        if (p_vgmctx->opll_state.reg[0xB0 + ch] != p_vgmctx->opll_state.reg_stamp[0xB0 + ch]) {
            emit_opl3_reg_write(p_vgmctx, 0xB0 + ch, ((dst_block << 2) | ((dst_fnum & 0x03))) | 0x20, p_opts, s, p);
        }
        p->is_active = true;
        p->is_pending = false;
        opll2opl3_debug_log("FLUSH", "KeyOn", s, ch, p, p_opts);
    }
}


// ------------------------------------------------------------
// 書き込みハンドラ
// ------------------------------------------------------------
void handle_opll_write_ym2413_to_opl (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s) 
{
    if (reg == 0x0e) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];

        bool is_rhythm_mode = (val & 0x20) != 0;

        if(!p_vgmctx->opll_state.is_rhythm_mode && is_rhythm_mode) {
            p_vgmctx->opll_state.is_rhythm_mode = true;

            OPL3VoiceParam vp;
            int voice_id = 16;
            opll_load_voice(voice_id, NULL, &vp, p_opts);
            apply_opl3_voiceparam(p_vgmctx, 6, &vp, p_opts);

        } else if (p_vgmctx->opll_state.is_rhythm_mode && !is_rhythm_mode) {
            p_vgmctx->opll_state.is_rhythm_mode = false;

        }
    }
    /* --- FNUM Low (0x10..0x18) --- */
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];
        p->fnum_comb = (uint16_t)((p->fnum_comb & 0x300) | val);
        p->has_fnum_low = true;
        p->last_reg_10 = val;

        opll2opl3_debug_log("HANDLE", "FNUM Low", s, ch, p, p_opts);
        bool keybit   = (val & 0x80) != 0;
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3変換前: block=%u, fnum=0x%03X (dec %u) src_clock=%.1f dst_clock=%.1f\n",
                p->block, p->fnum_comb & 0x3FF, p->fnum_comb & 0x3FF,
                p_vgmctx->source_fm_clock, p_vgmctx->target_fm_clock);
        }

        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;
            (void)convert_fnum_block_from_opll_to_opl3(
                p_vgmctx->source_fm_clock,
                p_vgmctx->target_fm_clock,
                p->block,
                p->fnum_comb,
                &dst_fnum,
                &dst_block);
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3変換後: dst_block=%u, dst_fnum=0x%03X (dec %u)\n",
                dst_block, dst_fnum, dst_fnum);
        }

        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr, "[DEBUG][0x10] ch=%d val=0x%02X (should be FNUM-LSB)\n", ch, val);
        }
        uint8_t reg_bn = ((p_vgmctx->opll_state.reg[0x20 + ch]  & 0x1F) << 1) | ((val & 0x80) >> 7) ;
        uint8_t reg_an = (val & 0x7f) << 1;

         if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] reg_bn:0x%02x reg_an:0x%02x\n",reg_bn,reg_an);
        }
        emit_opl3_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts, s, p);
        emit_opl3_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts, s, p);
        return;
    }

    /* --- FNUM High + Block + Key (0x20..0x28) --- */
    if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];

        uint8_t fhi   = val & 0x03;
        uint8_t block = (val >> 2) & 0x07;
        bool keybit   = (val & 0x10) != 0;
        bool prev_key = p->key_state;

        bool rising_edge  = (prev_key != keybit) &&(!prev_key && keybit);
        bool falling_edge = (prev_key != keybit) && (prev_key && !keybit);

        p->fnum_high = fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->last_reg_10 & 0xFF));
        p->block = block;
        p->last_reg_20 = val;

         /* --- Apply voice params before KeyOn so operator registers are set at KeyOn time --- */
        OPL3VoiceParam vp;
        if (p->voice_id == 0) {
            opll_load_voice(0, p_vgmctx->ym2413_user_patch, &vp, p_opts);
        } else {
            opll_load_voice(p->voice_id, NULL, &vp, p_opts);
        }
        ym2413_update_voice(p_vgmctx, ch, &vp, p_opts);

        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr, "[DEBUG][0x20] ch=%d val=0x%02X (should be FNUM-LSB)\n", ch, val);
        }
        uint8_t reg_bn = ((val  & 0x1F) << 1) | ((p_vgmctx->opll_state.reg[0x10 + ch] & 0x80) >> 7) ;
        uint8_t reg_an = (p->last_reg_10 & 0x7f) << 1;

         if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] reg_bn:0x%02x reg_an:0x%02x\n",reg_bn,reg_an);
        }

        // FNUM LSB/MSB 書き込み
        emit_opl3_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts, s, p);
        emit_opl3_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts, s, p);

        p->key_state = keybit;
        p->has_fnum_high = true;
        return;
    }

    /* --- Instrument/Volume (0x30..0x38) --- */
    if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];
        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;
        
        p->last_reg_30 = val;
        p->voice_id = (val >> 4) & 0x0F;
        p->tl = val & 0x0F;

        // 音色変化もflush対象（vgm-convでは_updateVoice）
        opll2opl3_debug_log("HANDLE", "Instrument/Volume", s, ch, p, p_opts);
        // p->is_pending = true;
        // flush_channel_ym2413_to_opl(p_vgmctx, p_opts, s, ch);
        OPL3VoiceParam vp;
        if (p->voice_id == 0) {
            opll_load_voice(0, p_vgmctx->ym2413_user_patch, &vp, p_opts);
        } else {
            opll_load_voice(p->voice_id, NULL, &vp, p_opts);
        }
        apply_opl3_voiceparam(p_vgmctx, ch, &vp, p_opts);

        return;
    }

    /* --- その他（LFO, rhythmなど） --- */
    //if (reg == 0x0E) {
    //    // rhythm/AM/FM/LFO制御など
    //    s->lfo_reg = val;
    //    opll2opl3_debug_log("HANDLE", "LFO/Rhythm", s, -1, NULL, p_opts);
    //    return;
    //}

    // その他: 無処理
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
// 
    // if (to_emit > 0) {
    //     if (p_opts->debug.verbose) {
    //     fprintf(stderr,
    //     "\n[OPLL2OPL3][SCHEDULE][TO EMIT] virtual_time:%llu emit_time:%llu --- to_emit:%d\n",
    //     (unsigned long long)g_scheduler.virtual_time,
    //     (unsigned long long)g_scheduler.emit_time,to_emit
    //     );
    // }
    //     emit_wait(p_vgmctx, to_emit, s, p_opts);
    // }
    emit_wait(p_vgmctx, wait_samples, s, p_opts);
}

/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll2opl_command_handler (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, uint16_t wait_samples, const CommandOptions *p_opts)
 {
    // Update timestamp
    g_scheduler.virtual_time = p_vgmctx->timestamp.current_sample;
    
    if (p_opts->debug.verbose) {
        fprintf(stderr,
        "\n[OPLL2OPL3][HANDLER][%s] virtual_time:%llu emit_time:%llu --- reg:0x%02x val:0x%02x Sample:%d\n",
        (p_vgmctx->cmd_type == VGMCommandType_RegWrite) ? "RegWrite" : "Wait",
        (unsigned long long)g_scheduler.virtual_time,
        (unsigned long long)g_scheduler.emit_time,reg,val,wait_samples
        );
    }

    if (p_vgmctx->cmd_type == VGMCommandType_RegWrite) {
        p_vgmctx->opll_state.reg_stamp[reg] = p_vgmctx->opll_state.reg[reg];
        p_vgmctx->opll_state.reg[reg] = val;
        //handle_opll_write(p_vgmctx, reg, val, p_opts, &g_scheduler );
        handle_opll_write_ym2413_to_opl(p_vgmctx, reg, val, p_opts, &g_scheduler );
    } else if (p_vgmctx->cmd_type == VGMCommandType_Wait) {
        //fprintf(stderr,"[OPLL2OPL3] opll2opl_command_handler: wait_samples=%d\n",wait_samples);
        g_scheduler.virtual_time += wait_samples;
        schedule_wait(p_vgmctx, wait_samples, p_opts, &g_scheduler);
    } else {
        // Should not be occured here
    }
}
