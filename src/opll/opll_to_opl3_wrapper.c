#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_convert.h"
#include "ym2413_voice_rom.h"
#include "nukedopll_voice_rom.h"
#include "opll_to_opl3_wrapper.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define OPLL_RHYTHM_BD   (1 << 4)
#define OPLL_RHYTHM_SD   (1 << 3)
#define OPLL_RHYTHM_TOM  (1 << 2)
#define OPLL_RHYTHM_CYM  (1 << 1)
#define OPLL_RHYTHM_HH   (1 << 0)
#define OPLL_RHYTHM_MASK (OPLL_RHYTHM_BD | OPLL_RHYTHM_SD | OPLL_RHYTHM_TOM | OPLL_RHYTHM_CYM | OPLL_RHYTHM_HH)

#ifndef KEYON_WAIT_GRACE_SAMPLES
#define KEYON_WAIT_GRACE_SAMPLES 16
#endif

/* 音色（$3n）が未確定のまま来た Note-On を待つ上限（サンプル数） */
#ifndef KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES
#define KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES 512
#endif

#define YM2413_NUM_CH 9
#define YM2413_REGS_SIZE 0x40

static uint8_t g_ym2413_regs[YM2413_REGS_SIZE] = {0};
OpllPendingCh g_pend[YM2413_NUM_CH] = {0};
OpllStampCh   g_stamp[YM2413_NUM_CH] = {0};
/* Note-On 保留経過 */
static uint16_t g_pending_on_elapsed[YM2413_NUM_CH] = {0};

struct OPL3State; /* 前方宣言（ヘッダで定義されるがここでも明示） */
static void ym2413_patch_to_opl3_with_fb(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp);
static inline void flush_channel(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                 int ch, const OPL3VoiceParam *vp, const CommandOptions *opts,
                                 OpllPendingCh* p, OpllStampCh* s);
static inline void flush_channel_keyoff(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                        int ch, const OPL3VoiceParam *vp, const CommandOptions *opts,
                                        OpllPendingCh* p, OpllStampCh* s);  

/* ========== 小物ユーティリティ ========== */

static inline int ch_from_addr(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return addr - 0x10; /* $1n */
    if (addr >= 0x20 && addr <= 0x28) return addr - 0x20; /* $2n */
    if (addr >= 0x30 && addr <= 0x38) return addr - 0x30; /* $3n */
    return -1;
}
static inline int reg_kind(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return 1; /* $1n */
    if (addr >= 0x20 && addr <= 0x28) return 2; /* $2n */
    if (addr >= 0x30 && addr <= 0x38) return 3; /* $3n */
    return 0;
}

static inline void stamp_clear(OpllStampCh* s) { memset(s, 0, sizeof(*s)); }
static inline void opll_pending_clear(OpllPendingCh* p) { memset(p, 0, sizeof(*p)); }

static inline void stamp_array_clear(OpllStampCh* arr, int ch_count) {
    for (int i = 0; i < ch_count; ++i) stamp_clear(&arr[i]);
}
static inline void opll_pending_array_clear(OpllPendingCh* arr, int ch_count) {
    for (int i = 0; i < ch_count; ++i) opll_pending_clear(&arr[i]);
}

/**
 * Calculates the output frequency for OPLL (YM2413) using block and fnum.
 * f_out = (clock / 72) * 2^(block - 1) * (FNUM / 512)
 */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opllfrequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
    return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);
}

/* ========== VOL/TL 変換 ========== */
/* OPLL VOL は 0..15（0最大、15最小）。キャリアの TL は “VOL のみ”で絶対指定する。 */
static inline uint8_t opll_vol_to_opl3_carrier_tl_abs(uint8_t vol /*0..15*/)
{
    /* 約3dB/step 相当。15は特別に 63（ほぼ無音）でクランプ。 */
    static const uint8_t kVolToTL_Abs[16] = {
        0, 4, 8, 12, 16, 20, 24, 28,
        32, 36, 40, 44, 48, 52, 56, 63
    };
    return kVolToTL_Abs[vol & 0x0F];
}

