#include <string.h>
#include <stdio.h>
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include "../override_apply.h"
#include "../override_loader.h"
#include "../compat_bool.h"
#include "ym2413_patch_convert.h"

static inline uint8_t rate_map_pick(uint8_t raw);          /* 前方宣言 */
static inline uint8_t enforce_min_attack(uint8_t ar, const char* stage, int inst, int op_index);

void apply_debug_overrides(OPL3VoiceParam *p_vp, const CommandOptions* p_opts) {
    if (!p_vp || !p_opts) return;
    if (p_opts->debug.fast_attack) {
        for (int i=0;i<2;i++) {
            p_vp->op[i].ar = 15;
            if (p_vp->op[i].dr < 4) p_vp->op[i].dr = 4;
            if (p_vp->op[i].rr < 2) p_vp->op[i].rr = 2;
        }
        p_vp->op[1].tl = 0;
    }
    if (p_opts->debug.test_tone) {
        /* 簡易: Mod 無効 / キャリア強制 */
        p_vp->cnt[0] = 1;      /* additive 風 (あなたの実装の意味に合わせて調整) */
        p_vp->op[0].tl = 63;   /* モジュレータ無効化 */
    }
}

/* Apply voice simplify (sine wave + FB=0) */
static void apply_voice_simplify_sine(OPL3VoiceParam *p_vp, const CommandOptions* p_opts) {
    if (!p_vp || !p_opts || !p_opts->debug.voice_simplify_sine) return;
    
    // Force both operators to sine wave (WS=0)
    p_vp->op[0].ws = 0;
    p_vp->op[1].ws = 0;
    
    // Force feedback to 0
    p_vp->fb[0] = 0;
    
    // IMPORTANT: Do NOT mute the modulator here
    // Leave modulator TL as-is so we can still hear modulation
    
    if (p_opts->debug.verbose) {
        fprintf(stderr, "[VOICE_SIMPLIFY] Applied: WS[0]=0 WS[1]=0 FB=0 (modTL preserved=%u)\n", 
                p_vp->op[0].tl);
    }
}

/* Apply debug mute modulator */
static void apply_voice_debug_mute_mod(OPL3VoiceParam *p_vp, const CommandOptions* p_opts) {
    if (!p_vp || !p_opts || !p_opts->debug.voice_debug_mute_mod) return;
    
    uint8_t old_tl = p_vp->op[0].tl;
    p_vp->op[0].tl = 63; // Mute modulator
    
    if (p_opts->debug.verbose) {
        fprintf(stderr, "[MUTE_MOD] Modulator TL: %u -> 63 (muted)\n", old_tl);
    }
}

/* Apply INST=1 specific overrides */
static void apply_inst1_overrides(int inst, OPL3VoiceParam *p_vp, const CommandOptions* p_opts) {
    if (!p_vp || !p_opts || inst != 1) return;
    
    bool applied = false;
    
    if (p_opts->debug.inst1_fb_override >= 0 && p_opts->debug.inst1_fb_override <= 7) {
        uint8_t old_fb = p_vp->fb[0];
        p_vp->fb[0] = (uint8_t)p_opts->debug.inst1_fb_override;
        if (p_opts->debug.verbose) {
            fprintf(stderr, "[INST1_FB] FB: %u -> %u\n", old_fb, p_vp->fb[0]);
        }
        applied = true;
    }
    
    if (p_opts->debug.inst1_tl_override >= 0 && p_opts->debug.inst1_tl_override <= 63) {
        uint8_t old_tl = p_vp->op[0].tl;
        p_vp->op[0].tl = (uint8_t)p_opts->debug.inst1_tl_override;
        if (p_opts->debug.verbose) {
            fprintf(stderr, "[INST1_TL] Modulator TL: %u -> %u\n", old_tl, p_vp->op[0].tl);
        }
        applied = true;
    }
    
    if (p_opts->debug.inst1_ws_override >= 0 && p_opts->debug.inst1_ws_override <= 7) {
        uint8_t old_ws0 = p_vp->op[0].ws;
        uint8_t old_ws1 = p_vp->op[1].ws;
        p_vp->op[0].ws = (uint8_t)p_opts->debug.inst1_ws_override;
        p_vp->op[1].ws = (uint8_t)p_opts->debug.inst1_ws_override;
        if (p_opts->debug.verbose) {
            fprintf(stderr, "[INST1_WS] WS: [%u,%u] -> [%u,%u]\n", 
                    old_ws0, old_ws1, p_vp->op[0].ws, p_vp->op[1].ws);
        }
        applied = true;
    }
    
    if (applied && p_opts->debug.verbose) {
        fprintf(stderr, "[INST1] Applied overrides for Violin (INST=1)\n");
    }
}

