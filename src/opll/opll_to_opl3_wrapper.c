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

static VGMContext *g_last_ctx = NULL;


static inline void flush_channel_ch(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp_unused, const CommandOptions *p_opts,
    OpllPendingCh* p, OpllStampCh* s);

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

/** Store program arguments for later use */
void opll_set_program_args(int argc, char **argv) {
    g_saved_argc = argc;
    g_saved_argv = argv;
}

/* 新ヘルパ: 有効な 3n があるか */
static inline bool opll_have_inst_ready(int ch) {
    return g_pend[ch].has_3n || g_stamp[ch].valid_3n;
}
static inline bool opll_have_fnum_ready(int ch) {
    return g_pend[ch].has_1n || g_stamp[ch].valid_1n;
}


/** Initialize OPLL/OPL3 voice DB and state */
void opll_init(OPL3State *p_state, const CommandOptions* p_opts) {
    if (!p_state) return;
    opl3_register_all_ym2413(&p_state->voice_db, p_opts);
    memset(g_ym2413_regs, 0, sizeof(g_ym2413_regs));
    for (int i = 0; i < YM2413_NUM_CH; ++i) {
        opll_pending_clear(&g_pend[i]);
        stamp_clear(&g_stamp[i]);
        g_pending_on_elapsed[i] = 0;
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


// ---------- Utility functions ----------

static inline int ch_from_addr(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return addr - 0x10;
    if (addr >= 0x20 && addr <= 0x28) return addr - 0x20;
    if (addr >= 0x30 && addr <= 0x38) return addr - 0x30;
    return -1;
}
/** Get register kind (1n,2n,3n) from address */
static inline int reg_kind(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return 1;
    if (addr >= 0x20 && addr <= 0x28) return 2;
    if (addr >= 0x30 && addr <= 0x38) return 3;
    return 0;
}

/** Calculate OPLL frequency for debugging */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opllfrequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
    return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);
}

/** Get KeyOff value for reg2n (clear KeyOn bit) */
static inline uint8_t opll_make_keyoff(uint8_t reg2n) {
    return (uint8_t)(reg2n & (uint8_t)~0x10);
}

/** Set pending values from OPLL write */
static inline void set_pending_from_opll_write_ch(OpllPendingCh* p, const OpllStampCh* s, uint8_t addr, uint8_t value) {
    (void)s;
    switch (reg_kind(addr)) {
        case 1: p->has_1n = 1; p->reg1n = value; break;
        case 2: p->has_2n = 1; p->reg2n = value; break;
        case 3: p->has_3n = 1; p->reg3n = value; break;
        default: break;
    }
}
static inline void set_pending_from_opll_write(OpllPendingCh pend[], const OpllStampCh stamp[], uint8_t addr, uint8_t value) {
    int ch = ch_from_addr(addr); if (ch < 0) return;
    set_pending_from_opll_write_ch(&pend[ch], &stamp[ch], addr, value);
}

/** Analyze pending edge state (note on/off detection) */
static inline PendingEdgeInfo analyze_pending_edge_ch(
    const OpllPendingCh* p_p, const OpllStampCh* p_s, const CommandOptions *opts)
{
    PendingEdgeInfo info = {0};
    if (!p_p || !p_s || !p_p->has_2n) return info;
    bool ko_next = (p_p->reg2n & 0x10) != 0;
    info.has_2n  = 1;
    info.ko_next = ko_next;
    bool prev_ko = p_s->ko;

    // retrigger オプションが無ければ純粋な 0→1
    if (opts && opts->force_retrigger_each_note) {
        info.note_on_edge = (!prev_ko && ko_next);
    } else {
        info.note_on_edge = (!prev_ko && ko_next);
    }
    info.note_off_edge = (prev_ko && !ko_next);
    return info;
}

/** Check if pending is needed for next write */
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