/* 従来の「baseTL + VOLTL」変換（必要ならモジュレータ用に利用） */
static uint8_t opll_vol_to_opl3_tl(uint8_t base_tl, uint8_t opll_vol)
{
    /* OPLL VOL ≈ 2 dB/step → OPL3 TL(約0.75dB/step) へのおおよそマップ */
    static const uint8_t opll_vol_to_tl[16] = {
        0,  2,  4,  6,
        8, 11, 13, 15,
        17, 19, 21, 23,
        26, 29, 32, 63
    };
    uint8_t vol_tl = opll_vol_to_tl[opll_vol & 0x0F];
    uint16_t tl = (uint16_t)base_tl + vol_tl;
    if (tl > 63) tl = 63;
    return (uint8_t)tl;
}

/* 0x40（キャリア）に書く値を VOL のみで作る（KSLは楽器のものを維持） */
static inline uint8_t make_carrier_40_from_vol(const OPL3VoiceParam *vp, uint8_t reg3n /*$3n*/)
{
    const uint8_t vol = reg3n & 0x0F;
    const uint8_t tl  = opll_vol_to_opl3_carrier_tl_abs(vol);
    const uint8_t ksl_bits = (uint8_t)((vp->op[1].ksl & 0x03) << 6);
    return (uint8_t)(ksl_bits | (tl & 0x3F));
}

/* ========== 出力（実レジスタ書き込み）用コールバック雛形 ========== */
typedef struct {
    void (*write_an)(void* ctx, int ch, uint8_t fnum_lsb);      /* OPL3 0xA0+ch */
    void (*write_bn)(void* ctx, int ch, uint8_t bn_value);      /* OPL3 0xB0+ch */
    void (*apply_3n)(void* ctx, int ch, uint8_t reg3n_value);   /* $3n → OPL3展開 */
} OPLL2OPL3_Out;

/* ========== Pending 取り回し ========== */

static inline void set_pending_from_opll_write_ch(OpllPendingCh* p, const OpllStampCh* s,
                                                  uint8_t addr, uint8_t value) {
    (void)s;
    switch (reg_kind(addr)) {
        case 1: p->has_1n = true; p->reg1n = value; break;
        case 2: p->has_2n = true; p->reg2n = value; break;
        case 3: p->has_3n = true; p->reg3n = value; break;
        default: break;
    }
}
static inline void set_pending_from_opll_write(OpllPendingCh g_pend[], const OpllStampCh g_stamp[],
                                               uint8_t addr, uint8_t value) {
    int ch = ch_from_addr(addr);
    if (ch < 0) return;
    set_pending_from_opll_write_ch(&g_pend[ch], &g_stamp[ch], addr, value);
}

/* 2nのエッジ解析 */
static inline PendingEdgeInfo analyze_pending_edge_ch(const OpllPendingCh* p, const OpllStampCh* s) {
    PendingEdgeInfo info = (PendingEdgeInfo){0};
    if (!p || !s || !p->has_2n) return info;
    bool ko_next = (p->reg2n & 0x10) != 0;
    info.has_2n        = true;
    info.ko_next       = ko_next;
    info.note_on_edge  = (!s->ko && ko_next);
    info.note_off_edge = ( s->ko && !ko_next);
    return info;
}
static inline PendingEdgeInfo analyze_pending_edge_idx(const OpllPendingCh pend[], const OpllStampCh stamp[], int ch) {
    return analyze_pending_edge_ch(&pend[ch], &stamp[ch]);
}

/* $2n を保留する条件（Note-Onで next_wait が短い場合に猶予） */
static inline bool should_pend(uint8_t addr, uint8_t value,
                               const OpllStampCh* stamp_ch,
                               uint16_t next_wait_samples) {
    const int kind = reg_kind(addr);
    switch (kind) {
        case 1: /* $1n: KeyOff中のみ保留 */
            return stamp_ch && (stamp_ch->ko == false);
        case 3: /* $3n: KeyOff中のみ保留 */
            return stamp_ch && (stamp_ch->ko == false);
        case 2: {
            if (!stamp_ch) return false;
            const bool ko_next = (value & 0x10) != 0;
            if (!stamp_ch->ko && ko_next) {
                return (next_wait_samples <= KEYON_WAIT_GRACE_SAMPLES);
            }
            return false;
        }
        default:
            return false;
    }
}

/* ========== OPLL → OPL3 A/B 値の合成ヘルパ ========== */
/* OPLL: $1n = FNUM LSB, $2n: bit0=FNUM MSB(9bit目), bit1-3=BLOCK, bit4=KO
   OPL3: A = FNUM LSB, B = [bit0-1:FNUM MSB(2bit)] | [bit2-4:BLOCK] | [bit5:KO] */
