#include "opll_to_opl3_map.h"

static inline uint8_t bit(uint8_t v, int b) { return (v >> b) & 1; }
static inline uint8_t fld(uint8_t v, int s, int n) { return (v >> s) & ((1u << n) - 1u); }
static inline uint8_t clamp_u8(int x, int lo, int hi) { return (uint8_t)(x < lo ? lo : (x > hi ? hi : x)); }

/* YM2413 volume nibble(0..15, 0最大) → OPL3 TL(0..63, 0最小) への単純線形換算 */
static inline uint8_t car_volume_to_tl(uint8_t vol_nib) {
    if (vol_nib > 15) vol_nib = 15;
    // 四捨五入: (vol*63)/15
    int tl = (vol_nib * 63 + 7) / 15;
    return clamp_u8(tl, 0, 63);
}

static void fill_op_from_ctrl(OPL3OpParam* op, uint8_t ctrl) {
    op->am   = bit(ctrl,7);
    op->vib  = bit(ctrl,6);
    op->egt  = bit(ctrl,5);
    op->ksr  = bit(ctrl,4);
    op->mult = fld(ctrl,0,4);
}

void opll_to_opl3_map_from_regs(
    uint8_t mod_ctrl, uint8_t car_ctrl, uint8_t carKsl_modTl, uint8_t misc_fb_rect_ksl,
    uint8_t mod_ar_dr, uint8_t car_ar_dr, uint8_t mod_sl_rr, uint8_t car_sl_rr,
    uint8_t volume_nibble, OPL3VoiceParam* out
) {
    if (!out) return;

    // 初期化
    for (int i=0;i<4;i++) {
        out->op[i].am=out->op[i].vib=out->op[i].egt=out->op[i].ksr=out->op[i].mult=0;
        out->op[i].ksl=out->op[i].tl=out->op[i].ar=out->op[i].dr=out->op[i].sl=out->op[i].rr=out->op[i].ws=0;
    }
    out->fb[0]=out->fb[1]=0;
    out->cnt[0]=out->cnt[1]=0;
    out->is_4op = 0;
    out->voice_no = -1;
    out->source_fmchip = 2413; // YM2413の由来であることを示す（運用側で上書き可）

    // op0: モジュレータ
    fill_op_from_ctrl(&out->op[0], mod_ctrl);
    out->op[0].ksl = fld(misc_fb_rect_ksl, 6, 2);       // mod KSL (0..3)
    out->op[0].tl  = fld(carKsl_modTl, 0, 6);           // mod TL  (0..63)
    out->op[0].ar  = fld(mod_ar_dr, 4, 4);
    out->op[0].dr  = fld(mod_ar_dr, 0, 4);
    out->op[0].sl  = fld(mod_sl_rr, 4, 4);
    out->op[0].rr  = fld(mod_sl_rr, 0, 4);
    out->op[0].ws  = bit(misc_fb_rect_ksl, 3) ? 1 : 0;  // rectified → half-sine

    // op1: キャリア
    fill_op_from_ctrl(&out->op[1], car_ctrl);
    out->op[1].ksl = fld(carKsl_modTl, 6, 2);           // car KSL (0..3)
    out->op[1].tl  = car_volume_to_tl(volume_nibble);   // car TL from volume nibble
    out->op[1].ar  = fld(car_ar_dr, 4, 4);
    out->op[1].dr  = fld(car_ar_dr, 0, 4);
    out->op[1].sl  = fld(car_sl_rr, 4, 4);
    out->op[1].rr  = fld(car_sl_rr, 0, 4);
    out->op[1].ws  = bit(misc_fb_rect_ksl, 4) ? 1 : 0;

    // 2-OP前提: アルゴリズムは mod→car（CNT=0）
    out->cnt[0] = 0;
    out->fb[0]  = fld(misc_fb_rect_ksl, 0, 3);          // 0..7
}

void opll_to_opl3_map_from_bytes(const uint8_t inst[8], uint8_t volume_nibble, OPL3VoiceParam* out) {
    if (!inst || !out) return;
    opll_to_opl3_map_from_regs(
        inst[0], inst[1], inst[2], inst[3],
        inst[4], inst[5], inst[6], inst[7],
        volume_nibble, out
    );
}