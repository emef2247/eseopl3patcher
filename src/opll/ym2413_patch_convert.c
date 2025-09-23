#include <string.h>
#include <stdio.h>
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include "../override_apply.h"
#include "../override_loader.h"
#include "../compat_bool.h"
#include "ym2413_patch_convert.h"

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

void apply_debug_overrides(OPL3VoiceParam *p_vp, const CommandOptions* p_opts) {
    if (p_opts->debug.fast_attack) {
        for (int i=0;i<2;i++) {
            p_vp->op[i].ar = 15;
            if (p_vp->op[i].dr < 4) p_vp->op[i].dr = 4;
            p_vp->op[i].rr = (p_vp->op[i].rr < 2) ? 2 : p_vp->op[i].rr;
        }
        // Ensure carrier TL=0 for audibility
        p_vp->op[1].tl = 0;
    }
    if (p_opts->debug.test_tone) {
        // Force additive mode & mute modulator
        p_vp->cnt[0] = 1; // algorithm=1 (if your mapping uses cnt to denote alg)
        p_vp->op[0].tl = 63;
    }
}


void debug_apply_audible(OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    if (!p_opts || !p_opts->debug.audible_sanity || !p_vp) return;

    uint8_t before_car_tl = p_vp->op[1].tl;

    // Carrier TL を低めに (鳴る)
    if (p_vp->op[1].tl > 0x10) p_vp->op[1].tl = 0x10;
    // Modulator TL を高めに (変調弱化)
    if (p_vp->op[0].tl < 0x24) p_vp->op[0].tl = 0x24;

    // Attack/Decay/Release 最低値引き上げ
    if (p_vp->op[1].ar < 12) p_vp->op[1].ar = 12;
    if (p_vp->op[0].ar < 12) p_vp->op[0].ar = 12;
    if (p_vp->op[1].dr < 4)  p_vp->op[1].dr = 4;
    if (p_vp->op[0].dr < 4)  p_vp->op[0].dr = 4;
    if (p_vp->op[1].rr < 4)  p_vp->op[1].rr = 4;
    if (p_vp->op[0].rr < 4)  p_vp->op[0].rr = 4;
    p_vp->op[0].sl = 0;
    p_vp->op[1].sl = 0;
    
    if (before_car_tl != p_vp->op[1].tl)
        fprintf(stderr,"[AUDIBLE] carTL %u -> %u\n", before_car_tl, p_vp->op[1].tl);

    if (p_opts->debug.verbose)
        fprintf(stderr, "[AUDIBLE] applied (carrierTL<=0x10 modTL>=0x24 AR>=12 DR/RR>=4 SL=0)\n");
}