static inline uint8_t opll_to_opl3_an(uint8_t reg1n /* $1n */) {
    return reg1n;
}
static inline uint8_t opll_to_opl3_bn(uint8_t reg2n /* $2n */) {
    uint8_t fnum_msb_2b = (reg2n & 0x01);              /* 1bit → 下位2bitに格納（上位は0） */
    uint8_t block_3b    = (reg2n >> 1) & 0x07;         /* 3bit */
    uint8_t ko_bit      = (reg2n & 0x10) ? 0x20 : 0x00;/* KeyOn bit位置合わせ */
    return (fnum_msb_2b) | (block_3b << 2) | ko_bit;
}

/* 互換のため残す（キャリアTLを baseTL+VOL で作る旧式）。キャリアでは使わない。 */
static inline uint8_t opll_to_opl3_4n(OPL3State *p_state, uint8_t reg3n /* $3n */) {
    int8_t inst = (reg3n >> 4) & 0x0F;
    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp);
    (void)p_state; /* voice_db 参照のために残していたが、ここでは未使用でもよい */
    uint8_t base_tl = vp.op[1].tl;          /* キャリアのベースTL */
    uint8_t vol     = reg3n & 0x0F;
    uint8_t final_tl = opll_vol_to_opl3_tl(base_tl, vol);
    uint8_t ksl_bits = (uint8_t)((vp.op[1].ksl & 0x03) << 6);
    uint8_t opl3_4n  = (uint8_t)(ksl_bits | (final_tl & 0x3F));
    return opl3_4n;
}

/* ========== YM2413 音色 → OPL3 パラメータ ========== */

/**
 * Converts a YM2413 patch to OPL3VoiceParam structure.
 * inst: 0=user patch ($00-$07), 1..15=ROM melodic, 15..19=rhythm bank indices
 */