/** Convert OPLL 1n/2n register values to OPL3 format */
static inline uint8_t opll_to_opl3_an(uint8_t reg1n) { return reg1n; }
static inline uint8_t opll_to_opl3_bn(uint8_t reg2n) {
    uint8_t fnum_msb_2b = (reg2n & 0x01);
    uint8_t block_3b    = (reg2n >> 1) & 0x07;
    uint8_t ko_bit      = (reg2n & 0x10) ? 0x20 : 0x00;
    return (fnum_msb_2b) | (block_3b << 2) | ko_bit;
}

/** Slot mapping for OPL3 operators */
static inline uint8_t opl3_local_mod_slot(uint8_t ch_local) {
    return (uint8_t)((ch_local % 3) + (ch_local / 3) * 8);
}
static inline uint8_t opl3_local_car_slot(uint8_t ch_local) {
    return (uint8_t)(opl3_local_mod_slot(ch_local) + 3);
}
static inline uint8_t opl3_opreg_addr(uint8_t base, uint8_t ch_local, int is_carrier) {
    uint8_t slot = is_carrier ? opl3_local_car_slot(ch_local) : opl3_local_mod_slot(ch_local);
    return (uint8_t)(base + slot);
}

/** Rate map selection (ID/SIMPLE/CALIB) */
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

/** Enforce minimum attack rate (for envelope stability) */
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

/** Envelope shape fix for steep AR/DR gaps */
#if !OPLL_ENABLE_ENVELOPE_SHAPE_FIX
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* p_ar, uint8_t* p_dr) {
    (void)inst; (void)op_index; (void)p_ar; (void)p_dr;
}
#else
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* p_ar, uint8_t* p_dr) {
    if (!p_ar || !dr) return;
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
        *dr = newD;
    } else {
#if OPLL_DEBUG_SHAPE_FIX
        printf("[SHAPEFIX] inst=%d op=%d gap=%u cond=1 but newD==D (AR=%u DR=%u) (th=%d max=%d)\n",
               inst, op_index, gap, A, D, OPLL_SHAPE_FIX_DR_GAP_THRESHOLD, OPLL_SHAPE_FIX_DR_MAX_AFTER);
#endif
    }
}
#endif

