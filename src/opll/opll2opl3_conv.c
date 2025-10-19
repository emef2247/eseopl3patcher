#include <math.h>
#include <stdio.h>
#include "../opl3/opl3_convert.h"
#include "../opll/opll_state.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include "../opll/ym2413_voice_roms.h"
#include "../opll/nukedopll_voice_rom.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>  // getenv
#include <stdarg.h>
#include "opll2opl3_conv.h"

#define YM2413_REGS_SIZE 0x40

// Virtual channel IDs for rhythm part
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

static inline void opll2opl3_debug_log(VGMContext *p_vgmctx, char *tag1,const char *tag2, int ch, const CommandOptions *p_opts) {
    if (p_opts->debug.verbose) {
        OPLL2OPL3_Scheduler      *s = &(p_vgmctx->opll_state.sch);
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
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

void opll2opl3_init_scheduler(VGMContext *p_vgmctx, const CommandOptions *p_opts) {
    OPLL2OPL3_Scheduler *s = &(p_vgmctx->opll_state.sch);
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

/** Calculate OPLL frequency for debugging */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    // YM2413 (OPLL) frequency calculation based on observed behavior:
    // f ≈ (clock / 72) / 2^18 * fnum * 2^block
    // Example: clock=3579545, block=2, fnum=500 -> approximately 379.3 Hz
    const double base = (clock / 72.0) / 262144.0; // 2^18
    return base * (double)fnum * ldexp(1.0, block);
}

/* Convert OPLL fnum/block to OPL3 fnum/block (10-bit fnum, 3bit block)
   returns true if conversion within range */
bool convert_fnum_block_from_opll_to_opl3(double opll_clock, double opl3_clock, uint8_t opll_block, uint16_t opll_fnum, uint16_t *best_f, uint8_t *best_b)
{

    double freq = calc_opll_frequency(opll_clock, opll_block, opll_fnum);
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
static void opll2opl3_freq_mapping(uint8_t reg1n, uint8_t reg2n, double src_clock, double dst_clock, uint8_t *p_best_f, uint8_t *p_best_block)
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


int  opll2opl3_emit_reg_write(VGMContext *p_vgmctx, uint8_t addr, uint8_t val, const CommandOptions *p_opts) 
{
    // Emit actual OPL3 write (handles dual port if needed)
    OPLL2OPL3_Scheduler *s = &(p_vgmctx->opll_state.sch);
    uint8_t last_val =  s->last_emitted_reg_val[addr];
    bool first_access = !s->accessed[addr];
    int wrote_bytes = 0;

    if (p_opts->debug.verbose) {
        fprintf(stderr, "[EMIT][Reg Write] time=%u addr=%02X val=%02X emit_time=%u\n", (unsigned)s->virtual_time, addr, val, (unsigned)s->emit_time);
    }
   if (first_access || val != last_val) {
        int bytes = duplicate_write_opl3(p_vgmctx, addr, val, p_opts);
        if (should_account_addtional_bytes_pre_loop(&(p_vgmctx->status))) wrote_bytes += bytes;

        s->accessed[addr] = true;
    }
    s->last_emitted_reg_val[addr] = val;

    return wrote_bytes;
}

int emit_wait(VGMContext *p_vgmctx, uint16_t samples, OPLL2OPL3_Scheduler *s,const CommandOptions *p_opts) {
    int wrote_bytes = 0;
    if (samples == 0) {
        if (p_opts->debug.verbose) {
            fprintf(stderr,"[EMIT][WAIT] emit_time was ignored by samples = %u\n", samples);
        }
        return wrote_bytes;
    }

    wrote_bytes += vgm_wait_samples(p_vgmctx, samples);
    // Advance emitted timeline
    s->emit_time += samples;

    if (p_opts->debug.verbose) {
        fprintf(stderr,"[EMIT][WAIT] emit_time advanced by %u -> %u\n", samples, (unsigned)s->emit_time);
    }
    return wrote_bytes;
}

/** Returns true if register is OPLL FNUM (pitch lower 8bit) */
static inline bool is_opll_fnum_reg(uint8_t reg)
{
    return (reg >= 0x10 && reg <= 0x18);
}

/** Returns true if register is KeyOn */
static inline bool is_opll_keyon_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) != 0;  // bit4=KeyOn
    return false;
}

/** Returns true if register is KeyOff */
static inline bool is_opll_keyoff_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) == 0;  // bit4=0ならKeyOff
    return false;
}