static void ym2413_patch_to_opl3_with_fb(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp) {
    memset(vp, 0, sizeof(OPL3VoiceParam));
    const uint8_t *src;
    if (inst == 0 && ym2413_regs) {
        src = ym2413_regs; /* User patch ($00-$07) */
    } else if (inst >= 1 && inst <= 15) {
        src = YM2413_VOICES[inst-1];
    } else if (inst >= 16 && inst <= 20) {
        src = YM2413_RHYTHM_VOICES[inst-16];
    } else {
        src = YM2413_VOICES[0]; /* fallback */
    }

    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
        inst,
        src[0], src[1], src[2], src[3]
    );

    /* Modulator */
    vp->op[0].am   = (src[0] >> 7) & 1;
    vp->op[0].vib  = (src[0] >> 6) & 1;
    vp->op[0].egt  = (src[0] >> 5) & 1;
    vp->op[0].ksr  = (src[0] >> 4) & 1;
    vp->op[0].mult =  src[0]       & 0x0F;
    vp->op[0].ksl  = (src[1] >> 6) & 3;
    vp->op[0].tl   =  src[1]       & 0x3F;
    vp->op[0].ar   = (src[2] >> 4) & 0x0F;
    vp->op[0].dr   =  src[2]       & 0x0F;
    vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    vp->op[0].rr   =  src[3]       & 0x0F;
    vp->op[0].ws   = 1;  /* 半サイン（OPLL寄せ） */
    printf("[DEBUG] YM2413 patch %d -> OPL3 Modulator: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
        inst,
        vp->op[0].am, vp->op[0].vib, vp->op[0].egt, vp->op[0].ksr, vp->op[0].mult,
        vp->op[0].ksl, vp->op[0].tl, vp->op[0].ar, vp->op[0].dr, vp->op[0].sl, vp->op[0].rr, vp->op[0].ws
    );  

    int ofs = 4; /* キャリアは4バイト目から */
    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
        inst,
        src[ofs+0], src[ofs+1], src[ofs+2], src[ofs+3]
    );
    /* Carrier */
    vp->op[1].am   = (src[ofs+0] >> 7) & 1;
    vp->op[1].vib  = (src[ofs+0] >> 6) & 1;
    vp->op[1].egt  = (src[ofs+0] >> 5) & 1;
    vp->op[1].ksr  = (src[ofs+0] >> 4) & 1;
    vp->op[1].mult =  src[ofs+0]       & 0x0F;
    vp->op[1].ksl  = (src[ofs+1] >> 6) & 3;
    vp->op[1].tl   =  src[ofs+1]       & 0x3F;
    vp->op[1].ar   = (src[ofs+2] >> 4) & 0x0F;
    vp->op[1].dr   =  src[ofs+2]       & 0x0F;
    vp->op[1].sl   = (src[ofs+3] >> 4) & 0x0F;
    vp->op[1].rr   =  src[ofs+3]       & 0x0F;
    vp->op[1].ws   = 0; /* サイン */
    printf("[DEBUG] YM2413 patch %d -> OPL3 Carrier: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
        inst,
        vp->op[1].am, vp->op[1].vib, vp->op[1].egt, vp->op[1].ksr, vp->op[1].mult,
        vp->op[1].ksl, vp->op[1].tl, vp->op[1].ar, vp->op[1].dr, vp->op[1].sl, vp->op[1].rr, vp->op[1].ws
    );

    /* Feedback/Alg */
    vp->fb[0] = (src[0] & 0x07);
    vp->cnt[0] = 0;
    vp->is_4op = 0;
    vp->voice_no = inst;
    vp->source_fmchip = FMCHIP_YM2413;

    printf("[DEBUG] YM2413 patch %d -> OPL3 Feedback/Alg: FB=%d CNT=%d 4OP=%d VOICE_NO=%d\n",
        inst,
        vp->fb[0], vp->cnt[0], vp->is_4op, inst
    );

    /* 極端なクリックを避ける軽い底上げ */
    if (vp->op[0].ar < 2) vp->op[0].ar = 2;
    if (vp->op[1].ar < 2) vp->op[1].ar = 2;
    if (vp->op[0].dr < 2) vp->op[0].dr = 2;
    if (vp->op[1].dr < 2) vp->op[1].dr = 2;
    printf("[DEBUG] YM2413 patch %d -> OPL3 Adjusted: Modulator AR=%d DR=%d, Carrier AR=%d DR=%d\n",
        inst,
        vp->op[0].ar, vp->op[0].dr,
        vp->op[1].ar, vp->op[1].dr
    );  
    printf("[DEBUG] YM2413 patch %d -> OPL3 Final: Modulator TL=%d, Carrier TL=%d\n",
        inst,
        vp->op[0].tl,
        vp->op[1].tl
    );

    printf("\n");
}

/* Local channel 0..8 -> operator slot index */
static inline uint8_t opl3_local_mod_slot(uint8_t ch_local) {
    return (uint8_t)((ch_local % 3) + (ch_local / 3) * 8);
}
static inline uint8_t opl3_local_car_slot(uint8_t ch_local) {
    return (uint8_t)(opl3_local_mod_slot(ch_local) + 3);
}

/* Global channel 0..17 -> port(0/1), local ch(0..8) */
static inline void opl3_port_and_local(uint8_t ch_global, uint8_t* out_port, uint8_t* out_ch_local) {
    uint8_t port = (ch_global >= 9) ? 1 : 0;
    uint8_t loc  = (uint8_t)(ch_global % 9);
    if (out_port) *out_port = port;
    if (out_ch_local) *out_ch_local = loc;
}

/* Operator-register absolute addr (without port). base = 0x20/0x40/0x60/0x80/0xE0 */
static inline uint8_t opl3_opreg_addr(uint8_t base, uint8_t ch_local, int is_carrier) {
    uint8_t slot = is_carrier ? opl3_local_car_slot(ch_local) : opl3_local_mod_slot(ch_local);
    return (uint8_t)(base + slot);
}

