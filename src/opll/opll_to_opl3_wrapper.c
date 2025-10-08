#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_voice.h"
#include "../opl3/opl3_convert.h"
#include "../opl3/opl3_voice_registry.h"
#include "../opl3/opl3_hooks.h"
#include "../opl3/opl3_metrics.h"
#include "ym2413_patch_convert.h"
#include "opll_to_opl3_wrapper.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "../override_apply.h"
#include "../override_loader.h"
#include "../compat_bool.h"
#include "../compat_string.h"

static VGMContext *p_last_ctx = NULL;
static int g_triple_force_retrigger = 0;

// Global variable for fast-path mode
static int g_freqmap_fast = 0;

/**
 * Calculate OPLL frequency for debugging.
 */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    // YM2413 (OPLL) frequency calculation:
    // f ≈ (clock / 72) / 2^18 * fnum * 2^block
    const double base = (clock / 72.0) / 262144.0;
    return base * (double)fnum * ldexp(1.0, block);
}

/**
 * OPLL to OPL3 frequency mapping with optional fast-path.
 * Fast-path heuristics:
 * - OPLL→OPL3 (dst≈4×src): passthrough (FNUM/BLOCK unchanged)
 * - OPLL→OPL2/Y8950 (dst≈src): FNUM×2, BLOCK unchanged, clamp to 1023
 * - Otherwise: precise mapping using opl3_find_fnum_block_with_ml_cents
 */
static void map_opll_to_opl3_freq(uint8_t reg1n, uint8_t reg2n,
                                  double src_clock, double dst_clock,
                                  uint8_t *p_out_A, uint8_t *p_out_B_noKO)
{
    uint8_t  opll_block = (uint8_t)((reg2n >> 1) & 0x07);
    uint16_t opll_fnum9 = (uint16_t)(((reg2n & 0x01) << 8) | reg1n);

    // Skip if fnum==0 (prevents 0Hz KeyOn)
    if (opll_fnum9 == 0) {
        if (p_out_A)      *p_out_A      = reg1n;
        if (p_out_B_noKO) {
            uint8_t fnum_msb_2b = (reg2n & 0x01);
            uint8_t block_3b    = (reg2n >> 1) & 0x07;
            *p_out_B_noKO = (uint8_t)(fnum_msb_2b | (block_3b << 2));
        }
        if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
            fprintf(stderr, "[FREQMAP] (skip: fnum==0) OPLL blk=%u fnum=%u\n",
                    opll_block, opll_fnum9);
        }
        return;
    }

    // Fast-path heuristics if enabled
    if (g_freqmap_fast) {
        double clock_ratio = dst_clock / src_clock;
        if (clock_ratio >= 3.8 && clock_ratio <= 4.2) {
            if (p_out_A)      *p_out_A      = reg1n;
            if (p_out_B_noKO) *p_out_B_noKO = (uint8_t)((reg2n & 0x01) | (opll_block << 2));
            if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
                fprintf(stderr, "[FREQMAP_FAST] passthrough OPLL->OPL3 (dst≈4×src) blk=%u fnum=%u ratio=%.2f\n",
                        opll_block, opll_fnum9, clock_ratio);
            }
            return;
        }
        if (clock_ratio >= 0.8 && clock_ratio <= 1.2) {
            uint16_t doubled_fnum = opll_fnum9 * 2;
            if (doubled_fnum > 1023) doubled_fnum = 1023;
            if (p_out_A)      *p_out_A      = (uint8_t)(doubled_fnum & 0xFF);
            if (p_out_B_noKO) *p_out_B_noKO = (uint8_t)(((doubled_fnum >> 8) & 0x03) | (opll_block << 2));
            if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
                fprintf(stderr, "[FREQMAP_FAST] x2-fnum OPLL->OPL2/Y8950 (dst≈src) blk=%u fnum=%u->%u ratio=%.2f\n",
                        opll_block, opll_fnum9, doubled_fnum, clock_ratio);
            }
            return;
        }
        if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
            fprintf(stderr, "[FREQMAP_FAST] fallback to precise (ratio=%.2f not matched)\n", clock_ratio);
        }
    }

    // Precise mapping (default behavior)
    double freq = calc_opll_frequency(src_clock, opll_block, opll_fnum9);
    unsigned char best_b = 0;
    unsigned short best_f = 0;
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_ml_cents(freq, dst_clock, &best_b, &best_f, &best_err_cents,
                                       (int)opll_block, 0.0, 0.0);

    if (p_out_A)      *p_out_A      = (uint8_t)(best_f & 0xFF);
    if (p_out_B_noKO) *p_out_B_noKO = (uint8_t)(((best_f >> 8) & 0x03) | (best_b << 2));

    if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
        fprintf(stderr,
            "[FREQMAP] OPLL blk=%u fnum=%u Hz=%.6f -> OPL3 blk=%u fnum=%u (err_cents=%.2f)\n",
            opll_block, opll_fnum9, freq, (unsigned)best_b, (unsigned)best_f, best_err_cents);
    }
}

// ---------- Global OPLL state ----------
#define YM2413_NUM_CH 9
#define YM2413_REGS_SIZE 0x40

uint8_t       g_ym2413_regs[YM2413_REGS_SIZE] = {0};
OpllPendingCh g_pend[YM2413_NUM_CH] = {0};
OpllStampCh   g_stamp[YM2413_NUM_CH] = {0};
static uint16_t g_pending_on_elapsed[YM2413_NUM_CH] = {0};

// ---------- Program argument retention ----------
static int    g_saved_argc = 0;
static char **g_saved_argv = NULL;

// Flag to wait for fresh 1n
static uint8_t g_need_fresh_1n[YM2413_NUM_CH] = {0};

// Timeout constants
#ifndef KEYON_WAIT_FOR_FNUM_TIMEOUT_SAMPLES
#define KEYON_WAIT_FOR_FNUM_TIMEOUT_SAMPLES 128
#endif

// Minimum gate hold variables
static uint16_t g_gate_elapsed[YM2413_NUM_CH] = {0};
static uint8_t  g_has_pending_keyoff[YM2413_NUM_CH] = {0};
static uint8_t  g_pending_keyoff_val2n[YM2413_NUM_CH] = {0};

// Minimum gate duration (minimum interval between KeyOn→KeyOff)
#ifndef OPLL_MIN_GATE_SAMPLES
#define OPLL_MIN_GATE_SAMPLES 0
#endif

static int g_freqmap_opllblock = 0;

/**
 * Store program arguments for later use.
 */
void opll_set_program_args(int argc, char **argv) {
    g_saved_argc = argc;
    g_saved_argv = argv;
}

/**
 * Check if instrument is ready.
 */
static inline bool have_inst_ready_policy(const OpllPendingCh* p_pend, const OpllStampCh* p_stamp) {
    return (p_pend && p_pend->has_3n) || (p_stamp && p_stamp->valid_3n);
}

/**
 * Check if fnum is ready.
 */
static inline bool have_fnum_ready_policy(int ch, const OpllPendingCh* p_pend, const OpllStampCh* p_stamp) {
    (void)ch;
    if (g_need_fresh_1n[ch]) {
        return (p_pend && p_pend->has_1n);
    }
    return (p_pend && p_pend->has_1n) || (p_stamp && p_stamp->valid_1n);
}

/**
 * Initialize OPLL/OPL3 voice DB and state.
 */