/* 唯一の “鳴るための” 調整 */
void apply_audible_sanity(OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    if (!p_vp || !p_opts || !p_opts->debug.audible_sanity) return;

    uint8_t b_mod = p_vp->op[0].tl;

    /* Carrier TL は触らない: p_vp->op[1].tl は 0 のまま */
    if (p_vp->op[0].tl < 0x24) p_vp->op[0].tl = 0x24;

    for (int i=0;i<2;i++) {
        if (p_vp->op[i].ar < 12) p_vp->op[i].ar = 12;
        if (p_vp->op[i].dr < 4 ) p_vp->op[i].dr = 4;
        if (p_vp->op[i].rr < 4 ) p_vp->op[i].rr = 4;
        p_vp->op[i].sl = 0;
    }
    if (p_opts->debug.verbose) {
        fprintf(stderr,
            "[AUDIBLE] modTL %u->%u carTL (unchanged=%u) (AR>=12 DR/RR>=4 SL=0)\n",
            b_mod, p_vp->op[0].tl, p_vp->op[1].tl);
    }
}

/* まとめ呼び出し */
void opll_apply_all_debug(OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    if (!p_vp || !p_opts) return;
    apply_debug_overrides(p_vp, p_opts);
    apply_audible_sanity(p_vp, p_opts);
}

void ym2413_patch_to_opl3_with_fb(int inst,
                                  const uint8_t *p_ym2413_regs,
                                  OPL3VoiceParam *p_vp,
                                  const CommandOptions *p_opts)
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
    uint8_t m_ar_raw = (src[2] >> 4) & 0x0F;
    uint8_t m_dr_raw =  src[2]       & 0x0F;

    p_vp->op[0].am   = (src[0] >> 7) & 1;
    p_vp->op[0].vib  = (src[0] >> 6) & 1;
    p_vp->op[0].egt  = (src[0] >> 5) & 1;
    p_vp->op[0].ksr  = (src[0] >> 4) & 1;
    p_vp->op[0].mult =  src[0]       & 0x0F;
    p_vp->op[0].ksl  = (src[1] >> 6) & 3;
    p_vp->op[0].tl   =  src[1]       & 0x3F; /* Mod TL は存在 */
    p_vp->op[0].ar   = rate_map_pick(m_ar_raw);
    p_vp->op[0].dr   = rate_map_pick(m_dr_raw);
    p_vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    p_vp->op[0].rr   =  src[3]       & 0x0F;
    p_vp->op[0].ws   = 0;

    p_vp->op[0].ar = enforce_min_attack(p_vp->op[0].ar, "Mod", inst, 0);

    /* --- Carrier (4..7) --- */
    uint8_t c_ar_raw = (src[6] >> 4) & 0x0F;
    uint8_t c_dr_raw =  src[6]       & 0x0F;

    p_vp->op[1].am   = (src[4] >> 7) & 1;
    p_vp->op[1].vib  = (src[4] >> 6) & 1;
    p_vp->op[1].egt  = (src[4] >> 5) & 1;
    p_vp->op[1].ksr  = (src[4] >> 4) & 1;
    p_vp->op[1].mult =  src[4]       & 0x0F;
    p_vp->op[1].ksl  = (src[5] >> 6) & 3;
    p_vp->op[1].tl   = 0; /* キャリアには TL レジスタが無いので基礎 0 */
    p_vp->op[1].ar   = rate_map_pick(c_ar_raw);
    p_vp->op[1].dr   = rate_map_pick(c_dr_raw);
    p_vp->op[1].sl   = (src[7] >> 4) & 0x0F;
    p_vp->op[1].rr   =  src[7]       & 0x0F;
    p_vp->op[1].ws   = 0;

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
    
    /* Apply new timbre debugging features (in order) */
    apply_voice_simplify_sine(p_vp, p_opts);
    apply_inst1_overrides(inst, p_vp, p_opts);
    apply_voice_debug_mute_mod(p_vp, p_opts);
}