/* Channel-register absolute addr (without port). base = 0xA0/0xB0/0xC0 */
static inline uint8_t opl3_chreg_addr(uint8_t base, uint8_t ch_local) {
    return (uint8_t)(base + ch_local);
}
/* OPL3 チャンネルへ音色適用（モジュレータ/キャリア/EGR/WS/C0） */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *opts) {
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = opl3_opreg_addr(0,ch,0);
    int slot_car = opl3_opreg_addr(0,ch,1);

    printf("[DEBUG] Apply OPL3 VoiceParam to ch=%d\n", ch);
    printf("[DEBUG]   Modulator: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
        vp->op[0].am, vp->op[0].vib, vp->op[0].egt, vp->op[0].ksr, vp->op[0].mult,
        vp->op[0].ksl, vp->op[0].tl, vp->op[0].ar, vp->op[0].dr, vp->op[0].sl, vp->op[0].rr, vp->op[0].ws
    );
    printf("[DEBUG]   Carrier:   AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
        vp->op[1].am, vp->op[1].vib, vp->op[1].egt, vp->op[1].ksr, vp->op[1].mult,
        vp->op[1].ksl, vp->op[1].tl, vp->op[1].ar, vp->op[1].dr, vp->op[1].sl, vp->op[1].rr, vp->op[1].ws
    );
    printf("[DEBUG]   Feedback/Alg: FB=%d CNT=%d 4OP=%d VOICE_NO=%d\n",
        vp->fb[0], vp->cnt[0], vp->is_4op, vp->voice_no
    );
    /* OPL3 0x20 + ch: D7:AM D6:VIB D5:EGT D4:KSR D3-0:MULT */
    uint8_t opl3_2n_mod = (uint8_t)((vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, opl3_2n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, opl3_2n_car, opts);

    /* OPL3 0x40 + ch: D7-6:KSL D5-0:TL */
    uint8_t opl3_4n_mod = (uint8_t)(((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, opl3_4n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, opl3_4n_car, opts);

    /* OPL3 0x60 + ch: D7-4:AR D3-0:DR */
    uint8_t opl3_6n_mod = (uint8_t)((vp->op[0].ar << 4) | (vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((vp->op[1].ar << 4) | (vp->op[1].dr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, opl3_6n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, opl3_6n_car, opts);

    /* OPL3 0x80 + ch: D7-4:SL D3-0:RR */
    uint8_t opl3_8n_mod = (uint8_t)((vp->op[0].sl << 4) | (vp->op[0].rr & 0x0F));
    uint8_t opl3_8n_car = (uint8_t)((vp->op[1].sl << 4) | (vp->op[1].rr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, opl3_8n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, opl3_8n_car, opts);

    /* OPL3 0xC0 + ch: D7-3:unused D2-0:FB D0:CNT */  
    uint8_t opl3_cn_mod = (uint8_t)((0xC0 | (vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01)); /* 4OP=0 */
    uint8_t opl3_cn_car = (uint8_t)((0xC0 | (vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01)); /* 4OP=0 */
   //bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, opl3_cn_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, opl3_cn_car, opts);

    /* OPL3 0xE0 + ch: D7-6:WS D5-0:unused */
    uint8_t opl3_en_mod = (uint8_t)((vp->op[0].ws & 0x07));
    uint8_t opl3_en_car = (uint8_t)((vp->op[1].ws & 0x07));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, opl3_en_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, opl3_en_car, opts);

    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x20 + slot_mod, opl3_2n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x40 + slot_mod, opl3_4n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x60 + slot_mod, opl3_6n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x80 + slot_mod, opl3_8n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0xC0 + slot_mod, opl3_cn_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0xE0 + slot_mod, opl3_en_mod);

    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x20 + slot_car, opl3_2n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x40 + slot_car, opl3_4n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x60 + slot_car, opl3_6n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x80 + slot_car, opl3_8n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0xC0 + ch, opl3_cn_car); 
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0xE0 + slot_car, opl3_en_car);

    return bytes;
}

/* ========== ROM パッチ事前登録 ========== */
void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db) {
    for (int inst = 1; inst <= 15; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp);
        opl3_voice_db_find_or_add(db, &vp);
    }
    for (int inst = 15; inst <= 19; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp);
        opl3_voice_db_find_or_add(db, &vp);
    }
}

/* ========== INST 適用（KeyOn 前） ========== */
static inline void apply_inst_before_keyon(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                           int ch, uint8_t reg3n, const CommandOptions *opts) {
    int8_t inst = (reg3n >> 4) & 0x0F;
    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp);
    /* 音色（0x20/0x40/0x60/0x80/0xE0/0xC0[FB/CNT]）を書き込み */
    opl3_voiceparam_apply(p_music_data, p_vstat, p_state, ch, &vp, opts);
    /* PAN(L/R) は常に ON にしておく（実機/コア差吸収） */
    uint8_t c0 = (uint8_t)(((vp.fb[0] & 0x07) << 1) | (vp.cnt[0] & 0x01));
    c0 |= 0xC0; /* L/R を立てる */
    duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, c0, opts);
}