void opll_init(OPL3State *p_state, const CommandOptions* p_opts) {
    if (!p_state) return;

    const char *fm = getenv("ESEOPL3_FREQMAP");
    if (fm && (strcasecmp(fm, "opllblock") == 0 || strcmp(fm, "1") == 0 || strcasecmp(fm, "true") == 0 || strcasecmp(fm, "on") == 0 || strcasecmp(fm, "yes") == 0)) {
        g_freqmap_opllblock = 1;
    } else {
        g_freqmap_opllblock = 0;
    }

    const char *fast = getenv("ESEOPL3_FREQMAP_FAST");
    g_freqmap_fast = (fast && (fast[0]=='1' || fast[0]=='y' || fast[0]=='Y' || fast[0]=='t' || fast[0]=='T')) ? 1 : 0;

    fprintf(stderr, "[FREQMAP] init mode=%s fast=%d (ESEOPL3_FREQMAP=%s ESEOPL3_FREQMAP_FAST=%s)\n",
            g_freqmap_opllblock ? "opllblock" : "off",
            g_freqmap_fast,
            fm ? fm : "(unset)",
            fast ? fast : "(unset)");

    const char *tr = getenv("ESEOPL3_TRIPLE_FORCE_RETRIGGER");
    g_triple_force_retrigger =
        (tr && (tr[0]=='1'||tr[0]=='y'||tr[0]=='Y'||tr[0]=='t'||tr[0]=='T')) ? 1 : 0;
    fprintf(stderr, "[TRIPLE] force_retrigger=%d (ESEOPL3_TRIPLE_FORCE_RETRIGGER=%s)\n",
            g_triple_force_retrigger, tr ? tr : "(unset)");

    opl3_register_all_ym2413(&p_state->voice_db, p_opts);
    memset(g_ym2413_regs, 0, sizeof(g_ym2413_regs));
    for (int i = 0; i < YM2413_NUM_CH; ++i) {
        opll_pending_clear(&g_pend[i]);
        stamp_clear(&g_stamp[i]);
        g_pending_on_elapsed[i] = 0;
        g_need_fresh_1n[i] = 0;
        g_gate_elapsed[i] = 0;
        g_has_pending_keyoff[i] = 0;
        g_pending_keyoff_val2n[i] = 0;
    }
    for (int ch = 0; ch < 9; ++ch) {
        p_state->last_key[ch] = 0;
    }
}

// ---------- Macro definitions ----------
#ifndef KEYON_WAIT_GRACE_SAMPLES
#define KEYON_WAIT_GRACE_SAMPLES 16
#endif
#ifndef KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES
#define KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES 512
#endif
#ifndef OPLL_ENABLE_RATE_MAP
#define OPLL_ENABLE_RATE_MAP 1
#endif
#ifndef OPLL_ENABLE_MOD_TL_ADJ
#define OPLL_ENABLE_MOD_TL_ADJ 0
#endif
#ifndef OPLL_ENABLE_KEYON_DEBUG
#define OPLL_ENABLE_KEYON_DEBUG 1
#endif
#ifndef OPLL_ENABLE_ARDR_MIN_CLAMP
#define OPLL_ENABLE_ARDR_MIN_CLAMP 0
#endif
#ifndef OPLL_RATE_MAP_MODE
#define OPLL_RATE_MAP_MODE 1
#endif
#ifndef OPLL_FORCE_MIN_ATTACK_RATE
#define OPLL_FORCE_MIN_ATTACK_RATE 2
#endif
#ifndef OPLL_DEBUG_ENFORCE_MIN_AR
#define OPLL_DEBUG_ENFORCE_MIN_AR 1
#endif
#ifndef OPLL_ENABLE_ENVELOPE_SHAPE_FIX
#define OPLL_ENABLE_ENVELOPE_SHAPE_FIX 0
#endif
#ifndef OPLL_SHAPE_FIX_DR_GAP_THRESHOLD
#define OPLL_SHAPE_FIX_DR_GAP_THRESHOLD 11
#endif
#ifndef OPLL_SHAPE_FIX_DR_MAX_AFTER
#define OPLL_SHAPE_FIX_DR_MAX_AFTER 12
#endif
#ifndef OPLL_DEBUG_SHAPE_FIX
#define OPLL_DEBUG_SHAPE_FIX 1
#endif
#ifndef OPLL_SHAPE_FIX_USE_STRICT_GT
#define OPLL_SHAPE_FIX_USE_STRICT_GT 0
#endif
#ifndef OPLL_DEBUG_RATE_PICK
#define OPLL_DEBUG_RATE_PICK 1
#endif
#ifndef OPLL_DEBUG_FORCE_CAR_TL_ZERO
#define OPLL_DEBUG_FORCE_CAR_TL_ZERO 0
#endif
#ifndef OPLL_ENABLE_INJECT_WAIT_ON_KEYOFF
#define OPLL_ENABLE_INJECT_WAIT_ON_KEYOFF 1
#endif
#ifndef OPLL_ENABLE_WAIT_BEFORE_KEYON
#define OPLL_ENABLE_WAIT_BEFORE_KEYON 1
#endif
#ifndef OPLL_PRE_KEYON_WAIT_SAMPLES
#define OPLL_PRE_KEYON_WAIT_SAMPLES 1
#endif
#ifndef OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES
#define OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES 0
#endif

// Add this prototype at the top of the file (after includes, before use)
static inline void flush_channel_ch(
    VGMBuffer *p_music_data, VGMStatus *p_vstatus, OPL3State *p_state,
    int ch, const OPL3VoiceParam *p_vp_unused, const CommandOptions *p_opts,
    OpllPendingCh* p_pend, OpllStampCh* p_stamp);
    
static const uint8_t kOPLLRateToOPL3_SIMPLE[16] = {
    0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,15
};
#if 0 /* unused tables (kept for documentation / future switch) */
static const uint8_t kOPLLRateToOPL3_CALIB[16] = {
    1,2,3,4,6,7,8,9,10,11,12,13,14,15,15,15
};
static const uint8_t kOPLLRateToOPL3_ID[16] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const int8_t kModTLAddTable[16] = {
    0,0,4,2,0,2,2,0,0,2,2,3,0,3,3,4
};
#endif

/**
 * Get channel number from register address.
 */
static inline int ch_from_addr(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return addr - 0x10;
    if (addr >= 0x20 && addr <= 0x28) return addr - 0x20;
    if (addr >= 0x30 && addr <= 0x38) return addr - 0x30;
    return -1;
}

/**
 * Get register kind (1n,2n,3n) from address.
 */
static inline int reg_kind(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return 1;
    if (addr >= 0x20 && addr <= 0x28) return 2;
    if (addr >= 0x30 && addr <= 0x38) return 3;
    return 0;
}

/**
 * Get KeyOff value for reg2n (clear KeyOn bit).
 */
static inline uint8_t opll_make_keyoff(uint8_t reg2n) {
    return (uint8_t)(reg2n & (uint8_t)~0x10);
}

/**
 * Set pending values from OPLL write (per channel).
 */
static inline void set_pending_from_opll_write_ch(OpllPendingCh* p_pend, const OpllStampCh* p_stamp, uint8_t addr, uint8_t value) {
    (void)p_stamp;
    switch (reg_kind(addr)) {
        case 1: p_pend->has_1n = 1; p_pend->reg1n = value; break;
        case 2: p_pend->has_2n = 1; p_pend->reg2n = value; break;
        case 3: p_pend->has_3n = 1; p_pend->reg3n = value; break;
        default: break;
    }
}

/**
 * Set pending values from OPLL write (all channels).
 */
