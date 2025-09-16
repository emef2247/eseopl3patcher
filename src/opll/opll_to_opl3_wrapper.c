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
#define YM2413_NUM_CH 9

#define YM2413_REGS_SIZE 0x40
static uint8_t g_ym2413_regs[YM2413_REGS_SIZE] = {0};
OpllPendingCh g_pend[YM2413_NUM_CH] = {0};
OpllStampCh   g_stamp[YM2413_NUM_CH] = {0};

/**
 * Converts OPLL VOL (0..15) and OPL3 base TL (0..63) to the final TL value for a slot.
 * 0 = maximum volume, 63 = minimum volume.
 * @param base_tl  Base TL from OPL3VoiceParam (0..63)
 * @param opll_vol OPLL VOL value (0..15, 0=max volume, 15=min volume)
 * @return final TL (0..63)
 */
static uint8_t opll_vol_to_opl3_tl(uint8_t base_tl, uint8_t opll_vol)
{
    // OPLL VOL is approx 2 dB/step → map to OPL3 TL (0.75 dB/step)
    // 2 dB / 0.75 dB ≈ 2.67 → scale factor
    static const uint8_t opll_vol_to_tl[16] = {
        0,  2,  4,  6,   // 0..3
        8, 11, 13, 15,   // 4..7
        17, 19, 21, 23,  // 8..11
        26, 29, 32, 63   // 12..15 (15=silence)
    };

    uint8_t vol_tl = opll_vol_to_tl[opll_vol];
    uint16_t tl = base_tl + vol_tl;
    if (tl > 63) tl = 63;
    return (uint8_t)tl;
}

/**
 * Calculates the output frequency for OPLL (YM2413) using block and fnum.
 * f_out = (clock / 72) * 2^(block - 1) * (FNUM / 512)
 * @param clock OPLL clock frequency (Hz)
 * @param block Block value (0..7)
 * @param fnum  FNUM value (0..511)
 * @return Frequency in Hz
 */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opllfrequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
    return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);
}