/* ========== エッジ・状態補助 ========== */
static inline bool has_effective_3n(const OpllPendingCh* p, const OpllStampCh* s) {
    return (p && p->has_3n) || (s && s->valid_3n);
}

/* wait を消化するタイミングで、保留中の Note-On をタイムアウト監視して強制 flush */
static inline void opll_tick_pending_on_elapsed(
    VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,
    const CommandOptions *opts, uint16_t wait_samples)
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
                                      ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    } else {
                        g_pending_on_elapsed[ch] = (uint16_t)elapsed;
                    }
                }
            }
        }
    }
}

/* ========== flush（チャネル単位） ========== */
static inline void flush_channel_ch (
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp_unused, const CommandOptions *opts, OpllPendingCh* p, OpllStampCh* s) {

    bool need_1n = p->has_1n && (!s->valid_1n || p->reg1n != s->last_1n);
    bool need_3n = p->has_3n && (!s->valid_3n || p->reg3n != s->last_3n);
    bool need_2n = p->has_2n && (!s->valid_2n || p->reg2n != s->last_2n);

    PendingEdgeInfo e = analyze_pending_edge_ch(p, s);

    fprintf(stderr,
        "[FLUSH] ch=%d pend{1:%s 3:%s 2:%s} edge{on:%d off:%d}\n",
        ch, p->has_1n?"Y":"n", p->has_3n?"Y":"n", p->has_2n?"Y":"n",
        e.note_on_edge?1:0, e.note_off_edge?1:0);

    if (e.has_2n && e.note_on_edge) {
        /* KeyOn 前に必ず INST を適用（$3n が無ければ直近の s->last_3n を使う） */
        uint8_t reg3n_eff = p->has_3n ? p->reg3n : (s->valid_3n ? s->last_3n : 0x00);
        apply_inst_before_keyon(p_music_data, p_vstat, p_state, ch, reg3n_eff, opts);

        /* A（FNUM LSB） */
        uint8_t reg1n_eff = p->has_1n ? p->reg1n : (s->valid_1n ? s->last_1n : 0x00);
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xA0 + ch, opll_to_opl3_an(reg1n_eff), opts);

        /* キャリアTL（VOLのみで絶対指定）を必ず KeyOn 前に書く */
        {
            int8_t inst = (reg3n_eff >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
            uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, reg3n_eff);
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), car40, opts);
        }

        /* B（FNUM MSB/Block/KeyOn） */
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xB0 + ch, opll_to_opl3_bn(p->reg2n), opts);

    } else if (e.has_2n && e.note_off_edge) {
        /* Note-Off: 先に KeyOff */
        uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p->reg2n));
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, opl3_bn, opts);

        /* 付随の A/3n があれば反映（順不同でOK） */
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), opts);
        }
        if (need_3n) {
            int8_t inst = (p->reg3n >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
            uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, p->reg3n);
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), car40, opts);
        }

    } else {
        /* KO 変化なし */
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), opts);
        }
        if (need_3n) {
            /* 発音中/停止中問わず、VOLのみでキャリアTLを反映（INST変更は次KeyOn） */
            int8_t inst = (p->reg3n >> 4) & 0x0F;
            OPL3VoiceParam vp_tmp;
            ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
            uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, p->reg3n);
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), car40, opts);
        }
        if (need_2n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(p->reg2n), opts);
        }
    }

    /* Stamp 更新と pending クリア */
    if (p->has_1n && need_1n) { s->last_1n = p->reg1n; s->valid_1n = true; }
    if (p->has_3n && need_3n) { s->last_3n = p->reg3n; s->valid_3n = true; }
    if (p->has_2n) { s->last_2n = p->reg2n; s->valid_2n = true; s->ko = (p->reg2n & 0x10) != 0; }
    opll_pending_clear(p);
}

/* 配列+chラッパ */
static inline void flush_channel (
      VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *opts, OpllPendingCh* p, OpllStampCh* s) {
    flush_channel_ch(p_music_data, p_vstat, p_state, ch, vp, opts, p, s);
}