static inline void set_pending_from_opll_write(OpllPendingCh p_pend[], const OpllStampCh p_stamp[], uint8_t addr, uint8_t value) {
    int ch = ch_from_addr(addr); if (ch < 0) return;
    set_pending_from_opll_write_ch(&p_pend[ch], &p_stamp[ch], addr, value);
}

/**
 * Analyze pending edge state (note on/off detection).
 */
static inline PendingEdgeInfo analyze_pending_edge_ch(
    const OpllPendingCh* p_pend, const OpllStampCh* p_stamp, const CommandOptions *p_opts)
{
    PendingEdgeInfo info = {0};
    if (!p_pend || !p_stamp || !p_pend->has_2n) return info;
    bool ko_next = (p_pend->reg2n & 0x10) != 0;
    info.has_2n  = 1;
    info.ko_next = ko_next;
    bool prev_ko = p_stamp->ko;

    if (p_opts && p_opts->force_retrigger_each_note) {
        info.note_on_edge = (!prev_ko && ko_next);
    } else {
        info.note_on_edge = (!prev_ko && ko_next);
    }
    info.note_off_edge = (prev_ko && !ko_next);
    return info;
}

/**
 * Check if pending is needed for next write.
 */
static inline bool should_pend(uint8_t addr, uint8_t value, const OpllStampCh* p_stamp_ch, uint16_t next_wait_samples) {
    switch (reg_kind(addr)) {
        case 1: return p_stamp_ch && (p_stamp_ch->ko == 0);
        case 3: return p_stamp_ch && (p_stamp_ch->ko == 0);
        case 2: {
            if (!p_stamp_ch) return 0;
            bool ko_next = (value & 0x10) != 0;
            if (!p_stamp_ch->ko && ko_next)
                return (next_wait_samples <= KEYON_WAIT_GRACE_SAMPLES);
            return 0;
        }
        default: return 0;
    }
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

/**
 * Rate map selection (ID/SIMPLE/CALIB).
 */
static inline uint8_t rate_map_pick(uint8_t raw) {
#if !OPLL_ENABLE_RATE_MAP
    return raw & 0x0F;
#else
 #if OPLL_RATE_MAP_MODE==0
    uint8_t v = kOPLLRateToOPL3_ID[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=ID raw=%u -> %u\n", raw, v);
    #endif
    return v;
 #elif OPLL_RATE_MAP_MODE==1
    uint8_t v = kOPLLRateToOPL3_SIMPLE[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=SIMPLE raw=%u -> %u\n", raw, v);
    #endif
    return v;
 #else
    uint8_t v = kOPLLRateToOPL3_CALIB[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=CALIBv2 raw=%u -> %u\n", raw, v);
    #endif
    return v;
 #endif
#endif
}

/**
 * Enforce minimum attack rate (for envelope stability).
 */
static inline uint8_t enforce_min_attack(uint8_t ar, const char* stage, int inst, int op_index) {
#if OPLL_FORCE_MIN_ATTACK_RATE > 0
    if (ar < OPLL_FORCE_MIN_ATTACK_RATE) {
#if OPLL_DEBUG_ENFORCE_MIN_AR
        printf("[DEBUG] AR-MinClamp inst=%d op=%d %s rawAR=%u -> %u\n",
               inst, op_index, stage, ar, (unsigned)OPLL_FORCE_MIN_ATTACK_RATE);
#endif
        return (uint8_t)OPLL_FORCE_MIN_ATTACK_RATE;
    }
#endif
    return ar;
}

/**
 * Get minimum gate samples (OPLL_MIN_GATE_SAMPLES).
 */
static inline uint16_t get_min_gate(const CommandOptions* p_opts) {
    return (p_opts && p_opts->min_gate_samples) ? p_opts->min_gate_samples : (uint16_t)OPLL_MIN_GATE_SAMPLES;
}

/**
 * Get pre-keyon wait samples (OPLL_PRE_KEYON_WAIT_SAMPLES).
 */
static inline uint16_t get_pre_keyon_wait(const CommandOptions* p_opts) {
    return (p_opts && p_opts->pre_keyon_wait_samples) ? p_opts->pre_keyon_wait_samples : (uint16_t)OPLL_PRE_KEYON_WAIT_SAMPLES;
}

/**
 * Get minimum off-to-on wait samples (OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES).
 */
static inline uint16_t get_min_off_on_wait(const CommandOptions* p_opts) {
    return (p_opts && p_opts->min_off_on_wait_samples) ? p_opts->min_off_on_wait_samples : (uint16_t)OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES;
}
#if !OPLL_ENABLE_ENVELOPE_SHAPE_FIX
/**
 * No-op for envelope shape fix (disabled by macro).
 */
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* p_ar, uint8_t* p_dr) {
    (void)inst; (void)op_index; (void)p_ar; (void)p_dr;
}
#else
/**
 * Apply envelope shape fix for steep AR/DR gaps, if enabled.
 */
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* p_ar, uint8_t* p_dr) {
    if (!p_ar || !p_dr) return;
    uint8_t A = (uint8_t)(*p_ar & 0x0F);
    uint8_t D = (uint8_t)(*p_dr & 0x0F);
    if (D <= A) {
#if OPLL_DEBUG_SHAPE_FIX
        printf("[SHAPEFIX] inst=%d op=%d no-fix (DR<=AR) AR=%u DR=%u\n", inst, op_index, A, D);
#endif
        return;
    }
    uint8_t gap = (uint8_t)(D - A);
#if OPLL_SHAPE_FIX_USE_STRICT_GT
    int cond = (gap > OPLL_SHAPE_FIX_DR_GAP_THRESHOLD);
#else
    int cond = (gap >= OPLL_SHAPE_FIX_DR_GAP_THRESHOLD);
#endif
    if (!cond) {
#if OPLL_DEBUG_SHAPE_FIX
        printf("[SHAPEFIX] inst=%d op=%d no-fix gap=%u th=%d AR=%u DR=%u\n", inst, op_index, gap, OPLL_SHAPE_FIX_DR_GAP_THRESHOLD, A, D);
#endif
        return;
    }
    uint8_t newD = D;
    if (newD > OPLL_SHAPE_FIX_DR_MAX_AFTER)
        newD = (uint8_t)OPLL_SHAPE_FIX_DR_MAX_AFTER;
    if (newD < (uint8_t)(A + 1))
        newD = (uint8_t)(A + 1);
    if (newD != D) {
#if OPLL_DEBUG_SHAPE_FIX
        printf("[SHAPEFIX] inst=%d op=%d AR=%u DR=%u gap=%u -> DR'=%u (th=%d, max=%d)\n",
               inst, op_index, A, D, gap, newD, OPLL_SHAPE_FIX_DR_GAP_THRESHOLD, OPLL_SHAPE_FIX_DR_MAX_AFTER);
#endif
        *p_dr = newD;
    } else {
#if OPLL_DEBUG_SHAPE_FIX
        printf("[SHAPEFIX] inst=%d op=%d gap=%u cond=1 but newD==D (AR=%u DR=%u) (th=%d max=%d)\n",
               inst, op_index, gap, A, D, OPLL_SHAPE_FIX_DR_GAP_THRESHOLD, OPLL_SHAPE_FIX_DR_MAX_AFTER);
#endif
    }
}
#endif

/**
 * Apply OPL3VoiceParam to a channel.
 */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstatus, OPL3State *p_state,
    int ch, const OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    if (!p_vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = opl3_opreg_addr(0, ch, 0);
    int slot_car = opl3_opreg_addr(0, ch, 1);

    printf("[DEBUG] Apply OPL3 VoiceParam to ch=%d\n", ch);

    uint8_t opl3_2n_mod = (uint8_t)((p_vp->op[0].am << 7) | (p_vp->op[0].vib << 6) | (p_vp->op[0].egt << 5) | (p_vp->op[0].ksr << 4) | (p_vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((p_vp->op[1].am << 7) | (p_vp->op[1].vib << 6) | (p_vp->op[1].egt << 5) | (p_vp->op[1].ksr << 4) | (p_vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x20 + slot_car, opl3_2n_car, p_opts);

    uint8_t opl3_4n_mod = (uint8_t)(((p_vp->op[0].ksl & 0x03) << 6) | (p_vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((p_vp->op[1].ksl & 0x03) << 6) | (p_vp->op[1].tl & 0x3F));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x40 + slot_mod, opl3_4n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x40 + slot_car, opl3_4n_car, p_opts);

    uint8_t opl3_6n_mod = (uint8_t)((p_vp->op[0].ar << 4) | (p_vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((p_vp->op[1].ar << 4) | (p_vp->op[1].dr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x60 + slot_mod, opl3_6n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x60 + slot_car, opl3_6n_car, p_opts);

    uint8_t opl3_8n_mod = (uint8_t)((p_vp->op[0].sl << 4) | (p_vp->op[0].rr & 0x0F));
    uint8_t opl3_8n_car = (uint8_t)((p_vp->op[1].sl << 4) | (p_vp->op[1].rr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x80 + slot_mod, opl3_8n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x80 + slot_car, opl3_8n_car, p_opts);

    uint8_t c0_val = (uint8_t)(0xC0 | ((p_vp->fb[0] & 0x07) << 1) | (p_vp->cnt[0] & 0x01));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xC0 + ch, c0_val, p_opts);

    uint8_t opl3_en_mod = (uint8_t)((p_vp->op[0].ws & 0x07));
    uint8_t opl3_en_car = (uint8_t)((p_vp->op[1].ws & 0x07));
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xE0 + slot_mod, opl3_en_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xE0 + slot_car, opl3_en_car, p_opts);

    return bytes;
}

/**
 * Register all YM2413 patches to OPL3 voice DB.
 */
void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *p_db, CommandOptions *p_opts) {
    for (int inst = 1; inst <= 15; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp, p_opts);
        opl3_voice_db_find_or_add(p_db, &vp);
    }
    for (int inst = 16; inst <= 20; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp, p_opts);
        opl3_voice_db_find_or_add(p_db, &vp);
    }
}

/**
 * Apply instrument before KeyOn - batch application assuming "ready to play".
 */
static inline void apply_inst_before_keyon(
    VGMBuffer *p_music_data, VGMStatus *p_vstatus, OPL3State *p_state,
    int ch, uint8_t reg3n, const CommandOptions *p_opts,
    OPL3VoiceParam *p_out_cached_vp)
{
    int8_t inst = (reg3n >> 4) & 0x0F;
    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp, p_opts);

    // Apply all debug/override features as needed
    opll_apply_all_debug(&vp, p_opts);

    // Full register batch write
    opl3_voiceparam_apply(p_music_data, p_vstatus, p_state, ch, &vp, p_opts);

    if (p_out_cached_vp) *p_out_cached_vp = vp;
}

/**
 * Check if channel has effective 3n value.
 */
static inline bool has_effective_3n(const OpllPendingCh* p_pend, const OpllStampCh* p_stamp) {
    return (p_pend && p_pend->has_3n) || (p_stamp && p_stamp->valid_3n);
}

/**
 * Flush channel wrapper (calls flush_channel_ch).
 */
static inline void flush_channel(
    VGMBuffer *p_music_data, VGMStatus *p_vstatus, OPL3State *p_state,
    int ch, const OPL3VoiceParam *p_vp, const CommandOptions *p_opts, OpllPendingCh* p_pend, OpllStampCh* p_stamp)
{
    (void)p_vp;
    flush_channel_ch(p_music_data, p_vstatus, p_state, ch, p_vp, p_opts, p_pend, p_stamp);
}

/**
 * Add fresh 1n timeout wait (fallback to stamp 1n as compromise).
 */
static inline void opll_tick_pending_on_elapsed(
    VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,
    const CommandOptions *p_opts, uint16_t wait_samples)
{
    if (wait_samples == 0) return;

    for (int ch = 0; ch < YM2413_NUM_CH; ++ch) {
        // 1) Update gate elapsed time while playing
        if (g_stamp[ch].ko) {
            uint32_t el = (uint32_t)g_gate_elapsed[ch] + wait_samples;
            g_gate_elapsed[ch] = (el > 0xFFFF) ? 0xFFFF : (uint16_t)el;
        } else {
            g_gate_elapsed[ch] = 0;
        }

        // 2) KeyOn wait (original logic)
        if (g_pend[ch].has_2n) {
            bool ko_next = (g_pend[ch].reg2n & 0x10) != 0;
            if (ko_next && !g_stamp[ch].ko) {
                // Wait for 3n first
                if (!has_effective_3n(&g_pend[ch], &g_stamp[ch])) {
                    uint32_t elapsed = (uint32_t)g_pending_on_elapsed[ch] + wait_samples;
                    if (elapsed >= KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES) {
                        flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                      ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    } else {
                        g_pending_on_elapsed[ch] = (uint16_t)elapsed;
                    }
                    continue;
                }

                // Wait for fresh 1n if required
                if (g_need_fresh_1n[ch] && !g_pend[ch].has_1n) {
                    uint32_t elapsed = (uint32_t)g_pending_on_elapsed[ch] + wait_samples;
                    if (elapsed >= KEYON_WAIT_FOR_FNUM_TIMEOUT_SAMPLES) {
                        OpllPendingCh temp = g_pend[ch];
                        if (!temp.has_1n && g_stamp[ch].valid_1n) {
                            temp.has_1n = 1;
                            temp.reg1n = g_stamp[ch].last_1n;
                        }
                        if (p_opts && p_opts->debug.verbose)
                            fprintf(stderr,"[KEYON_FNUM_TIMEOUT_FLUSH] ch=%d use_stamp_1n=%d val=%02X\n",
                                    ch, (int)g_stamp[ch].valid_1n, g_stamp[ch].last_1n);
                        flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                      ch, NULL, p_opts, &temp, &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    } else {
                        g_pending_on_elapsed[ch] = (uint16_t)elapsed;
                    }
                }
            }
        }

        // 3) Delayed KeyOff flush (emit B0 when minimum gate is satisfied)
        if (g_has_pending_keyoff[ch] && g_gate_elapsed[ch] >= get_min_gate(p_opts)) {
            uint8_t v2n = g_pending_keyoff_val2n[ch];
            uint8_t opl3_bn = opll_to_opl3_bn(v2n); // v2n should already be KO=0
            duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                 0xB0 + ch, opl3_bn, p_opts);

            // Update stamp and state
            g_stamp[ch].last_2n = v2n; g_stamp[ch].valid_2n = 1; g_stamp[ch].ko = 0;
            p_state->last_key[ch] = 0;
            g_has_pending_keyoff[ch] = 0;

            if (p_opts && p_opts->debug.verbose) {
                fprintf(stderr,"[DELAY_KEYOFF_FLUSH] ch=%d elapsed=%u/%u reg2n=%02X\n",
                        ch, g_gate_elapsed[ch], get_min_gate(p_opts), v2n);
            }
            opl3_metrics_note_off(ch);
            if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
        }
    }
    // Note: vgm_wait_samples is called at the call site, not here.
}

/**
 * Debug: calculate effective modulator TL from KSL and block.
 */
static inline uint8_t debug_effective_mod_tl(uint8_t raw_tl, uint8_t ksl, uint8_t block) {
    uint8_t add = 0;
    if (block >= 5) add = (uint8_t)(ksl * 2);
    else if (block >= 4) add = (uint8_t)(ksl);
    uint16_t eff = raw_tl + add;
    if (eff > 63) eff = 63;
    return (uint8_t)eff;
}

/**
 * Suppress duplicate B0 writes when unchanged key state.
 */
static bool key_state_already(OPL3State *p_state, int ch, bool key_on) {
    if (p_state->last_key[ch] == (key_on ? 1 : 0)) return true;
    p_state->last_key[ch] = key_on ? 1 : 0;
    return false;
}

/**
 * Emergency boost for carrier TL (for debugging/audible-sanity).
 */
static inline uint8_t emergency_boost_carrier_tl(uint8_t car40,
                                                 int boost_steps,
                                                 const CommandOptions *p_opts)
{
    if (boost_steps <= 0) return car40;
    uint8_t ksl = car40 & 0xC0;
    uint8_t tl  = car40 & 0x3F;
    int new_tl = (int)tl - boost_steps;
    if (new_tl < 0) new_tl = 0;
    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,"[BOOST] carrier TL %u -> %d (steps=%d)\n",
                tl, new_tl, boost_steps);
    }
    return (uint8_t)(ksl | (uint8_t)new_tl);
}