/** Returns true if register is TL (Total Level/Volume) */
static inline bool is_opll_tl_reg(uint8_t reg)
{
    return (reg >= 0x30 && reg <= 0x38);
}

/** Returns true if register is voice/instrument */
static inline bool is_opll_voice_reg(uint8_t reg)
{
    // User patch definition
    if (reg <= 0x07)
        return true;
    // Channel-specific instrument setting
    if (reg >= 0x30 && reg <= 0x38)
        return true;
    return false;
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

/** Sustain Level (SL) passthrough (0-15) */
static inline uint8_t opll2opl_sl(uint8_t sl) { return sl & 0x0F; }

/** Compute unique hash for OPLL voice parameters */
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

static inline int get_mod_offset(int ch) {
    static const int tbl[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    return tbl[ch];
}

static inline uint8_t ym2413_to_opl3_tl(uint8_t inst_tl, uint8_t volume) {
    int tl = (inst_tl << 2) + (volume << 2);
    if (tl > 63) tl = 63;
    return (uint8_t)tl;
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

/** Write to OPL3 register mirror and VGM output */
static inline void write_opl3_reg(VGMContext *p_vgmctx, uint8_t addr, uint8_t data) {
    forward_write(p_vgmctx, 0, addr, data);
    p_vgmctx->opll_state.reg[addr] = data;
}

static inline int toTL(int vol, int off) {
    int t = (vol << 2) - off;
    return (t > 0) ? t : 0;
}

void opll_load_voice(VGMContext *p_vgmctx, int inst, int ch, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp) return;
    memset(p_vp, 0, sizeof(*p_vp));

    uint8_t user_patch[8];
    const unsigned char (*source_preset)[8] = YM2413_VOICES; // Default

    // Select the preset table
    switch (p_opts->preset) {
        case OPLL_PresetType_YM2413:
            source_preset = YM2413_VOICES;
            break;
        case OPLL_PresetType_VRC7:
            source_preset = VRC7_VOICES;
            break;
        case OPLL_PresetType_YMF281B:
            source_preset = YMF281B_VOICES;
            break;
        default:
            source_preset = YM2413_VOICES;
            break;
    }

    // Select patch
    const unsigned char *src = NULL;
    if (inst == 0) {
        // User patch (from registers)
        for (int i = 0; i < 8; ++i)
            user_patch[i] = p_vgmctx->opll_state.reg[i];
        src = user_patch;
    } else if (inst >= 1 && inst <= 19) {
        src = source_preset[inst - 1]; // [1]..[19] are preset patches
    } else if (source_preset >= 20) {
        src = source_preset[19]; // Fallback: last preset
    } else {
        src = source_preset[0];  // Fallback: first preset
    }

    // Defensive: if src is NULL, abort
    if (!src) {
        fprintf(stderr, "[ERROR] src is NULL in opll_load_voice (inst=%d)\n", inst);
        return;
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
    uint8_t m_ws   = (src[3] >> 3) & 0x01;

    // --- Carrier ---
    uint8_t c_am   = (src[1] >> 7) & 1;
    uint8_t c_vib  = (src[1] >> 6) & 1;
    uint8_t c_egt  = (src[1] >> 5) & 1;
    uint8_t c_ksr  = (src[1] >> 4) & 1;
    uint8_t c_mult = src[1] & 0x0F;
    uint8_t c_kl   = (src[3] >> 6) & 0x03;
    uint8_t c_tl   = src[3] & 0x3F;
    uint8_t c_ar   = (src[5] >> 4) & 0x0F;
    uint8_t c_dr   = src[5] & 0x0F;
    uint8_t c_sl   = (src[7] >> 4) & 0x0F;
    uint8_t c_rr   = src[7] & 0x0F;
    uint8_t c_ws   = (src[3] >> 4) & 0x01;

    uint8_t fb = src[3] & 0x07;

    // Modulator
    p_vp->op[0].am   = m_am;
    p_vp->op[0].vib  = m_vib;
    p_vp->op[0].egt  = m_egt;
    p_vp->op[0].ksr  = m_ksr;
    p_vp->op[0].mult = m_mult;
    p_vp->op[0].ksl  = m_kl;
    p_vp->op[0].tl   = m_tl;
    p_vp->op[0].ar   = m_ar;
    p_vp->op[0].dr   = m_dr;
    p_vp->op[0].sl   = m_sl;
    p_vp->op[0].rr   = m_rr;
    p_vp->op[0].ws   = m_ws;

    // Carrier
    p_vp->op[1].am   = c_am;
    p_vp->op[1].vib  = c_vib;
    p_vp->op[1].egt  = c_egt;
    p_vp->op[1].ksr  = c_ksr;
    p_vp->op[1].mult = c_mult;
    p_vp->op[1].ksl  = c_kl;
    p_vp->op[1].tl   = c_tl;
    p_vp->op[1].ar   = c_ar;
    p_vp->op[1].dr   = c_dr;
    p_vp->op[1].sl   = c_sl;
    p_vp->op[1].rr   = c_rr;
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
int opll2opl3_apply_voice(VGMContext *p_vgmctx, int ch, int mod_volume, int car_volume, bool key, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp || ch < 0 || ch >= 9) return 0;
    int wrote_bytes = 0;
    int slot_mod = opl3_opreg_addr(0, ch, 0);
    int slot_car = opl3_opreg_addr(0, ch, 1);
    uint8_t tl = p_vgmctx->opll_state.reg[0x30 + ch] & 0xF;

    // AM/VIB/EGT/KSR/MULT
    uint8_t opl3_2n_mod = (uint8_t)((p_vp->op[0].am << 7) | (p_vp->op[0].vib << 6) | (p_vp->op[0].egt << 5) | (p_vp->op[0].ksr << 4) | (p_vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((p_vp->op[1].am << 7) | (p_vp->op[1].vib << 6) | (p_vp->op[1].egt << 5) | (p_vp->op[1].ksr << 4) | (p_vp->op[1].mult & 0x0F));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + slot_car, opl3_2n_car, p_opts);

    // KSL/TL 
    uint8_t opl3_4n_mod = (uint8_t)((opll2opl_ksl[(p_vp->op[0].ksl & 0x03)] << 6) | ((mod_volume >= 0) ? mod_volume : (p_vp->op[0].tl & 0x3F)));
    uint8_t opl3_4n_car = (uint8_t)((opll2opl_ksl[(p_vp->op[1].ksl & 0x03)] << 6) | ((car_volume >= 0) ? car_volume : (p_vp->op[1].tl & 0x3F)));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + slot_mod, opl3_4n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + slot_car, opl3_4n_car, p_opts);

    // AR/DR
    uint8_t opl3_6n_mod = (uint8_t)((p_vp->op[0].ar << 4) | (p_vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((p_vp->op[1].ar << 4) | (p_vp->op[1].dr & 0x0F));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + slot_mod, opl3_6n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + slot_car, opl3_6n_car, p_opts);

    // SL/RR
    uint8_t opl3_8n_mod = (uint8_t)((p_vp->op[0].sl << 4) | ((!p_vp->op[0].egt) ? (p_vp->op[0].rr & 0x0F) : 0));
    uint8_t opl3_8n_car = (uint8_t)((p_vp->op[1].sl << 4) | ((p_vp->op[1].egt || key) ? (p_vp->op[1].rr & 0x0F) : 6));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + slot_mod, opl3_8n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + slot_car, opl3_8n_car, p_opts);

    // FB/CNT
    uint8_t c0_val = (uint8_t)(0xC0 | ((p_vp->fb[0] & 0x07) << 1) | (p_vp->cnt[0] & 0x01));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + ch, c0_val, p_opts);

    // WS
    uint8_t opl3_en_mod = (uint8_t)((p_vp->op[0].ws)? 1 : 0);
    uint8_t opl3_en_car = (uint8_t)((p_vp->op[1].ws)? 1 : 0);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + slot_mod, opl3_en_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + slot_car, opl3_en_car, p_opts);

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
    return wrote_bytes;
}

/**
 * Update OPL3 voice for the specified channel.
 * This function loads the voice and applies it to the OPL3 channel.
 */
int opll2opl3_update_voice(VGMContext *p_vgmctx, int ch, const CommandOptions *p_opts)
{
    OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
    uint8_t *regs = p_vgmctx->opll_state.reg;
    bool rflag = (p_vgmctx->opll_state.is_rhythm_mode != 0);
    uint8_t d = regs[0x30 + ch];
    int inst = (d & 0xF0) >> 4;
    int volume = d & 0x0F;
    bool key = (regs[0x20 + ch] & 0x10) ? true : false;
    int wrote_bytes = 0;
    
    OPLL2OPL3_PendingChannel *p = &p_s->ch[ch];
    OPL3VoiceParam vp;

    if (rflag && ch >= 6) {
        // Rhythm mode: use rhythm voice presets
        switch (ch) {
            case 6:
                opll_load_voice(p_vgmctx, 16, ch, &vp, p_opts); // BD
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, -1 ,toTL(volume, 0), key,&vp, p_opts);
                break;
            case 7:
                opll_load_voice(p_vgmctx, 17, ch, &vp, p_opts); // SD/TOM
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
                break;
            case 8:
                opll_load_voice(p_vgmctx, 18, ch, &vp, p_opts); // CYM/HH
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
                break;
        }
    } else if (!rflag && ch >= 6) {
        // Rhythm mode off: restore normal voice for ch6/7/8
        int inst_normal = (regs[0x30 + ch] >> 4) & 0x0F;
        if (inst_normal == 0) {
            // User patch
            opll_load_voice(p_vgmctx, 0, ch, &vp, p_opts);
            wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
        } else {
            opll_load_voice(p_vgmctx, inst_normal, ch, &vp, p_opts);
            wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
        }
    } else {
        // Normal melodic channel
        int inst_normal = (regs[0x30 + ch] >> 4) & 0x0F;
        if (inst_normal == 0) {
            opll_load_voice(p_vgmctx, 0, ch, &vp, p_opts);
        } else {
            opll_load_voice(p_vgmctx, inst_normal, ch, &vp, p_opts);
        }
        wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, -1, toTL(volume, 0), key,&vp, p_opts);
    }

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] ch=%d inst=%d vol=%d key=%d rhythm=%d\n",
            ch, inst, volume, key, rflag);
    }
    return wrote_bytes;
}