/* Stamp commit（外で使うなら） */
static inline void commit_pending_to_stamp(OpllStampCh stamp[], OpllPendingCh pend[], int ch) {
    if (pend[ch].has_1n) { stamp[ch].last_1n = pend[ch].reg1n; stamp[ch].valid_1n = true; }
    if (pend[ch].has_3n) { stamp[ch].last_3n = pend[ch].reg3n; stamp[ch].valid_3n = true; }
    if (pend[ch].has_2n) {
        stamp[ch].last_2n = pend[ch].reg2n; stamp[ch].valid_2n = true;
        stamp[ch].ko = (pend[ch].reg2n & 0x10) != 0;
    }
    opll_pending_clear(&pend[ch]);
}

/* ========== エントリポイント ========== */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t addr, uint8_t val, uint16_t next_wait_samples,
    const CommandOptions *opts) {

    /* ユーザー音色（$00-$07）は即反映して、保留中の Note-On があれば flush */
    if (addr <= 0x07) {
        g_ym2413_regs[addr] = val;
        for (int c = 0; c < YM2413_NUM_CH; ++c) {
            if (g_pend[c].has_2n && !g_stamp[c].ko && (g_pend[c].reg2n & 0x10)) {
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              c, NULL, opts, &g_pend[c], &g_stamp[c]);
                g_pending_on_elapsed[c] = 0;
            }
        }
        return 0;
    }

    /* 通常レジスタは保存してから処理 */
    g_ym2413_regs[addr] = val;

    int additional_bytes = 0;
    int is_wait_samples_done = (next_wait_samples == 0) ? 1 : 0;

    int ch = ch_from_addr(addr);
    if (ch >= 0) {
        int kind = reg_kind(addr);

        /* この ch に Note-On の保留があるか？ */
        const bool pend_note_on =
            g_pend[ch].has_2n && !g_stamp[ch].ko && ((g_pend[ch].reg2n & 0x10) != 0);

        if (kind == 1) { /* $1n */
            if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                g_pending_on_elapsed[ch] = 0;
            } else if (g_stamp[ch].ko) {
                /* 発音中：即時 A を更新 */
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xA0 + ch, opll_to_opl3_an(val), opts);
                g_stamp[ch].last_1n = val; g_stamp[ch].valid_1n = true;
            } else {
                /* KeyOff 中：保留 */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }

        } else if (kind == 3) { /* $3n（INST/VOL） */
            if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                g_pending_on_elapsed[ch] = 0;
            } else if (g_stamp[ch].ko) {
                /* 発音中：VOL は即時反映（キャリアTLを“VOLのみ”で上書き） */
                int8_t inst = (val >> 4) & 0x0F;
                OPL3VoiceParam vp_tmp;
                ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
                uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, val);
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0x40 + (ch + 3), car40, opts);
                g_stamp[ch].last_3n = val; g_stamp[ch].valid_3n = true;
            } else {
                /* KeyOff 中：保留（Note-Onで flush） */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }

        } else if (kind == 2) { /* $2n（KeyOn/Off, FNUM MSB/BLOCK/KO） */
            bool ko_next = (val & 0x10) != 0;

            if (!g_stamp[ch].ko && ko_next) {
                /* Note-On：短い待ちなら一旦保留し、後続の $1n/$3n 到着で flush */
                if (should_pend(addr, val, &g_stamp[ch], next_wait_samples)) {
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                    /* INST/VOL確定待ち。タイムアウトは wait で監視。 */
                } else {
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);

                    /* INST/VOL が未確定なら保留のままにし、確定していれば即 flush */
                    if (has_effective_3n(&g_pend[ch], &g_stamp[ch])) {
                        flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                      ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    }
                    /* 未確定なら、次の wait まで待機（タイムアウト後に強制flush） */
                }
            } else {
                /* Note-Off または KO 不変：Bn を即時に出力 */
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xB0 + ch, opll_to_opl3_bn(val), opts);
                g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = true; g_stamp[ch].ko = ko_next;
            }
        }
    }

    /* wait 前に、Note-On 保留のタイムアウト監視を回す */
    if (is_wait_samples_done == 0 && next_wait_samples > 0) {
        opll_tick_pending_on_elapsed(p_music_data, p_vgm_context, p_state, opts, next_wait_samples);
        vgm_wait_samples(p_music_data, &p_vgm_context->status, next_wait_samples);
        is_wait_samples_done = 1;
    }

    return additional_bytes;
}