// ===================== Unordered accumulator path =====================

static uint8_t g_acc_has_1n[YM2413_NUM_CH] = {0};
static uint8_t g_acc_has_2n[YM2413_NUM_CH] = {0};
static uint8_t g_acc_has_3n[YM2413_NUM_CH] = {0};
static uint8_t g_acc_1n[YM2413_NUM_CH]      = {0};
static uint8_t g_acc_2n[YM2413_NUM_CH]      = {0};
static uint8_t g_acc_3n[YM2413_NUM_CH]      = {0};

#ifndef YM2413_VOL_MAP_STEP
#define YM2413_VOL_MAP_STEP 2
#endif

/**
 * Calculate carrier TL value from YM2413 $3n register (VOL nibble).
 * Returns value to write to 0x40 + carrier slot (KSL/TL).
 */
uint8_t make_carrier_40_from_vol(VGMStatus *p_status, const OPL3VoiceParam *p_vp, uint8_t reg3n, const CommandOptions *p_opts)
{
    // YM2413 volume nibble: 0 = loudest, 15 = softest
    uint8_t vol = reg3n & 0x0F;
    uint8_t base_tl = (uint8_t)(p_vp->op[1].tl & 0x3F);
    const uint8_t STEP = YM2413_VOL_MAP_STEP;
    uint16_t tl = (uint16_t)base_tl + (uint16_t)vol * STEP;
    if (tl > 63) tl = 63;
    uint8_t ksl_bits = (p_vp->op[1].ksl & 0x03) << 6;

    static int dbg_cnt = 0;
    if (dbg_cnt < 8) {
        if (p_opts->debug.verbose) {
            fprintf(stderr,
            "[VOLMAP] reg3n=%02X vol=%u baseTL=%u => newTL=%u (STEP=%u)\n",
            reg3n, vol, base_tl, (unsigned)tl, STEP);
        }
        dbg_cnt++;
    }
    return (uint8_t)(ksl_bits | (uint8_t)tl);
}