static uint8_t final_tl_clamp(uint8_t car40, const CommandOptions *p_opts) {
    if (!p_opts || p_opts->carrier_tl_clamp < 0) return car40;
    uint8_t ksl = (uint8_t)((car40 >> 6) & 0x03);
    uint8_t tl  = (uint8_t)(car40 & 0x3F);
    if (tl > (uint8_t)p_opts->carrier_tl_clamp) {
        if (p_opts->debug.verbose)
            fprintf(stderr,"[CLAMP] Carrier final TL %u -> %d\n", tl, p_opts->carrier_tl_clamp);
        tl = (uint8_t)p_opts->carrier_tl_clamp;
    }
    return (uint8_t)((ksl << 6) | tl);
}

/** Suppress duplicate B0 writes when unchanged key state */
static bool key_state_already(OPL3State *p_state, int ch, bool key_on) {
    // (Add small array in state to remember last key bit)
    if (p_state->last_key[ch] == (key_on?1:0)) return true;
    p_state->last_key[ch] = key_on?1:0;
    return false;
}

void apply_carrier_tl_clamp (OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {

    if (!p_opts->carrier_tl_clamp_enabled) return;
    if (p_vp->op[1].tl > p_opts->carrier_tl_clamp) {
        if (p_opts->debug.verbose) {
            fprintf(stderr,"[CLAMP] Carrier TL %u -> %u\n", p_vp->op[1].tl, p_opts->carrier_tl_clamp);
        }
        p_vp->op[1].tl = p_opts->carrier_tl_clamp;
    }
}

void finalize_opl3_pair (OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    /* audible-sanity → clamp の順番 */
    apply_audible_sanity(p_vp, p_opts);
    apply_carrier_tl_clamp(p_vp,p_opts);
}

void opl3_apply_debug_adjust(OPL3VoiceParam *p_vp,
                             const CommandOptions *p_opts) {
    if (p_opts->debug.audible_sanity) {
        // audible-sanity (B)
        // Carrier: なるべく小さく
        if (p_vp->op[1].tl > 0x10) p_vp->op[1].tl = 0x10;
        // Modulator: 変調を弱める（TL 大きく）
        if (p_vp->op[0].tl < 0x24) p_vp->op[0].tl = 0x24;
        // AR/DR/RR 強制高速 (max/min なので再適用で悪化しない)
        if (p_vp->op[1].ar < 12) p_vp->op[1].ar = 12;
        if (p_vp->op[0].ar < 12) p_vp->op[0].ar = 12;
        if (p_vp->op[1].dr < 4)  p_vp->op[1].dr = 4;
        if (p_vp->op[0].dr < 4)  p_vp->op[0].dr = 4;
        if (p_vp->op[1].rr < 4)  p_vp->op[1].rr = 4;
        if (p_vp->op[0].rr < 4)  p_vp->op[0].rr = 4;
        p_vp->op[0].sl = 0;
        p_vp->op[1].sl = 0;
    }
    if (p_opts->carrier_tl_clamp_enabled) {
        if (p_vp->op[1].tl > p_opts->carrier_tl_clamp) {
            p_vp->op[1].tl = p_opts->carrier_tl_clamp;
        }
    }
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
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* ar, uint8_t* dr) {
    (void)inst; (void)op_index; (void)ar; (void)dr;
}
#else
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* ar, uint8_t* dr) {
    if (!ar || !dr) return;
    uint8_t A = (uint8_t)(*ar & 0x0F);
    uint8_t D = (uint8_t)(*dr & 0x0F);
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