/**
 * Applies an OPL3VoiceParam to a specified OPL3 channel by writing the appropriate registers.
 */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *opts) {
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = ch;
    int slot_car = ch + 3;

    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, ((vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, (((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, (((vp->op[0].ar & 0x0F) << 4) | (vp->op[0].dr & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, (((vp->op[0].sl & 0x0F) << 4) | (vp->op[0].rr & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, (vp->op[0].ws & 0x07), opts);

    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, ((vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, (((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, (((vp->op[1].ar & 0x0F) << 4) | (vp->op[1].dr & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, (((vp->op[1].sl & 0x0F) << 4) | (vp->op[1].rr & 0x0F)), opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, (vp->op[1].ws & 0x07), opts);

    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, (((vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01)), opts);

    return bytes;
}

/**
 * Converts a YM2413 patch to OPL3VoiceParam structure.
 * @param inst         Instrument index (0 = user patch, 1..15 = ROM patch)
 * @param ym2413_regs  Pointer to 8-byte user patch (only used if inst==0)
 * @param vp           Output: OPL3VoiceParam structure
 * Notes:
 *   - For melodic instruments, use YM2413_VOICES[inst-1].
 *   - For rhythm instruments, use YM2413_RHYTHM_VOICES[index].
 */
static void ym2413_patch_to_opl3_with_fb(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp) {
    memset(vp, 0, sizeof(OPL3VoiceParam));
    const uint8_t *src;
    if (inst == 0 && ym2413_regs) {
        src = ym2413_regs; // User patch ($00-$07)
    } else if (inst >= 1 && inst <= 15) {
        src = YM2413_VOICES[inst-1];
    } else if (inst >= 15 && inst <= 19) {
        src = YM2413_RHYTHM_VOICES[inst-15];
    } else {
        src = YM2413_VOICES[0]; // Default to Violin if out of range
    }

    // Modulator
    vp->op[0].am   = (src[0] >> 7) & 1;
    vp->op[0].vib  = (src[0] >> 6) & 1;
    vp->op[0].egt  = (src[0] >> 5) & 1;
    vp->op[0].ksr  = (src[0] >> 4) & 1;
    vp->op[0].mult = src[0] & 0x0F;
    vp->op[0].ksl  = (src[1] >> 6) & 3;
    vp->op[0].tl   = src[1] & 0x3F;
    vp->op[0].ar   = (src[2] >> 4) & 0x0F;
    vp->op[0].dr   = src[2] & 0x0F;
    vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    vp->op[0].rr   = src[3] & 0x0F;
    vp->op[0].ws   = 0;

    // Carrier
    int ofs = 4;
    vp->op[1].am   = (src[ofs+0] >> 7) & 1;
    vp->op[1].vib  = (src[ofs+0] >> 6) & 1;
    vp->op[1].egt  = (src[ofs+0] >> 5) & 1;
    vp->op[1].ksr  = (src[ofs+0] >> 4) & 1;
    vp->op[1].mult = src[ofs+0] & 0x0F;
    vp->op[1].ksl  = (src[ofs+1] >> 6) & 3;
    vp->op[1].tl   = src[ofs+1] & 0x3F;
    vp->op[1].ar   = (src[ofs+2] >> 4) & 0x0F;
    vp->op[1].dr   = src[ofs+2] & 0x0F;
    vp->op[1].sl   = (src[ofs+3] >> 4) & 0x0F;
    vp->op[1].rr   = src[ofs+3] & 0x0F;
    vp->op[1].ws   = 0;

    // Feedback (lowest 3 bits of modulator[0])
    vp->fb[0] = (src[0] & 0x07);

    vp->cnt[0] = 0;
    vp->is_4op = 0;
    vp->voice_no = inst;
    vp->source_fmchip = FMCHIP_YM2413;

    // Ensure AR/DR are not too short (prevents clicks)
    if (vp->op[0].ar < 2) vp->op[0].ar = 2;
    if (vp->op[1].ar < 2) vp->op[1].ar = 2;
    if (vp->op[0].dr < 2) vp->op[0].dr = 2;
    if (vp->op[1].dr < 2) vp->op[1].dr = 2;

    vp->op[1].tl   = 0; // Carrier TL is controlled by VOL register, set to 0 here
}

/**
 * Registers all YM2413 ROM patches (melodic and rhythm) into the OPL3 voice database.
 * Each ROM patch is converted to OPL3VoiceParam format and registered in the database.
 * Melodic: YM2413_VOICES[0..14], Rhythm: YM2413_RHYTHM_VOICES[0..4]
 */
void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db) {
    // Register melodic patches
    for (int inst = 1; inst <= 15; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp);
        opl3_voice_db_find_or_add(db, &vp);
    }
    // Register rhythm patches as voice_id 15-19
    for (int inst = 15; inst <= 19; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp);
        opl3_voice_db_find_or_add(db, &vp);
    }
}



/* 出力（実レジスタ書き込み）用コールバック */
typedef struct {
    /* An: FNUM LSB（OPL3 0xA0+ch 相当） */
    void (*write_an)(void* ctx, int ch, uint8_t fnum_lsb);
    /* Bn: FNUM MSB/Block/KeyOn（OPL3 0xB0+ch 相当） */
    void (*write_bn)(void* ctx, int ch, uint8_t bn_value);
    /* 3n: 音色/音量適用（OPLL $3n を OPL3 の複数レジスタへ展開） */
    void (*apply_3n)(void* ctx, int ch, uint8_t reg3n_value);
} OPLL2OPL3_Out;

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

/* ========== Pending へ格納（coalescing） ========== */
/* ポインタ版（コア実装） */
static inline void set_pending_from_opll_write_ch(OpllPendingCh* p, const OpllStampCh* s,
                                                  uint8_t addr, uint8_t value) {
    (void)s; /* 現状未使用。方針で参照したい場合に使えるよう残す。 */
    switch (reg_kind(addr)) {
        case 1: p->has_1n = true; p->reg1n = value; break;
        case 2: p->has_2n = true; p->reg2n = value; break;
        case 3: p->has_3n = true; p->reg3n = value; break;
        default: break; /* その他のレジスタはここでは扱わない */
    }
}
/* 配列+ch自動抽出版（ラッパ） */
static inline void set_pending_from_opll_write(OpllPendingCh g_pend[], const OpllStampCh g_stamp[],
                                               uint8_t addr, uint8_t value) {
    int ch = ch_from_addr(addr);
    if (ch < 0) return;
    set_pending_from_opll_write_ch(&g_pend[ch], &g_stamp[ch], addr, value);
}

/* ========== エッジ解析 ========== */
/* ポインタ版（コア実装） */
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
/* 配列+ch版（ラッパ） */
static inline PendingEdgeInfo analyze_pending_edge_idx(const OpllPendingCh pend[], const OpllStampCh stamp[], int ch) {
    return analyze_pending_edge_ch(&pend[ch], &stamp[ch]);
}

/* ========== 保留すべきかの判定 ========== */
/* 「まず set_pending_from_opll_write を無条件に呼ぶ」のではなく、
   この条件を満たすときだけ保留に積んでください。 */
/* $2n を保留する条件：
   - KO:0→1 の Note-On で、かつ次の wait が短い（同一/ほぼ同一サンプル内に $1n/$3n が来る見込み） */
static inline bool should_pend(uint8_t addr, uint8_t value,
                               const OpllStampCh* stamp_ch,
                               uint16_t next_wait_samples) {
    const int kind = reg_kind(addr);
    switch (kind) {
        case 1: /* $1n: KeyOff中のみ保留 */
            return stamp_ch && (stamp_ch->ko == false);
        case 3: /* $3n: KeyOff中のみ保留 */
            return stamp_ch && (stamp_ch->ko == false);
        case 2: { /* $2n: Note-On を短時間だけ保留して $1n/$3n を先に適用する */
            if (!stamp_ch) return false;
            const bool ko_next = (value & 0x10) != 0;
            if (!stamp_ch->ko && ko_next) {
                return (next_wait_samples <= KEYON_WAIT_GRACE_SAMPLES);
            }
            return false; /* Note-Off/KO不変は即時 */
        }
        default:
            return false;
    }
}


/* ========== OPLL → OPL3 A/B 値の合成ヘルパ ========== */
/* OPLL: $1n = FNUM LSB, $2n: bit0=FNUM MSB(9bit目), bit1-3=BLOCK, bit4=KO
   OPL3: A = FNUM LSB, B = [bit0-1:FNUM MSB(2bit)] | [bit2-4:BLOCK] | [bit5:KO] */
static inline uint8_t opll_to_opl3_an(uint8_t reg1n /* $1n */) {
    return reg1n; /* そのまま */
}
static inline uint8_t opll_to_opl3_bn(uint8_t reg2n /* $2n */) {
    uint8_t fnum_msb_2b = (reg2n & 0x01);              /* 1bit → 2bitに収める（上位は0） */
    uint8_t block_3b    = (reg2n >> 1) & 0x07;         /* 3bit */
    uint8_t ko_bit      = (reg2n & 0x10) ? 0x20 : 0x00;/* 位置をOPL3に合わせる */
    return (fnum_msb_2b) | (block_3b << 2) | ko_bit;
}

// 既存。TLの合成を修正（KSLはキャリアop[1]、ビット合成は OR）
static inline uint8_t opll_to_opl3_4n(OPL3State *p_state, uint8_t reg3n /* $3n */) {
    int8_t inst = (reg3n >> 4) & 0x0F;

    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp);
    int found = opl3_voice_db_find_or_add(&p_state->voice_db, &vp);
    printf("[DEBUG] opll_to_opl3_4n: inst=%d -> voice_id=%d\n", inst, found);

    uint8_t base_tl = vp.op[1].tl;          // キャリアのベースTL
    uint8_t vol     = reg3n & 0x0F;
    uint8_t final_tl = opll_vol_to_opl3_tl(base_tl, vol);

    // 0x40 レジスタ: [7:6]=KSL, [5:0]=TL
    uint8_t ksl_bits = (uint8_t)((vp.op[1].ksl & 0x03) << 6);
    uint8_t opl3_4n  = (uint8_t)(ksl_bits | (final_tl & 0x3F));
    return opl3_4n;
}

/* ========== flush（チャネル単位） ========== */
/* p: 対象チャネルの Pending（入力＆クリア対象）
   s: 対象チャネルの Stamp（出力後に更新）
   out: 出力コールバック
   ctx: 出力側の文脈（VGMBuffer等） */

// 追加: Note-On 前に OPL3 へ音色を適用するヘルパ
static inline void apply_inst_before_keyon(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                           int ch, uint8_t reg3n, const CommandOptions *opts) {
    int8_t inst = (reg3n >> 4) & 0x0F;
    OPL3VoiceParam vp;
    ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp);
    // 音色（0x20/0x40/0x60/0x80/0xE0/0xC0）を書き込み
    opl3_voiceparam_apply(p_music_data, p_vstat, p_state, ch, &vp, opts);
}

// 既存 flush_channel_ch の Note-On 分岐を修正
static inline void flush_channel_ch (
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp_unused, const CommandOptions *opts, OpllPendingCh* p, OpllStampCh* s) {

    bool need_1n = p->has_1n && (!s->valid_1n || p->reg1n != s->last_1n);
    bool need_3n = p->has_3n && (!s->valid_3n || p->reg3n != s->last_3n);
    bool need_2n = p->has_2n && (!s->valid_2n || p->reg2n != s->last_2n);

    PendingEdgeInfo e = analyze_pending_edge_ch(p, s);

    if (e.has_2n && e.note_on_edge) {
        // 1) KeyOn 前に必ず INST を適用（$3n が無ければ直近の s->last_3n を使う手もある）
        uint8_t reg3n_for_inst = p->has_3n ? p->reg3n : (s->valid_3n ? s->last_3n : 0x00);
        apply_inst_before_keyon(p_music_data, p_vstat, p_state, ch, reg3n_for_inst, opts);

        // 2) FNUM LSB（A）→ キャリアTL（4n）→ FNUM MSB/Block/KO（B）
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), opts);
        }
        if (need_3n) {
            uint8_t opl3_4n = opll_to_opl3_4n(p_state, p->reg3n);
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), opl3_4n, opts);
        }
        uint8_t opl3_bn = opll_to_opl3_bn(p->reg2n);
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xB0 + ch, opl3_bn, opts);

    } else if (e.has_2n && e.note_off_edge) {
        // Note-Off: 先に KeyOff
        uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p->reg2n));
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, opl3_bn, opts);
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), opts);
        }
        if (need_3n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), opll_to_opl3_4n(p_state, p->reg3n), opts);
        }

    } else {
        // KO 変化なし
        if (need_1n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xA0 + ch, opll_to_opl3_an(p->reg1n), opts);
        }
        if (need_3n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0x40 + (ch + 3), opll_to_opl3_4n(p_state, p->reg3n), opts);
        }
        if (need_2n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(p->reg2n), opts);
        }
    }

    // Stamp 更新と pending クリア（既存通り）
    if (p->has_1n && need_1n) { s->last_1n = p->reg1n; s->valid_1n = true; }
    if (p->has_3n && need_3n) { s->last_3n = p->reg3n; s->valid_3n = true; }
    if (p->has_2n) { s->last_2n = p->reg2n; s->valid_2n = true; s->ko = (p->reg2n & 0x10) != 0; }
    opll_pending_clear(p);
}