/**
 * Reset accumulator state for the given channel.
 */
static inline void acc_reset_ch(int ch) {
    g_acc_has_1n[ch] = g_acc_has_2n[ch] = g_acc_has_3n[ch] = 0;
}

/**
 * If all (1n, 2n, 3n) are present, flush immediately (unordered compatible).
 * - If 2n (KO=1) is not present, wait.
 * - If already playing, forcibly KeyOff before retrigger (unless force_retrigger).
 * - After flush, clear accumulator to prevent double-trigger.
 */
static inline void acc_maybe_flush_triple(
    VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,
    const CommandOptions *p_opts, int ch)
{
    if (!g_acc_has_1n[ch] || !g_acc_has_2n[ch] || !g_acc_has_3n[ch]) return;

    uint8_t reg1n = g_acc_1n[ch];
    uint8_t reg2n = g_acc_2n[ch];
    uint8_t reg3n = g_acc_3n[ch];

    // If not KeyOn, wait
    if ((reg2n & 0x10) == 0) return;

    // If already on (unless force retrigger), skip to prevent duplicate KeyOn
    if (g_stamp[ch].ko && !g_triple_force_retrigger) {
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr, "[TRIPLE_SKIP_ALREADY_ON] ch=%d\n", ch);
        }
        return;
    }

    // If fresh 1n is required and not present, wait (prevents 0Hz KeyOn)
    if (g_need_fresh_1n[ch]) {
        uint8_t prev1 = g_stamp[ch].valid_1n ? g_stamp[ch].last_1n : 0xFF;
        if (reg1n == 0x00 || (g_stamp[ch].valid_1n && reg1n == prev1)) {
            if (p_opts && p_opts->debug.verbose) {
                fprintf(stderr, "[TRIPLE_WAIT_FRESH_1N] ch=%d reg1n=%02X prev=%02X (defer flush)\n",
                        ch, reg1n, prev1);
            }
            return;
        }
    }

    // If already playing, forcibly KeyOff before retrigger (ignore delay)
    if (g_stamp[ch].ko) {
#if OPLL_ENABLE_INJECT_WAIT_ON_KEYOFF
        if (p_opts && p_opts->debug.audible_sanity) {
            if (g_gate_elapsed[ch] < get_min_gate(p_opts)) {
                uint16_t need = (uint16_t)(get_min_gate(p_opts) - g_gate_elapsed[ch]);
                if (p_opts->debug.verbose) {
                    fprintf(stderr,"[KEYOFF_INJECT_WAIT][TRIPLE] ch=%d need=%u (elapsed=%u, min=%u)\n",
                            ch, need, g_gate_elapsed[ch], get_min_gate(p_opts));
                }
                vgm_wait_samples(p_music_data, &p_vgm_context->status, need);
                uint32_t el2 = (uint32_t)g_gate_elapsed[ch] + need;
                g_gate_elapsed[ch] = (el2 > 0xFFFF) ? 0xFFFF : (uint16_t)el2;
            }
        }
#endif
        uint8_t ko_val2n = g_has_pending_keyoff[ch] ? g_pending_keyoff_val2n[ch]
                                                    : opll_make_keyoff(g_stamp[ch].last_2n);
        uint8_t opl3_bn = opll_to_opl3_bn(ko_val2n);
        duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                             0xB0 + ch, opl3_bn, p_opts);
        g_stamp[ch].last_2n = ko_val2n; g_stamp[ch].valid_2n = 1; g_stamp[ch].ko = 0;
        p_state->last_key[ch] = 0;
        g_has_pending_keyoff[ch] = 0;
        if (p_opts && p_opts->debug.verbose)
            fprintf(stderr,"[TRIPLE_PRE_KEYOFF] ch=%d reg2n=%02X\n", ch, ko_val2n);