/** Apply OPL3VoiceParam to channel */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *p_opts) {
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = opl3_opreg_addr(0, ch, 0);
    int slot_car = opl3_opreg_addr(0, ch, 1);

    printf("[DEBUG] Apply OPL3 VoiceParam to ch=%d\n", ch);

    uint8_t opl3_2n_mod = (uint8_t)((vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, opl3_2n_car, p_opts);

    uint8_t opl3_4n_mod = (uint8_t)(((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, opl3_4n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, opl3_4n_car, p_opts);

    uint8_t opl3_6n_mod = (uint8_t)((vp->op[0].ar << 4) | (vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((vp->op[1].ar << 4) | (vp->op[1].dr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, opl3_6n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, opl3_6n_car, p_opts);

    uint8_t opl3_8n_mod = (uint8_t)((vp->op[0].sl << 4) | (vp->op[0].rr & 0x0F));
    uint8_t opl3_8n_car = (uint8_t)((vp->op[1].sl << 4) | (vp->op[1].rr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, opl3_8n_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, opl3_8n_car, p_opts);

    uint8_t c0_val = (uint8_t)(0xC0 | ((vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, c0_val, p_opts);

    uint8_t opl3_en_mod = (uint8_t)((vp->op[0].ws & 0x07));
    uint8_t opl3_en_car = (uint8_t)((vp->op[1].ws & 0x07));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, opl3_en_mod, p_opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, opl3_en_car, p_opts);

    return bytes;
}

/** Register all YM2413 patches to OPL3 voice DB */
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

/** Apply voice params to channel before KeyOn */
static inline void apply_inst_before_keyon(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, uint8_t reg3n, const CommandOptions *p_opts,
    OPL3VoiceParam *out_cached_vp /* ← 追加: 後段で再利用 */
) {
    int8_t inst = (reg3n >> 4) & 0x0F;
    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp, p_opts);

    /* ここでデバッグ/鳴る調整を適用してから一括書き込み */
    opll_apply_all_debug(&vp, p_opts);

    /* （初期書き込み）全スロット/フィードバック/エンベロープ */
    opl3_voiceparam_apply(p_music_data, p_vstat, p_state, ch, &vp, p_opts);

    /* 呼び出し元で volume nibble 反映や clamp/boost を行うため返却 */
    if (out_cached_vp) *out_cached_vp = vp;
}

/** Check if channel has effective 3n value */
static inline bool has_effective_3n(const OpllPendingCh* p_p, const OpllStampCh* p_s) {
    return (p_p && p_p->has_3n) || (p_s && p_s->valid_3n);
}

/** Flush channel wrapper (calls flush_channel_ch) */
static inline void flush_channel(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *p_opts, OpllPendingCh* p, OpllStampCh* s)
{
    (void)vp;
    flush_channel_ch(p_music_data, p_vstat, p_state, ch, vp, p_opts, p, s);
}

/** Update pending on elapsed for KeyOn timeout */
static inline void opll_tick_pending_on_elapsed(
    VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,
    const CommandOptions *p_opts, uint16_t wait_samples)
{
    if (wait_samples == 0) return;
    for (int ch = 0; ch < YM2413_NUM_CH; ++ch) {
        if (g_pend[ch].has_2n) {
            bool ko_next = (g_pend[ch].reg2n & 0x10) != 0;
            if (ko_next && !g_stamp[ch].ko) {
                if (!has_effective_3n(&g_pend[ch], &g_stamp[ch])) {
                    uint32_t elapsed = (uint32_t)g_pending_on_elapsed[ch] + wait_samples;
                    if (elapsed >= KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES) {
                        flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                      ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    } else {
                        g_pending_on_elapsed[ch] = (uint16_t)elapsed;
                    }
                }
            }
        }
    }
}

/** Debug: calculate effective modulator TL from KSL and block */
static inline uint8_t debug_effective_mod_tl(uint8_t raw_tl, uint8_t ksl, uint8_t block) {
    uint8_t add = 0;
    if (block >= 5) add = (uint8_t)(ksl * 2);
    else if (block >= 4) add = (uint8_t)(ksl);
    uint16_t eff = raw_tl + add;
    if (eff > 63) eff = 63;
    return (uint8_t)eff;
}

/** Suppress duplicate B0 writes when unchanged key state */
static bool key_state_already(OPL3State *p_state, int ch, bool key_on) {
    // (Add small array in state to remember last key bit)
    if (p_state->last_key[ch] == (key_on?1:0)) return true;
    p_state->last_key[ch] = key_on?1:0;
    return false;
}


static inline uint8_t emergency_boost_carrier_tl(uint8_t car40,
                                                 int boost_steps,
                                                 const CommandOptions *opts)
{
    if (boost_steps <= 0) return car40;
    uint8_t ksl = car40 & 0xC0;
    uint8_t tl  = car40 & 0x3F;
    int new_tl = (int)tl - boost_steps;
    if (new_tl < 0) new_tl = 0;
    if (opts && opts->debug.verbose) {
        fprintf(stderr,"[BOOST] carrier TL %u -> %d (steps=%d)\n",
                tl, new_tl, boost_steps);
    }
    return (uint8_t)(ksl | (uint8_t)new_tl);
}

/** Flush pending channel state (KeyOn/KeyOff/param writes) */
static inline void flush_channel_ch(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp_unused, const CommandOptions *p_opts,
    OpllPendingCh* p, OpllStampCh* s)
{
    // Determine which pending registers need update
    bool need_1n = p->has_1n && (!s->valid_1n || p->reg1n != s->last_1n);
    bool need_3n = p->has_3n && (!s->valid_3n || p->reg3n != s->last_3n);
    bool need_2n = p->has_2n && (!s->valid_2n || p->reg2n != s->last_2n);

    //PendingEdgeInfo e = analyze_pending_edge_ch(p_music_data, p_vstat, p_state, ch, vp_unused, p_opts, p, s);
    PendingEdgeInfo e = analyze_pending_edge_ch(p, s, p_opts);
    fprintf(stderr,
    "[FLUSH] ch=%d pend{1:%c 3:%c 2:%c} edge{on:%d off:%d} ko(stamp)=%d next2n=%02X last2n=%02X\n",
    ch, p->has_1n?'Y':'n', p->has_3n?'Y':'n', p->has_2n?'Y':'n',
    e.note_on_edge, e.note_off_edge, s->ko,
    p->has_2n ? p->reg2n : 0xFF,
    s->valid_2n ? s->last_2n : 0xFF);


    uint8_t car_slot = opl3_local_car_slot((uint8_t)ch);

    if (e.has_2n && e.note_on_edge) {
        /* guard: inst 未確定なら flush 中断 (二重防御) */
        if (!opll_have_inst_ready(ch)) {
            if (p_opts->debug.verbose)
                fprintf(stderr,"[ABORT_KEYON_NO_INST] ch=%d (reg2n=%02X)\n",
                    ch, p->reg2n);
            /* pending 状態を維持: return せず but B0 書かない */
            // stamp は更新しないで戻す
            return;
        }
        if (!opll_have_fnum_ready(ch)) {
            if (p_opts->debug.verbose)
                fprintf(stderr,"[ABORT_KEYON_NO_FNUM] ch=%d\n", ch);
            return;
        }
        uint8_t reg3n_eff = p->has_3n ? p->reg3n : (s->valid_3n ? s->last_3n : 0x00);
        uint8_t reg2n_eff = p->reg2n;
        uint8_t reg1n_eff = p->has_1n ? p->reg1n : (s->valid_1n ? s->last_1n : 0x00);

        if (((reg3n_eff >> 4) & 0x0F)==0 && p_opts->debug.verbose) {
            fprintf(stderr,"[WARN] ch=%d KeyOn with inst=0 (zero patch) -- likely premature flush\n", ch);
        }

        OPL3VoiceParam vp_cached;
        apply_inst_before_keyon(p_music_data, p_vstat, p_state, ch, reg3n_eff, p_opts, &vp_cached);

        /* A0 (FNUM LSB) */
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xA0 + ch, opll_to_opl3_an(reg1n_eff), p_opts);

        /* Carrier volume 反映 */
        uint8_t car40 = make_carrier_40_from_vol(&vp_cached, reg3n_eff);

       /* boost first */
        car40 = emergency_boost_carrier_tl(car40,p_opts ? p_opts->emergency_boost_steps : 0, p_opts);
        /* clamp AFTER boost so clamp is a true ceiling */
        if (p_opts && p_opts->carrier_tl_clamp_enabled) {
            uint8_t tl = car40 & 0x3F;
            if (tl > p_opts->carrier_tl_clamp) {
                if (p_opts->debug.verbose)
                    fprintf(stderr,"[CLAMP] carrier TL %u -> %u (post-boost)\n",
                        tl, p_opts->carrier_tl_clamp);
                car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
            }
        }


        uint8_t car_slot = opl3_local_car_slot((uint8_t)ch);
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0x40 + car_slot, car40, p_opts);
        #if OPLL_ENABLE_KEYON_DEBUG
        {
            uint16_t fnum = (uint16_t)reg1n_eff | ((reg2n_eff & 0x01) << 8);
            uint8_t  block = (reg2n_eff >> 1) & 0x07;
            uint8_t  mod_raw_tl = vp_cached.op[0].tl;
            uint8_t  eff_mod_tl = debug_effective_mod_tl(mod_raw_tl, vp_cached.op[0].ksl, block);
            uint8_t  car_final_tl = car40 & 0x3F;
            /* サンプル位置は p_vstat -> total_samples を優先し、無ければ g_last_ctx->status.total_samples を使う */
            uint32_t ksamp =
                (p_vstat ? p_vstat->total_samples :
                (g_last_ctx ? g_last_ctx->status.total_samples : 0));

            fprintf(stderr,
                "[DEBUG] KeyOnDbg ch=%d inst=%d fnum=%u block=%u sample=%u "
                "modTL=%u effModTL=%u carTLraw=%u carTLfinal=%u FB=%u CNT=%u\n",
                ch, (reg3n_eff >> 4) & 0x0F, fnum, block, ksamp,
                mod_raw_tl, eff_mod_tl, vp_cached.op[1].tl, car_final_tl,
                vp_cached.fb[0], vp_cached.cnt[0]);
        }
        #endif

        p_state->post_keyon_sample[ch] =
            (p_vstat ? p_vstat->total_samples :
            (g_last_ctx ? g_last_ctx->status.total_samples : 0));
        p_state->post_keyon_valid[ch] = 1;
        if (!key_state_already(p_state, ch, true)) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(reg2n_eff), p_opts);
            opl3_metrics_note_on(ch,
                (uint16_t)reg1n_eff | ((reg2n_eff & 0x01) << 8),
                (reg2n_eff >> 1) & 0x07);
        }
    }
    else if (e.has_2n && e.note_off_edge) {
        // KeyOff edge detected
        uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p->reg2n));
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, opl3_bn, p_opts);

        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), p_opts);
        }
        if (need_3n) {
            int8_t inst = (p->reg3n >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp,p_opts);
            opll_apply_all_debug(&vp_tmp, p_opts);
            uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, p->reg3n);
            /* carrier TL clamp (audible_sanity 内の基礎調整後 + VOL 反映後) */
            if (p_opts && p_opts->debug.audible_sanity) {
                uint8_t tl = car40 & 0x3F;
                if (tl > 0x10) {
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[AUDIBLE] final carrier TL clamp %u -> 16\n", tl);
                    car40 = (uint8_t)((car40 & 0xC0) | 0x10);
                }
            }
            if (p_opts && p_opts->carrier_tl_clamp_enabled) {
                uint8_t tl = car40 & 0x3F;
                if (tl > p_opts->carrier_tl_clamp) {
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[CLAMP] carrier TL %u -> %u\n", tl, p_opts->carrier_tl_clamp);
                    car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
                }
            }
            /* 緊急ブースト */
            car40 = emergency_boost_carrier_tl(car40,p_opts ? p_opts->emergency_boost_steps : 0, p_opts);
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + car_slot, car40, p_opts);
        }
        opl3_metrics_note_off(ch);
        if (g_opl3_hooks.on_note_off) g_opl3_hooks.on_note_off(ch);
    }
    else {
        // No edge; just flush any changed params
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), p_opts);
        }
        if (need_3n) {
            int8_t inst = (p->reg3n >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp, p_opts);
            opll_apply_all_debug(&vp_tmp, p_opts);
            uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, p->reg3n);
            /* clamp AFTER boost so clamp is a true ceiling */
            if (p_opts && p_opts->carrier_tl_clamp_enabled) {
                uint8_t tl = car40 & 0x3F;
                if (tl > p_opts->carrier_tl_clamp) {
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[CLAMP] carrier TL %u -> %u (post-boost)\n",
                            tl, p_opts->carrier_tl_clamp);
                    car40 = (uint8_t)((car40 & 0xC0) | (p_opts->carrier_tl_clamp & 0x3F));
                }
            }
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + car_slot, car40, p_opts);
        }
        if (need_2n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(p->reg2n), p_opts);
        }
    }

    // Update stamp state
    if (p->has_1n && need_1n) { s->last_1n = p->reg1n; s->valid_1n = 1; }
    if (p->has_3n && need_3n) { s->last_3n = p->reg3n; s->valid_3n = 1; }
    if (p->has_2n) { s->last_2n = p->reg2n; s->valid_2n = 1; s->ko = (p->reg2n & 0x10) != 0; }
    opll_pending_clear(p);
}