int opll2opl3_handle_opll_command (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, const CommandOptions *p_opts) 
{
    int wrote_bytes = 0;
    if (reg == 0x0e) {
        OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
        int lfoDepth = OPLL_LFO_DEPTH; 

        bool prev_rhythm_mode = p_vgmctx->opll_state.is_rhythm_mode;
        bool now_rhythm_mode  = (val & 0x20) != 0;

        int mod_slots[3] = { opl3_opreg_addr(0, 6, 0), opl3_opreg_addr(0, 7, 0), opl3_opreg_addr(0, 8, 0) };
        int car_slots[3] = { opl3_opreg_addr(0, 6, 1), opl3_opreg_addr(0, 7, 1), opl3_opreg_addr(0, 8, 1) };

        if (now_rhythm_mode && !prev_rhythm_mode) {
            // Rhythm mode enabled （posedge）
            if (p_opts->is_voice_zero_clear) {
                for (int i = 0; i < 3; ++i) {
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + (6 + i), 0xF0, p_opts);
                }
            }
            p_vgmctx->opll_state.is_rhythm_mode = true;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts); // BD
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts); // SD/TOM
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts); // CYM/HH
        } else if (!now_rhythm_mode && prev_rhythm_mode) {
            // Rhythm mode disabled（negedge）
            if (p_opts->is_voice_zero_clear) {
                for (int i = 0; i < 3; ++i) {
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + mod_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + car_slots[i], 0x00, p_opts);
                    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + (6 + i), 0xF0, p_opts);
                }
            }
            p_vgmctx->opll_state.is_rhythm_mode = false;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts);
            // 1. Write LFO Depth = 0 once（0xc0 | (val & 0x3f)）
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, 0xc0 | (val & 0x3f), p_opts);
        } else {
                // Only update the flag (no edge)
                p_vgmctx->opll_state.is_rhythm_mode = (val & 0x20) ? true : false;
        }
         // 2. Write LFO Depth at least once for all cases
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, (lfoDepth << 6) | (val & 0x3f), p_opts);
        return wrote_bytes;
    }
        /* --- FNUM Low (0x10..0x18) --- */
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        p->fnum_comb = (uint16_t)((p->fnum_comb & 0x300) | val);
        p->has_fnum_low = true;
        p->last_reg_10 = val;

        opll2opl3_debug_log(p_vgmctx, "HANDLE", "FNUM Low",ch, p_opts);
        bool keybit   = (val & 0x80) != 0;
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3 Before conversion: block=%u, fnum=0x%03X (dec %u) src_clock=%.1f dst_clock=%.1f\n",
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
                "[DEBUG] OPLL→OPL3 After conversion: dst_block=%u, dst_fnum=0x%03X (dec %u)\n",
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
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts);
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts);
        return wrote_bytes;
    }

    /* --- FNUM High + Block + Key (0x20..0x28) --- */
    if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);

        uint8_t fhi   = val & 0x03;
        uint8_t block = (val >> 2) & 0x07;
        bool keybit   = (val & 0x10) != 0;
        bool prev_key = p->key_state;

        // FNUM/Block/Key update
        p->fnum_high = fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->last_reg_10 & 0xFF));
        p->block = block;
        p->last_reg_20 = val;

        if (p_opts->is_voice_zero_clear) {
            // 0x20～0x28書き込み時は毎回ゼロクリア＋voice apply（vgm-conv互換）
            int mod_slot = opl3_opreg_addr(0, ch, 0);
            int car_slot = opl3_opreg_addr(0, ch, 1);

            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + mod_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + mod_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + mod_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + mod_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + mod_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + ch, 0xF0, p_opts);
        }

        wrote_bytes += opll2opl3_update_voice(p_vgmctx, ch, p_opts);

        // KeyOn/KeyOff含むA0/B0レジスタ書き込み（OPLL的なNoteOn/Off反映）
        uint8_t reg_bn = ((val  & 0x1F) << 1) | ((p_vgmctx->opll_state.reg[0x10 + ch] & 0x80) >> 7);
        uint8_t reg_an = (p->last_reg_10 & 0x7f) << 1;
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts);
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts);
        p->key_state = keybit;
        p->has_fnum_high = true;
        return wrote_bytes;
    }

    /* --- Instrument/Volume (0x30..0x38) --- */
    // Instrument/Volume (0x30..0x38)
    if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;

        p->last_reg_30 = val;
        p->voice_id = (val >> 4) & 0x0F;
        p->tl = val & 0x0F;

        if (p_opts->is_voice_zero_clear) {
            // --- ここからゼロクリア追加 ---
            // operator slot計算(2op)
            int mod_slot = opl3_opreg_addr(0, ch, 0);
            int car_slot = opl3_opreg_addr(0, ch, 1);

            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + mod_slot, 0x00, p_opts); // AM/VIB/EGT/KSR/MULT
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + mod_slot, 0x00, p_opts); // KSL/TL
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + mod_slot, 0x00, p_opts); // AR/DR
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + mod_slot, 0x00, p_opts); // SL/RR
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + mod_slot, 0x00, p_opts); // WS
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + car_slot, 0x00, p_opts);
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + ch, 0xF0, p_opts); // Feedback/Algo（FM）
        }

        opll2opl3_debug_log(p_vgmctx, "HANDLE", "Instrument/Volume", ch, p_opts);
        wrote_bytes += opll2opl3_update_voice(p_vgmctx, ch, p_opts);

        return wrote_bytes;
    }
    return wrote_bytes;
}