#if OPLL_ENABLE_WAIT_BEFORE_KEYON
        if (p_opts && p_opts->debug.audible_sanity && get_min_off_on_wait(p_opts) > 0) {
            vgm_wait_samples(p_music_data, &p_vgm_context->status, (uint16_t)get_min_off_on_wait(p_opts));
            if (p_opts->debug.verbose) {
                fprintf(stderr,"[OFF_TO_ON_WAIT][TRIPLE] ch=%d samples=%u\n",
                        ch, (unsigned)get_min_off_on_wait(p_opts));
            }
        }
#endif
    }

    // Prepare temp pending for KeyOn
    OpllPendingCh temp = {0};
    temp.has_1n = 1; temp.reg1n = reg1n;
    temp.has_2n = 1; temp.reg2n = reg2n;
    temp.has_3n = 1; temp.reg3n = reg3n;

    if (p_opts && p_opts->debug.verbose)
        fprintf(stderr,"[TRIPLE_FLUSH] ch=%d 1n=%02X 2n=%02X 3n=%02X\n", ch, reg1n, reg2n, reg3n);

    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                  ch, NULL, p_opts, &temp, &g_stamp[ch]);

    // Post-processing: clear accumulator and pending to prevent double-trigger
    acc_reset_ch(ch);
    opll_pending_clear(&g_pend[ch]);
    g_pending_on_elapsed[ch] = 0;
    g_need_fresh_1n[ch] = 0;
    g_has_pending_keyoff[ch] = 0;
    g_gate_elapsed[ch] = 0;
}

/**
 * Flush pending channel state (KeyOn/KeyOff/param writes).
 */
static inline void flush_channel_ch(
    VGMBuffer *p_music_data, VGMStatus *p_vstatus, OPL3State *p_state,
    int ch, const OPL3VoiceParam *p_vp_unused, const CommandOptions *p_opts,
    OpllPendingCh* p_pend, OpllStampCh* p_stamp)
{
    // Determine which pending registers need update
    bool need_1n = p_pend->has_1n && (!p_stamp->valid_1n || p_pend->reg1n != p_stamp->last_1n);
    bool need_3n = p_pend->has_3n && (!p_stamp->valid_3n || p_pend->reg3n != p_stamp->last_3n);
    bool need_2n = p_pend->has_2n && (!p_stamp->valid_2n || p_pend->reg2n != p_stamp->last_2n);

    PendingEdgeInfo edge = analyze_pending_edge_ch(p_pend, p_stamp, p_opts);
    fprintf(stderr,
        "[FLUSH] ch=%d pend{1:%c 3:%c 2:%c} edge{on:%d off:%d} ko(stamp)=%d next2n=%02X last2n=%02X\n",
        ch, p_pend->has_1n?'Y':'n', p_pend->has_3n?'Y':'n', p_pend->has_2n?'Y':'n',
        edge.note_on_edge, edge.note_off_edge, p_stamp->ko,
        p_pend->has_2n ? p_pend->reg2n : 0xFF,
        p_stamp->valid_2n ? p_stamp->last_2n : 0xFF);

    uint8_t car_slot = opl3_local_car_slot((uint8_t)ch);
    bool delayed_keyoff = false;

    // KeyOn edge
    if (edge.has_2n && edge.note_on_edge) {
        if (!have_inst_ready_policy(p_pend, p_stamp)) {
            if (p_opts && p_opts->debug.verbose) fprintf(stderr,"[ABORT_KEYON_NO_INST] ch=%d\n", ch);
            return;
        }
        if (!have_fnum_ready_policy(ch, p_pend, p_stamp)) {
            if (p_opts && p_opts->debug.verbose) fprintf(stderr,"[ABORT_KEYON_NO_FNUM] ch=%d\n", ch);
            return;
        }
        uint8_t reg3n_eff = p_pend->has_3n ? p_pend->reg3n : p_stamp->last_3n;
        uint8_t reg2n_eff = p_pend->reg2n;
        uint8_t reg1n_eff = p_pend->has_1n ? p_pend->reg1n : p_stamp->last_1n;

        OPL3VoiceParam vp_cached;
        apply_inst_before_keyon(p_music_data, p_vstatus, p_state, ch, reg3n_eff, p_opts, &vp_cached);

        uint8_t car40 = make_carrier_40_from_vol(p_vstatus, &vp_cached, reg3n_eff, p_opts);
        car40 = emergency_boost_carrier_tl(car40, p_opts ? p_opts->emergency_boost_steps : 0, p_opts);
        if (p_opts && p_opts->carrier_tl_clamp_enabled) {
            uint8_t tl = car40 & 0x3F;
            if (tl > p_opts->carrier_tl_clamp) car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
        }
#if OPLL_DEBUG_FORCE_CAR_TL_ZERO
        car40 = (uint8_t)((car40 & 0xC0) | 0x00);
        if (p_opts && p_opts->debug.verbose)
            fprintf(stderr,"[FORCE_TL0][KEYON] ch=%d\n", ch);
#endif
        uint8_t a0_val = opll_to_opl3_an(reg1n_eff);
        uint8_t b0_val = opll_to_opl3_bn(reg2n_eff);
        if (g_freqmap_opllblock) {
            uint8_t a_tmp=0, b_no_ko=0;
            double src_clk = (p_last_ctx && p_last_ctx->source_fm_clock > 0.0) ? p_last_ctx->source_fm_clock : 3579545.0;
            double dst_clk = (p_last_ctx && p_last_ctx->target_fm_clock > 0.0) ? p_last_ctx->target_fm_clock : (double)OPL3_CLOCK;
            map_opll_to_opl3_freq(reg1n_eff, reg2n_eff, src_clk, dst_clk, &a_tmp, &b_no_ko);
            a0_val = a_tmp;
            b0_val = (uint8_t)(b_no_ko | (reg2n_eff & 0x10 ? 0x20 : 0x00));
            if (g_freqmap_opllblock) {
                uint8_t keyon_bit = (reg2n_eff & 0x10) ? 0x20 : 0x00;
                fprintf(stderr, "[FREQMAP] USED map: ch=%d src{blk=%u fnum=%u} -> dst{blk=%u fnum=%u} KO=%d\n",
                        ch,
                        (unsigned)((reg2n_eff >> 1) & 0x07),
                        (unsigned)((((reg2n_eff & 0x01) << 8) | reg1n_eff) & 0x1FF),
                        (unsigned)((b0_val >> 2) & 0x07),
                        (unsigned)(((b0_val & 0x03) << 8) | a0_val),
                        keyon_bit ? 1 : 0);
            } else {
                fprintf(stderr, "[FREQMAP] BYPASS (bit-copy): ch=%d blk=%u fnum=%u\n",
                        ch,
                        (unsigned)((reg2n_eff >> 2) & 0x07),
                        (unsigned)((((reg2n_eff & 0x01) << 8) | reg1n_eff) & 0x1FF));
            }
        }

        // Write A0 (frequency LSB)
        duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xA0 + ch, a0_val, p_opts);

        // Write TL (as usual)
        duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0x40 + car_slot, car40, p_opts);

#if OPLL_ENABLE_WAIT_BEFORE_KEYON
        if (p_opts && p_opts->debug.audible_sanity) {
            uint16_t pre = (uint16_t)get_pre_keyon_wait(p_opts);
            if (pre > 0) {
                vgm_wait_samples(p_music_data, p_vstatus, pre);
                if (p_opts->debug.verbose) {
                    fprintf(stderr,"[PRE_KEYON_WAIT] ch=%d samples=%u\n", ch, (unsigned)pre);
                }
                uint32_t newel = (uint32_t)g_gate_elapsed[ch] + (uint32_t)pre;
                g_gate_elapsed[ch] = (newel > 0xFFFF) ? 0xFFFF : (uint16_t)newel;
                if (p_opts && p_opts->debug.verbose) {
                    fprintf(stderr,"[PRE_KEYON_ADVANCE] ch=%d elapsed(after)=%u\n", ch, g_gate_elapsed[ch]);
                }
            }
        }