/* 配列+ch版（ラッパ） */
static inline void flush_channel (
      VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *opts,OpllPendingCh* p, OpllStampCh* s) {
    flush_channel_ch(p_music_data, p_vstat, p_state, ch, vp, opts, p, s);
}


/* ========== Stamp へのコミット専用ヘルパ（必要なら） ========== */
/* flush_channel_ch() 内で既に行っているため、外部で分離したい場合のみ使用してください。 */
static inline void commit_pending_to_stamp(OpllStampCh stamp[], OpllPendingCh pend[], int ch) {
    if (pend[ch].has_1n) { stamp[ch].last_1n = pend[ch].reg1n; stamp[ch].valid_1n = true; }
    if (pend[ch].has_3n) { stamp[ch].last_3n = pend[ch].reg3n; stamp[ch].valid_3n = true; }
    if (pend[ch].has_2n) {
        stamp[ch].last_2n = pend[ch].reg2n; stamp[ch].valid_2n = true;
        stamp[ch].ko = (pend[ch].reg2n & 0x10) != 0;
    }
    opll_pending_clear(&pend[ch]);
}


/**
 * OPLL to OPL3 register conversion entrypoint.
 * All OPL3 command options are passed as CommandOptions.
 */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t addr, uint8_t val, uint16_t next_wait_samples,
    const CommandOptions *opts) {

    g_ym2413_regs[addr] = val;
    int additional_bytes = 0;
    int is_wait_samples_done = (next_wait_samples == 0) ? 1 : 0;

    int ch = ch_from_addr(addr);
    if (ch >= 0) {
        int kind = reg_kind(addr);

        /* 便利なフラグ：この ch に Note-On の保留があるか？ */
        const bool pend_note_on =
            g_pend[ch].has_2n && !g_stamp[ch].ko && ((g_pend[ch].reg2n & 0x10) != 0);

        if (kind == 1) { /* $1n */
            if (pend_note_on) {
                /* Note-On 保留中：$1n を積んで即 flush（INST/VOL/FNUM → 最後に KeyOn） */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
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
                /* Note-On 保留中：$3n を積んで即 flush（INST/VOL を先に適用してから KeyOn） */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
            } else if (g_stamp[ch].ko) {
                /* 発音中：VOL は即時反映（キャリアTL）。INSTを完全反映したい場合は次KeyOnで反映する設計でも可 */
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0x40 + (ch + 3), opll_to_opl3_4n(p_state, val), opts);
                g_stamp[ch].last_3n = val; g_stamp[ch].valid_3n = true;
            } else {
                /* KeyOff 中：保留 */
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }

        } else if (kind == 2) { /* $2n（KeyOn/Off, FNUM MSB/BLOCK/KO） */
            bool ko_next = (val & 0x10) != 0;

            if (!g_stamp[ch].ko && ko_next) {
                /* Note-On：短い待ちなら一旦保留し、後続の $1n/$3n 到着で flush */
                if (should_pend(addr, val, &g_stamp[ch], next_wait_samples)) {
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                    /* ここでは flush しない（INST/VOL/FNUM を先に反映するため） */
                } else {
                    /* 待ちが長い等：即 flush（保留済み 1n/3n があれば先に適用される） */
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                    flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                  ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                }
            } else {
                /* Note-Off または KO 不変：Bn を即時に出力 */
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xB0 + ch, opll_to_opl3_bn(val), opts);
                g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = true; g_stamp[ch].ko = ko_next;
            }
        }
    }

    

    // Wait phase after register writes
    if (is_wait_samples_done == 0 && next_wait_samples > 0) {
        printf("[DEBUG] Wait %u samples (no channel-specific wait) after the command addr:0x%02X val:0x%02x\n\n", next_wait_samples, addr, val);
        vgm_wait_samples(p_music_data, &p_vgm_context->status, next_wait_samples);
        is_wait_samples_done = 1;
    }
    return additional_bytes;
}