int opll2opl3_schedule_wait(VGMContext *p_vgmctx, uint32_t wait_samples, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    int wrote_bytes = 0;
    wrote_bytes += emit_wait(p_vgmctx, wait_samples, s, p_opts);
    return wrote_bytes;
}

/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll2opl3_command_handler (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, uint16_t wait_samples, const CommandOptions *p_opts)
 {
    int wrote_bytes = 0;
    // Update timestamp
    p_vgmctx->opll_state.sch.virtual_time = p_vgmctx->timestamp.current_sample;
    
    if (p_opts->debug.verbose) {
        fprintf(stderr,
        "\n[OPLL2OPL3][HANDLER][%s] virtual_time:%llu emit_time:%llu --- reg:0x%02x val:0x%02x Sample:%d\n",
        (p_vgmctx->cmd_type == VGMCommandType_RegWrite) ? "RegWrite" : "Wait",
        (unsigned long long)p_vgmctx->opll_state.sch.virtual_time,
        (unsigned long long)p_vgmctx->opll_state.sch.emit_time,reg,val,wait_samples
        );
    }

    if (p_vgmctx->cmd_type == VGMCommandType_RegWrite) {
        p_vgmctx->opll_state.reg_stamp[reg] = p_vgmctx->opll_state.reg[reg];
        p_vgmctx->opll_state.reg[reg] = val;
        //handle_opll_write(p_vgmctx, reg, val, p_opts, &g_scheduler );
        wrote_bytes += opll2opl3_handle_opll_command(p_vgmctx, reg, val, p_opts);
    } else if (p_vgmctx->cmd_type == VGMCommandType_Wait) {
        //fprintf(stderr,"[OPLL2OPL3] opll2opl3_command_handler: wait_samples=%d\n",wait_samples);
        p_vgmctx->opll_state.sch.virtual_time += wait_samples;
        wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, wait_samples, p_opts, &(p_vgmctx->opll_state.sch));
    } else {
        // Should not be occured here
    }

    return wrote_bytes;
}