#endif
        if (!key_state_already(p_state, ch, true)) {
            duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xB0 + ch, b0_val, p_opts);

            uint16_t fnum10 = (uint16_t)(((b0_val & 0x03) << 8) | a0_val);
            uint8_t  blk3   = (uint8_t)((b0_val >> 2) & 0x07);
            opl3_metrics_note_on(ch, fnum10, blk3);
        }
        acc_reset_ch(ch);

        g_need_fresh_1n[ch] = 0;
        g_gate_elapsed[ch] = 0;
        g_has_pending_keyoff[ch] = 0;
    }
    // KeyOff edge
    else if (edge.has_2n && edge.note_off_edge) {
        if (g_gate_elapsed[ch] < get_min_gate(p_opts)) {
#if OPLL_ENABLE_INJECT_WAIT_ON_KEYOFF
            if (p_opts && p_opts->debug.audible_sanity) {
                uint16_t need = (uint16_t)(get_min_gate(p_opts) - g_gate_elapsed[ch]);
                if (p_opts->debug.verbose) {
                    fprintf(stderr,"[KEYOFF_INJECT_WAIT] ch=%d need=%u (elapsed=%u, min=%u)\n",
                            ch, need, g_gate_elapsed[ch], get_min_gate(p_opts));
                }
                vgm_wait_samples(p_music_data, p_vstatus, need);
                uint32_t el2 = (uint32_t)g_gate_elapsed[ch] + need;
                g_gate_elapsed[ch] = (el2 > 0xFFFF) ? 0xFFFF : (uint16_t)el2;

                uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p_pend->reg2n));
                duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xB0 + ch, opl3_bn, p_opts);
                p_state->last_key[ch] = 0;
                if (p_opts && p_opts->debug.verbose)
                    fprintf(stderr,"[KEYOFF] ch=%d reg2n=%02X (after inject wait)\n", ch, p_pend->reg2n);

                opl3_metrics_note_off(ch);
                if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
            } else
#endif
            {
                g_has_pending_keyoff[ch]   = 1;
                g_pending_keyoff_val2n[ch] = p_pend->reg2n;
                delayed_keyoff = true;
                if (p_opts && p_opts->debug.verbose)
                    fprintf(stderr,"[DELAY_KEYOFF_ARM] ch=%d elapsed=%u/%u val=%02X\n",
                            ch, g_gate_elapsed[ch], get_min_gate(p_opts), p_pend->reg2n);
            }
        } else {
            uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p_pend->reg2n));
            duplicate_write_opl3(p_music_data, p_vstatus, p_state, 0xB0 + ch, opl3_bn, p_opts);
            p_state->last_key[ch] = 0;
            if (p_opts && p_opts->debug.verbose)
                fprintf(stderr,"[KEYOFF] ch=%d reg2n=%02X (elapsed=%u)\n", ch, p_pend->reg2n, g_gate_elapsed[ch]);

            opl3_metrics_note_off(ch);
            if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
        }
    }
    // No edge; just flush any changed params
    else {
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstatus, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p_pend->reg1n), p_opts);
        }
        if (need_3n) {
            int8_t inst = (p_pend->reg3n >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp, p_opts);
            opll_apply_all_debug(&vp_tmp, p_opts);
            uint8_t car40 = make_carrier_40_from_vol(p_vstatus, &vp_tmp, p_pend->reg3n, p_opts);
            if (p_opts && p_opts->carrier_tl_clamp_enabled) {
                uint8_t tl = car40 & 0x3F;
                if (tl > p_opts->carrier_tl_clamp) {
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[CLAMP] carrier TL %u -> %u (post-boost)\n",
                            tl, p_opts->carrier_tl_clamp);
                    car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
                }
            }
            duplicate_write_opl3(p_music_data, p_vstatus, p_state,
                                 0x40 + car_slot, car40, p_opts);
        }
        if (need_2n) {
            duplicate_write_opl3(p_music_data, p_vstatus, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(p_pend->reg2n), p_opts);
        }
    }

    // Update stamp state
    if (p_pend->has_1n && need_1n) { p_stamp->last_1n = p_pend->reg1n; p_stamp->valid_1n = 1; }
    if (p_pend->has_3n && need_3n) { p_stamp->last_3n = p_pend->reg3n; p_stamp->valid_3n = 1; }

    // If KeyOff was delayed, skip stamp update for 2n
    if (p_pend->has_2n && !delayed_keyoff) {
        p_stamp->last_2n = p_pend->reg2n; p_stamp->valid_2n = 1; p_stamp->ko = (p_pend->reg2n & 0x10) != 0;
    } else if (delayed_keyoff) {
        p_pend->has_2n = 0;
    }

    opll_pending_clear(p_pend);
}
/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t addr, uint8_t val, uint16_t next_wait_samples,
    const CommandOptions *p_opts)
{
    p_last_ctx = p_vgm_context;

    // Handle global registers (0x00 - 0x07)
    // For global registers, flush only if instrument+fresh fnum is ready
    if (addr <= 0x07) {
        g_ym2413_regs[addr] = val;
        for (int c = 0; c < YM2413_NUM_CH; ++c) {
            if (g_pend[c].has_2n && !g_stamp[c].ko && (g_pend[c].reg2n & 0x10)) {
                if (have_inst_ready_policy(&g_pend[c], &g_stamp[c]) && have_fnum_ready_policy(c, &g_pend[c], &g_stamp[c])) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                c, NULL, p_opts, &g_pend[c], &g_stamp[c]);
                    g_pending_on_elapsed[c] = 0;
                } else if (p_opts && p_opts->debug.verbose) {
                    fprintf(stderr,"[GLOBAL_SKIP_FLUSH] ch=%d instReady=%d fnumReady=%d\n",
                            c, (int)have_inst_ready_policy(&g_pend[c], &g_stamp[c]),
                            (int)have_fnum_ready_policy(c, &g_pend[c], &g_stamp[c]));
                }
            }
        }
        return 0;
    }

    g_ym2413_regs[addr] = val;

    int additional_bytes = 0;
    int is_wait_samples_done = (next_wait_samples == 0) ? 1 : 0;

    int ch = ch_from_addr(addr);
    if (ch >= 0) {
        int kind = reg_kind(addr);
        const bool pend_note_on =
            g_pend[ch].has_2n && !g_stamp[ch].ko && ((g_pend[ch].reg2n & 0x10) != 0);

        // Unordered accumulator: always take value, regardless of changes
        if (kind == 1) { g_acc_has_1n[ch] = 1; g_acc_1n[ch] = val; }
        else if (kind == 2) { g_acc_has_2n[ch] = 1; g_acc_2n[ch] = val; }
        else if (kind == 3) { g_acc_has_3n[ch] = 1; g_acc_3n[ch] = val; }

        if (kind == 1) { // $1n
            if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                g_pending_on_elapsed[ch] = 0;
            } else if (g_stamp[ch].ko) {
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xA0 + ch, opll_to_opl3_an(val), p_opts);
                g_stamp[ch].last_1n = val; g_stamp[ch].valid_1n = 1;
            } else {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }
        }
        // $3n (kind==3) path: flush immediately if KeyOn pending and fnum ready
        else if (kind == 3) { // $3n
            if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                if (have_fnum_ready_policy(ch, &g_pend[ch], &g_stamp[ch])) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                  ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                    g_pending_on_elapsed[ch] = 0;
                    if (p_opts && p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_FLUSH_ON_3N] ch=%d 3n=%02X (fnum ready)\n", ch, val);
                } else {
                    if (p_opts && p_opts->debug.verbose)
                        fprintf(stderr,"[PEND_3N_WAIT_FNUM] ch=%d 3n=%02X\n", ch, val);
                }
            } else if (g_stamp[ch].ko) {
                // While note is on: apply only VOL changes
                int8_t inst = (val >> 4) & 0x0F;
                OPL3VoiceParam vp_tmp;
                ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp, p_opts);
                opll_apply_all_debug(&vp_tmp, p_opts);

                uint8_t car40 = make_carrier_40_from_vol(&p_vgm_context->status, &vp_tmp, val, p_opts);

                // Apply boost, then clamp
                car40 = emergency_boost_carrier_tl(car40,
                        p_opts ? p_opts->emergency_boost_steps : 0, p_opts);
                if (p_opts && p_opts->carrier_tl_clamp_enabled) {
                    uint8_t tl = car40 & 0x3F;
                    if (tl > p_opts->carrier_tl_clamp) {
                        if (p_opts->debug.verbose)
                            fprintf(stderr,"[CLAMP][POST30] carrier TL %u -> %u\n",
                                tl, p_opts->carrier_tl_clamp);
                        car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
                    }
                }