/** Main register write entrypoint for OPLL emulation */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t addr, uint8_t val, uint16_t next_wait_samples,
    const CommandOptions *p_opts)
{
    g_last_ctx = p_vgm_context;
    // Handle global registers (0x00 - 0x07)
    if (addr <= 0x07) {
        g_ym2413_regs[addr] = val;
        for (int c = 0; c < YM2413_NUM_CH; ++c) {
            if (g_pend[c].has_2n && !g_stamp[c].ko && (g_pend[c].reg2n & 0x10)) {
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              c, NULL, p_opts, &g_pend[c], &g_stamp[c]);
                g_pending_on_elapsed[c] = 0;
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
        } else if (kind == 3) { // $3n
           if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                // ここではまだ reg2n が KeyOn だけど 3n が来たので instrument ready
                // 1n (FNUM) も揃っていれば flush
                if (opll_have_fnum_ready(ch)) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                    g_pending_on_elapsed[ch] = 0;
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_FLUSH_ON_3N] ch=%d 3n=%02X\n", ch, val);
                } else {
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[PEND_3N_WAIT_FNUM] ch=%d 3n=%02X\n", ch, val);
                }
            } else if (g_stamp[ch].ko) {
                int8_t inst = (val >> 4) & 0x0F;
                OPL3VoiceParam vp_tmp;
                ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp, p_opts);
                opll_apply_all_debug(&vp_tmp, p_opts);

                uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, val);

                /* boost → clamp の順 (KeyOn 時と同一) */
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
                if (p_opts->debug.verbose)
                    fprintf(stderr,"[PEND_3N_IDLE] ch=%d 3n=%02X (await keyon)\n", ch, val);
            }
        } else if (kind == 2) { // $2n
            bool ko_next = (val & 0x10) != 0;
            bool ko_prev = g_stamp[ch].ko;

            if (!ko_prev && ko_next) {
                /* KeyOn 開始: まず pending に積む */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);

                /* instrument(3n) と FNUM(1n) が両方揃っているか? */
                if (opll_have_inst_ready(ch) && opll_have_fnum_ready(ch)) {
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                    g_pending_on_elapsed[ch] = 0;
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_FLUSH_NOW] ch=%d reg2n=%02X (inst+fnum ready)\n", ch, val);
                } else {
                    /* 揃ってないので後続 3n/1n まで待つ */
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[KEYON_PEND] ch=%d reg2n=%02X instReady=%d fnumReady=%d\n",
                            ch, val, opll_have_inst_ready(ch), opll_have_fnum_ready(ch));
                }
            }
            else if (ko_prev && !ko_next) {
                /* KeyOff */
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                    0xB0 + ch, opll_to_opl3_bn(val), p_opts);
                g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = 1; g_stamp[ch].ko = 0;
                if (p_opts->debug.verbose)
                    fprintf(stderr,"[KEYOFF] ch=%d reg2n=%02X\n", ch, val);
            }
            else {
                /* 継続 (周波数変更のみ) */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                /* まだ KeyOn してない or KeyOn 中? */
                if (g_stamp[ch].ko) {
                    /* 既に鳴ってる → 即書き換え */
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                ch, NULL, p_opts, &g_pend[ch], &g_stamp[ch]);
                } else {
                    /* KeyOn 未完了: flush しない */
                    if (p_opts->debug.verbose)
                        fprintf(stderr,"[B0_PEND_FREQ] ch=%d reg2n=%02X (awaiting keyon conditions)\n", ch, val);
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

    return additional_bytes;
}