void apply_audible_sanity (OPL3VoiceParam *p_vp, const CommandOptions *p_opts) {
    if (!p_opts->debug.audible_sanity) return;

    uint8_t orig_car_tl = p_vp->op[1].tl;
    uint8_t orig_mod_tl = p_vp->op[0].tl;
    uint8_t orig_car_ar = p_vp->op[1].ar;
    uint8_t orig_mod_ar = p_vp->op[0].ar;

    /* Carrier: できるだけ聞こえる (TL 小) */
    if (p_vp->op[1].tl > 0x10) p_vp->op[1].tl = 0x10;
    /* Modulator: 変調過剰を避けるためやや弱く (TL 大きめ) */
    if (p_vp->op[0].tl < 0x24) p_vp->op[0].tl = 0x24;

    /* Attack, Decay, Release 強制高速化 */
    if (p_vp->op[1].ar < 12) p_vp->op[1].ar = 12;
    if (p_vp->op[0].ar < 12) p_vp->op[0].ar = 12;
    if (p_vp->op[1].dr < 4)  p_vp->op[1].dr = 4;
    if (p_vp->op[0].dr < 4)  p_vp->op[0].dr = 4;
    if (p_vp->op[1].rr < 4)  p_vp->op[1].rr = 4;
    if (p_vp->op[0].rr < 4)  p_vp->op[0].rr = 4;
    p_vp->op[1].sl = 0;
    p_vp->op[0].sl = 0;

    if (p_opts->debug.verbose) {
        fprintf(stderr,
            "[AUDIBLE] CarrierTL %u->%u ModTL %u->%u CarAR %u->%u ModAR %u->%u\n",
            orig_car_tl, p_vp->op[1].tl, orig_mod_tl, p_vp->op[0].tl,
            orig_car_ar, p_vp->op[1].ar, orig_mod_ar, p_vp->op[0].ar);
    }
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

/* ---- ここに元の static void ym2413_patch_to_opl3_with_fb(...) 実装を丸ごと移植 ----
 *  logger / debug print / override ロジックはそのまま保持
 *  依存シンボルが足りない場合は適宜ヘッダを追加
 */
void ym2413_patch_to_opl3_with_fb(int inst,
                                  const uint8_t *p_ym2413_regs,
                                  OPL3VoiceParam *p_vp, const CommandOptions* p_opts)
{
    memset(p_vp, 0, sizeof(*p_vp));
    const uint8_t *src;
    if (inst == 0 && p_ym2413_regs) src = p_ym2413_regs;
    else if (inst >= 1 && inst <= 15) src = YM2413_VOICES[inst - 1];
    else if (inst >= 16 && inst <= 20) src = YM2413_RHYTHM_VOICES[inst - 16];
    else src = YM2413_VOICES[0];

    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
           inst, src[0], src[1], src[2], src[3]);

    uint8_t raw_mod_ar = (src[2] >> 4) & 0x0F;
    uint8_t raw_mod_dr = src[2] & 0x0F;
    int ofs = 4;
    uint8_t raw_car_ar = (src[ofs + 2] >> 4) & 0x0F;
    uint8_t raw_car_dr = src[ofs + 2] & 0x0F;

    // Modulator
    p_vp->op[0].am   = (src[0] >> 7) & 1;
    p_vp->op[0].vib  = (src[0] >> 6) & 1;
    p_vp->op[0].egt  = (src[0] >> 5) & 1;
    p_vp->op[0].ksr  = (src[0] >> 4) & 1;
    p_vp->op[0].mult = src[0] & 0x0F;
    p_vp->op[0].ksl  = (src[1] >> 6) & 3;
    p_vp->op[0].tl   = src[1] & 0x3F;
    p_vp->op[0].ar   = rate_map_pick(raw_mod_ar);
    p_vp->op[0].dr   = rate_map_pick(raw_mod_dr);
    p_vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    p_vp->op[0].rr   = src[3] & 0x0F;
    p_vp->op[0].ws   = 1;
    p_vp->op[0].ar   = enforce_min_attack(p_vp->op[0].ar, "Mod", inst, 0);

    printf("[DEBUG] YM2413 patch %d -> RateMap(Mod): rawAR=%u->%u rawDR=%u->%u\n",
           inst, raw_mod_ar, p_vp->op[0].ar, raw_mod_dr, p_vp->op[0].dr);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Modulator: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
           inst, p_vp->op[0].am, p_vp->op[0].vib, p_vp->op[0].egt, p_vp->op[0].ksr, p_vp->op[0].mult,
           p_vp->op[0].ksl, p_vp->op[0].tl, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[0].sl, p_vp->op[0].rr, p_vp->op[0].ws);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
           inst, src[ofs + 0], src[ofs + 1], src[ofs + 2], src[ofs + 3]);

    // Carrier
    p_vp->op[1].am   = (src[ofs + 0] >> 7) & 1;
    p_vp->op[1].vib  = (src[ofs + 0] >> 6) & 1;
    p_vp->op[1].egt  = (src[ofs + 0] >> 5) & 1;
    p_vp->op[1].ksr  = (src[ofs + 0] >> 4) & 1;
    p_vp->op[1].mult = src[ofs + 0] & 0x0F;
    p_vp->op[1].ksl  = (src[ofs + 1] >> 6) & 3;
    p_vp->op[1].tl   = src[ofs + 1] & 0x3F;
    p_vp->op[1].ar   = rate_map_pick(raw_car_ar);
    p_vp->op[1].dr   = rate_map_pick(raw_car_dr);
    p_vp->op[1].sl   = (src[ofs + 3] >> 4) & 0x0F;
    p_vp->op[1].rr   = src[ofs + 3] & 0x0F;
    p_vp->op[1].ws   = 0;
    p_vp->op[1].ar   = enforce_min_attack(p_vp->op[1].ar, "Car", inst, 1);

    printf("[DEBUG] YM2413 patch %d -> RateMap(Car): rawAR=%u->%u rawDR=%u->%u\n",
           inst, raw_car_ar, p_vp->op[1].ar, raw_car_dr, p_vp->op[1].dr);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Carrier: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
           inst, p_vp->op[1].am, p_vp->op[1].vib, p_vp->op[1].egt, p_vp->op[1].ksr, p_vp->op[1].mult,
           p_vp->op[1].ksl, p_vp->op[1].tl, p_vp->op[1].ar, p_vp->op[1].dr, p_vp->op[1].sl, p_vp->op[1].rr, p_vp->op[1].ws);

    p_vp->fb[0] = (src[0] & 0x07);
    p_vp->cnt[0] = 0;
    p_vp->is_4op = 0;
    p_vp->voice_no = inst;
    p_vp->source_fmchip = FMCHIP_YM2413;

    printf("[DEBUG] YM2413 patch %d -> OPL3 Feedback/Alg: FB=%d CNT=%d 4OP=%d VOICE_NO=%d\n",
           inst, p_vp->fb[0], p_vp->cnt[0], p_vp->is_4op, inst);