#if OPLL_DEBUG_FORCE_CAR_TL_ZERO
                car40 = (uint8_t)((car40 & 0xC0) | 0x00);
                if (p_opts && p_opts->debug.verbose)
                    fprintf(stderr,"[FORCE_TL0][POST30] ch=%d\n", ch);
#endif
                uint8_t car_slot2 = opl3_local_car_slot((uint8_t)ch);
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                    0x40 + car_slot2, car40, p_opts);

                if (p_opts->debug.verbose) {
                    fprintf(stderr,
                        "[POST30] ch=%d inst=%d volNib=%u finalCarTL=%u\n",
                        ch, inst, val & 0x0F, car40 & 0x3F);
                }

                g_stamp[ch].last_3n = val; g_stamp[ch].valid_3n = 1;
                return 0;
            } else {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                if (p_opts && p_opts->debug.verbose) fprintf(stderr,"[PEND_3N_IDLE] ch=%d 3n=%02X (await keyon)\n", ch, val);
            }
        }
        // $2n (kind==2) path
        else if (kind == 2) { // $2n
            bool ko_next = (val & 0x10) != 0, ko_prev = g_stamp[ch].ko;
            if (!ko_prev && ko_next) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);

                // On KeyOn edge, clear has_1n to require fresh 1n
                g_need_fresh_1n[ch] = 1;
                g_pend[ch].has_1n = 0;
                g_pending_on_elapsed[ch] = 0;

                if (have_inst_ready_policy(&g_pend[ch], &g_stamp[ch]) && have_fnum_ready_policy(ch, &g_pend[ch], &g_stamp[ch])) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                  ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                    g_pending_on_elapsed[ch] = 0;
                    if (p_opts && p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_FLUSH_NOW] ch=%d reg2n=%02X (inst+fnum ready)\n", ch, val);
                } else {
                    if (p_opts && p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_ARM] ch=%d reg2n=%02X (await fresh 1n) instReady=%d fnumReady=%d\n",
                                ch, val,
                                (int)have_inst_ready_policy(&g_pend[ch], &g_stamp[ch]),
                                (int)have_fnum_ready_policy(ch, &g_pend[ch], &g_stamp[ch]));
                }
            } else if (ko_prev && !ko_next) {
                // KeyOff: hold if minimum gate not reached
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);

                if (g_gate_elapsed[ch] < get_min_gate(p_opts)) {
#if OPLL_ENABLE_INJECT_WAIT_ON_KEYOFF
                    if (p_opts && p_opts->debug.audible_sanity) {
                        uint16_t need = (uint16_t)(get_min_gate(p_opts) - g_gate_elapsed[ch]);
                        if (p_opts->debug.verbose) {
                            fprintf(stderr,"[KEYOFF_INJECT_WAIT][B0] ch=%d need=%u (elapsed=%u, min=%u)\n",
                                    ch, need, g_gate_elapsed[ch], get_min_gate(p_opts));
                        }
                        vgm_wait_samples(p_music_data, &p_vgm_context->status, need);
                        uint32_t el2 = (uint32_t)g_gate_elapsed[ch] + need;
                        g_gate_elapsed[ch] = (el2 > 0xFFFF) ? 0xFFFF : (uint16_t)el2;

                        // Immediate KeyOff (val is KO=0)
                        uint8_t opl3_bn = opll_to_opl3_bn(val);
                        duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                            0xB0 + ch, opl3_bn, p_opts);
                        g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = 1; g_stamp[ch].ko = 0;
                        p_state->last_key[ch] = 0;
                        if (p_opts->debug.verbose) {
                            fprintf(stderr,"[KEYOFF][B0] ch=%d reg2n=%02X (after inject wait)\n",
                                    ch, val);
                        }
                        opl3_metrics_note_off(ch);
                        if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
                    } else
#endif
                    {
                        g_has_pending_keyoff[ch]   = 1;
                        g_pending_keyoff_val2n[ch] = val;
                        if (p_opts && p_opts->debug.verbose) {
                            fprintf(stderr,
                                "[DELAY_KEYOFF_ARM][B0] ch=%d elapsed=%u/%u val=%02X\n",
                                ch, g_gate_elapsed[ch], get_min_gate(p_opts), val);
                        }
                        // Keep s->ko unchanged
                    }
                } else {
                    // Immediate KeyOff if above threshold
                    uint8_t opl3_bn = opll_to_opl3_bn(val); // val is KO=0
                    duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                         0xB0 + ch, opl3_bn, p_opts);
                    g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = 1; g_stamp[ch].ko = 0;
                    p_state->last_key[ch] = 0;
                    if (p_opts && p_opts->debug.verbose) {
                        fprintf(stderr,"[KEYOFF][B0] ch=%d reg2n=%02X (elapsed=%u)\n",
                                ch, val, g_gate_elapsed[ch]);
                    }
                    opl3_metrics_note_off(ch);
                    if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
                }
            } else {
                // Frequency MSB updates, etc.
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                if (g_stamp[ch].ko) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state, ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                } else {
                    if (p_opts && p_opts->debug.verbose) fprintf(stderr,"[B0_PEND_FREQ] ch=%d reg2n=%02X (awaiting keyon conditions)\n", ch, val);
                }
            }
        }
    }
    // Handle waits
    if (is_wait_samples_done == 0 && next_wait_samples > 0) {
        opll_tick_pending_on_elapsed(p_music_data, p_vgm_context, p_state, p_opts, next_wait_samples);
        vgm_wait_samples(p_music_data, &p_vgm_context->status, next_wait_samples);
        is_wait_samples_done = 1;
    }

    // At the end, if all (1n,2n,3n) are present, try to flush immediately (unordered accumulator)
    if (ch >= 0) {
        acc_maybe_flush_triple(p_music_data, p_vgm_context, p_state, p_opts, ch);
    }

    return additional_bytes;
}
