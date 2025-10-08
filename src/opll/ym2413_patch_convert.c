#include <string.h>
#include <stdio.h>
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include "../override_apply.h"
#include "../override_loader.h"
#include "../compat_bool.h"
#include "ym2413_patch_convert.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static const uint8_t fnum_to_atten[16] = {0,24,32,37,40,43,45,47,48,50,51,52,53,54,55,56};
static const float rate_to_attack_time[16] = {2826.24f,2260.99f,1888.43f,1577.74f,1318.52f,1102.96f,921.98f,770.38f,644.21f,539.54f,452.28f,379.99f,319.84f,269.51f,227.15f,191.20f};
static const float rate_to_decay_time[16]  = {2260.99f,1888.43f,1577.74f,1318.52f,1102.96f,921.98f,770.38f,644.21f,539.54f,452.28f,379.99f,319.84f,269.51f,227.15f,191.20f,160.08f};

// ...必要に応じてKSR/ksl補正も考慮

// 3. AR/DR補正ロジックの流れ例
// rate: 0～15, keycode, ksr等の補正を省略した簡易例
// OPLL/OPL3のrate値から「実ms」をそれぞれ計算
float get_attack_time_OPLL(int ar) { return rate_to_attack_time[ar & 0xF]; }
float get_attack_time_OPL3(int ar) { return rate_to_attack_time[ar & 0xF]; }
float get_decay_time_OPLL(int dr)  { return rate_to_decay_time[dr & 0xF]; }
float get_decay_time_OPL3(int dr)  { return rate_to_decay_time[dr & 0xF]; }

// OPLL値で得たmsに一番近いOPL3値を逆引き
int find_best_rate_OPL3(float target_ms, const float* table) {
    int best = 0;
    float min_diff = 1e9;
    for (int i = 0; i < 16; ++i) {
        float diff = fabsf(table[i] - target_ms);
        if (diff < min_diff) { min_diff = diff; best = i; }
    }
    return best;
}

// 4. 補正関数案（block/fnum4, ar/dr を引数で渡す）
// --- ksl: dB算出 ---
static int ksl_db(int ksl, int block, int fnum4) {
    if (ksl == 0) return 0;
    int atten = fnum_to_atten[fnum4 & 0xF] - 8 * (block ^ 7);
    if (atten < 0) atten = 0;
    return atten << ksl;
}

// --- AR/DR: ms算出 ---
static float ar_ms(int ar) { return rate_to_attack_time[ar & 0xF]; }
static float dr_ms(int dr) { return rate_to_decay_time[dr & 0xF]; }
static int best_rate(float target_ms, const float* tbl) {
    int best = 0; float min_diff = 1e9;
    for (int i = 0; i < 16; ++i) {
        float diff = fabsf(tbl[i] - target_ms);
        if (diff < min_diff) { min_diff = diff; best = i; }
    }
    return best;
}
// ---- ksl補正 ----
static int best_ksl_for_db(int db, int block, int fnum4) {
    int best = 0, min_diff = 1e9;
    for (int ksl=0; ksl<4; ++ksl) {
        int db3 = ksl_db(ksl, block, fnum4);
        int diff = abs(db - db3);
        if (diff < min_diff) { min_diff = diff; best = ksl; }
    }
    return best;
}

// ---- TL補正（簡易: 0-15→0-63スケール） ----
static uint8_t convert_tl(uint8_t tl_opll) {
    return (uint8_t)((tl_opll * 63 + 7) / 15);
}

// ---- multiple補正 ----
static const int mult_table[16] = {1,2,4,6,8,10,12,14,16,18,20,20,24,24,30,30}; // 実効値x2
static int best_mult(int mult_opll) {
    int v = mult_table[mult_opll & 0xF];
    int best = 0, min_diff = 1e9;
    for (int m=0; m<16; ++m) {
        int v3 = mult_table[m];
        int diff = abs(v - v3);
        if (diff < min_diff) { min_diff = diff; best = m; }
    }
    return best;
}

// ---- 補正本体 ----
void correct_opl3_voice_param(OPL3VoiceParam *p_vp, int block_opll, int fnum4_opll, int block_opl3, int fnum4_opl3) {
    for (int op=0; op<2; ++op) {
        // ksl補正
        int db = ksl_db(p_vp->op[op].ksl, block_opll, fnum4_opll);
        p_vp->op[op].ksl = best_ksl_for_db(db, block_opl3, fnum4_opl3);

        // TL補正
        p_vp->op[op].tl = convert_tl(p_vp->op[op].tl);

        // multiple補正
        p_vp->op[op].mult = best_mult(p_vp->op[op].mult);

        // AR/DR補正
        float ar_target = ar_ms(p_vp->op[op].ar);
        float dr_target = dr_ms(p_vp->op[op].dr);
        p_vp->op[op].ar = best_rate(ar_target, rate_to_attack_time);
        p_vp->op[op].dr = best_rate(dr_target, rate_to_decay_time);
    }
}
    
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