#if OPLL_ENABLE_ENVELOPE_SHAPE_FIX
    maybe_shape_fix(inst, 0, &p_vp->op[0].ar, &p_vp->op[0].dr);
    maybe_shape_fix(inst, 1, &p_vp->op[1].ar, &p_vp->op[1].dr);
#endif

#if OPLL_ENABLE_ARDR_MIN_CLAMP
    if (p_vp->op[0].ar < 2) p_vp->op[0].ar = 2;
    if (p_vp->op[1].ar < 2) p_vp->op[1].ar = 2;
    if (p_vp->op[0].dr < 2) p_vp->op[0].dr = 2;
    if (p_vp->op[1].dr < 2) p_vp->op[1].dr = 2;
#endif

#if OPLL_ENABLE_MOD_TL_ADJ
    {
        int idx = inst; if (idx < 0) idx = 0; if (idx > 15) idx = 15;
        uint8_t raw_tl = p_vp->op[0].tl;
        int add = kModTLAddTable[idx];
        int new_tl = raw_tl + add;
        if (new_tl < 0) new_tl = 0;
        if (new_tl > 63) new_tl = 63;
        if (add != 0) {
            printf("[DEBUG] YM2413 patch %d -> ModTL adjust: raw=%u add=%d final=%d\n",
                   inst, raw_tl, add, new_tl);
            p_vp->op[0].tl = (uint8_t)new_tl;
        }
    }
#endif

    printf("[DEBUG] YM2413 patch %d -> OPL3 Adjusted: Modulator AR=%d DR=%d, Carrier AR=%d DR=%d\n",
           inst, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[1].ar, p_vp->op[1].dr);
    printf("[DEBUG] YM2413 patch %d -> OPL3 Final: Modulator TL=%d, Carrier TL=%d\n",
           inst, p_vp->op[0].tl, p_vp->op[1].tl);
    printf("\n");

    /* ----------------------------------------------------------- */
    (void)inst;
    (void)p_ym2413_regs;
    /* デバッグビルド時に空呼び出しが判るよう最低限 */
    /* ----------------------------------------------------------- */
}

