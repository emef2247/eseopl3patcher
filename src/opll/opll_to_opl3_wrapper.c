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

// 音色（$3n）が未確定のまま来た Note-On を待つ上限（サンプル数）
#ifndef KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES
#define KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES 512
#endif

/* ★追加: Attack Rate 強制下限設定マクロ */
#ifndef OPLL_FORCE_MIN_ATTACK_RATE
#define OPLL_FORCE_MIN_ATTACK_RATE 1   /* 1 だと原音に近いが “遅すぎ無音” リスク残るため 2 推奨 */
#endif

#ifndef OPLL_DEBUG_ENFORCE_MIN_AR
#define OPLL_DEBUG_ENFORCE_MIN_AR 1
#endif

// ★追加: 可変機能フラグ
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


/* ==== ★追加: レートマップモード ====
 * 0 = アイデンティティ (生値)
 * 1 = 旧簡易テーブル (既存)
 * 2 = 校正(CALIB)テーブル（低ARを少し底上げ / 中域平滑）
 */
#ifndef OPLL_RATE_MAP_MODE
#define OPLL_RATE_MAP_MODE 2
#endif

/* ==== ★追加: Attack Rate 最低保証 ==== */
#ifndef OPLL_FORCE_MIN_ATTACK_RATE
#define OPLL_FORCE_MIN_ATTACK_RATE 2
#endif
#ifndef OPLL_DEBUG_ENFORCE_MIN_AR
#define OPLL_DEBUG_ENFORCE_MIN_AR 1
#endif

/* ==== ★追加: Envelope Shape Fix (ARとDRの乖離を抑える) ==== */
#ifndef OPLL_ENABLE_ENVELOPE_SHAPE_FIX
#define OPLL_ENABLE_ENVELOPE_SHAPE_FIX 0
#endif
/* DR - AR >= しきい値 で補正 */
#ifndef OPLL_SHAPE_FIX_DR_GAP_THRESHOLD
#define OPLL_SHAPE_FIX_DR_GAP_THRESHOLD 13
#endif
/* 補正後 DR の上限 (必要なら AR+N まで丸め) */
#ifndef OPLL_SHAPE_FIX_DR_MAX_AFTER
#define OPLL_SHAPE_FIX_DR_MAX_AFTER 12
#endif
#ifndef OPLL_DEBUG_SHAPE_FIX
#define OPLL_DEBUG_SHAPE_FIX 1
#endif

/* 追加デバッグ */
#ifndef OPLL_DEBUG_RATE_PICK
#define OPLL_DEBUG_RATE_PICK 1
#endif

/* ==== ★追加: レートマップモード ====
 * 0 = アイデンティティ (生値)
 * 1 = 旧簡易テーブル (既存)
 * 2 = 校正(CALIB)テーブル（低ARを少し底上げ / 中域平滑）
 */
#ifndef OPLL_RATE_MAP_MODE
#define OPLL_RATE_MAP_MODE 2
#endif

/* ==== ★追加: Attack Rate 最低保証 ==== */
#ifndef OPLL_FORCE_MIN_ATTACK_RATE
#define OPLL_FORCE_MIN_ATTACK_RATE 2
#endif
#ifndef OPLL_DEBUG_ENFORCE_MIN_AR
#define OPLL_DEBUG_ENFORCE_MIN_AR 1
#endif

/* ==== ★追加: Envelope Shape Fix (ARとDRの乖離を抑える) ==== */
#ifndef OPLL_ENABLE_ENVELOPE_SHAPE_FIX
#define OPLL_ENABLE_ENVELOPE_SHAPE_FIX 1
#endif
/* DR - AR >= しきい値 で補正 */
#ifndef OPLL_SHAPE_FIX_DR_GAP_THRESHOLD
#define OPLL_SHAPE_FIX_DR_GAP_THRESHOLD 13
#endif
/* 補正後 DR の上限 (必要なら AR+N まで丸め) */
#ifndef OPLL_SHAPE_FIX_DR_MAX_AFTER
#define OPLL_SHAPE_FIX_DR_MAX_AFTER 12
#endif
#ifndef OPLL_DEBUG_SHAPE_FIX
#define OPLL_DEBUG_SHAPE_FIX 1
#endif

/* 追加デバッグ */
#ifndef OPLL_DEBUG_RATE_PICK
#define OPLL_DEBUG_RATE_PICK 1
#endif

/* ==== 既存RateMap(旧簡易) ==== */
static const uint8_t kOPLLRateToOPL3_SIMPLE[16] = {
    0,1,2,3,5,6,7,8,9,10,11,12,13,14,15,15
};

/* ==== ★追加: CALIB RateMap (暫定調整値)
 * 目標: 低AR域を持ち上げて「立ち上がらず消える」を防止しつつ
 * 中〜高域は OPL3 の 1刻み増加に近似
 * 実測が取れたらここを更新
 */
static const uint8_t kOPLLRateToOPL3_CALIB[16] = {
    /* raw:0..15 */
    2, 3, 4, 5,   /* 0..3   -> 少し底上げ */
    6, 7, 8, 9,   /* 4..7 */
    10,11,12,13,  /* 8..11 */
    14,15,15,15   /* 12..15 (上限飽和) */
};

/* ==== ★追加: Identity RateMap ==== */
static const uint8_t kOPLLRateToOPL3_ID[16] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};

// ★追加: Mod TL 補正テーブル (inst=0(User)～15)
// 値は加算 (0～63範囲にクリップ)。用途: 低TL(=強変調)を少し和らげる等。
// 必要に応じ後でチューニング
static const int8_t kModTLAddTable[16] = {
/*U ,Vi ,Gt ,Pi ,Fl ,Cl ,Ob ,Tr ,Or ,Hr ,SyS,Har,Vib,SyB,AcB,EGt */
  0,  0,  4,  2,  0,  2,  2,  0,  0,  2,  2,  3,  0,  3,  3,  4
};

#define YM2413_NUM_CH 9
#define YM2413_REGS_SIZE 0x40

static uint8_t g_ym2413_regs[YM2413_REGS_SIZE] = {0};
OpllPendingCh g_pend[YM2413_NUM_CH] = {0};
OpllStampCh   g_stamp[YM2413_NUM_CH] = {0};
// Note-On 保留経過
static uint16_t g_pending_on_elapsed[YM2413_NUM_CH] = {0};

struct OPL3State; // 前方宣言

static void ym2413_patch_to_opl3_with_fb(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp);
static inline void flush_channel(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                 int ch, const OPL3VoiceParam *vp, const CommandOptions *opts,
                                 OpllPendingCh* p, OpllStampCh* s);
static inline void flush_channel_keyoff(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
                                        int ch, const OPL3VoiceParam *vp, const CommandOptions *opts,
                                        OpllPendingCh* p, OpllStampCh* s);

/* ------------------------------------------------
 * Utility
 * ------------------------------------------------ */
static inline int ch_from_addr(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return addr - 0x10;
    if (addr >= 0x20 && addr <= 0x28) return addr - 0x20;
    if (addr >= 0x30 && addr <= 0x38) return addr - 0x30;
    return -1;
}
static inline int reg_kind(uint8_t addr) {
    if (addr >= 0x10 && addr <= 0x18) return 1;
    if (addr >= 0x20 && addr <= 0x28) return 2;
    if (addr >= 0x30 && addr <= 0x38) return 3;
    return 0;
}
static inline void stamp_clear(OpllStampCh* s) { memset(s,0,sizeof(*s)); }
static inline void opll_pending_clear(OpllPendingCh* p){ memset(p,0,sizeof(*p)); }


static inline void stamp_array_clear(OpllStampCh* arr, int ch_count) {
    for (int i = 0; i < ch_count; ++i) stamp_clear(&arr[i]);
}
static inline void opll_pending_array_clear(OpllPendingCh* arr, int ch_count) {
    for (int i = 0; i < ch_count; ++i) opll_pending_clear(&arr[i]);
}

/** Calculates OPLL output frequency */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opllfrequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
    return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);
}

/* ------------------------------------------------
 * Volume / TL
 * ------------------------------------------------ */
 // OPLL VOL は 0..15（0最大、15最小）。キャリアの TL は “VOL のみ”で絶対指定する。
static inline uint8_t opll_vol_to_opl3_carrier_tl_abs(uint8_t vol /*0..15*/)
{
    // 約3dB/step 相当。15は特別に 63（ほぼ無音）でクランプ。
    static const uint8_t kVolToTL_Abs[16] = {
        0, 4, 8, 12, 16, 20, 24, 28,
        32, 36, 40, 44, 48, 52, 56, 63
    };
    return kVolToTL_Abs[vol & 0x0F];
}

// 従来の「baseTL + VOLTL」変換（必要ならモジュレータ用に利用）
static uint8_t opll_vol_to_opl3_tl(uint8_t base_tl, uint8_t opll_vol)
{
    // OPLL VOL ≈ 2 dB/step → OPL3 TL(約0.75dB/step) へのおおよそマップ
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

// 0x40（キャリア）に書く値を VOL のみで作る（KSLは楽器のものを維持）
static inline uint8_t make_carrier_40_from_vol(const OPL3VoiceParam *vp, uint8_t reg3n /*$3n*/)
{
    const uint8_t vol = reg3n & 0x0F;
    const uint8_t tl  = opll_vol_to_opl3_carrier_tl_abs(vol);
    const uint8_t ksl_bits = (uint8_t)((vp->op[1].ksl & 0x03) << 6);
    return (uint8_t)(ksl_bits | (tl & 0x3F));
}

/* ------------------------------------------------
 * Pending helpers
 * ------------------------------------------------ */
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

/** Analyze 2n edge */
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

/** Determine if should pend */
static inline bool should_pend(uint8_t addr, uint8_t value,
                               const OpllStampCh* stamp_ch,
                               uint16_t next_wait_samples) {
    const int kind = reg_kind(addr);
    switch (kind) {
        case 1: // $1n
            return stamp_ch && (stamp_ch->ko == false);
        case 3: // $3n
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

/* ------------------------------------------------
 * Freq combine
 * ------------------------------------------------ */
static inline uint8_t opll_to_opl3_an(uint8_t reg1n){ return reg1n; }
static inline uint8_t opll_to_opl3_bn(uint8_t reg2n){
    uint8_t fnum_msb_2b = (reg2n & 0x01);
    uint8_t block_3b    = (reg2n >>1) & 0x07;
    uint8_t ko_bit      = (reg2n & 0x10)?0x20:0x00;
    return (fnum_msb_2b) | (block_3b<<2) | ko_bit;
}

/* ------------------------------------------------
 * Slot mapping
 * ------------------------------------------------ */
static inline uint8_t opl3_local_mod_slot(uint8_t ch_local){
    return (uint8_t)((ch_local%3)+(ch_local/3)*8);
}
static inline uint8_t opl3_local_car_slot(uint8_t ch_local){
    return (uint8_t)(opl3_local_mod_slot(ch_local)+3);
}
static inline uint8_t opl3_opreg_addr(uint8_t base,uint8_t ch_local,int is_carrier){
    uint8_t slot=is_carrier?opl3_local_car_slot(ch_local):opl3_local_mod_slot(ch_local);
    return (uint8_t)(base+slot);
}

/* ------------------------------------------------
 * Rate map selection
 * ------------------------------------------------ */
static inline uint8_t rate_map_pick(uint8_t raw){
#if !OPLL_ENABLE_RATE_MAP
    return raw & 0x0F;
#else
 #if OPLL_RATE_MAP_MODE==0
    uint8_t v=kOPLLRateToOPL3_ID[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=ID raw=%u -> %u\n",raw,v);
    #endif
    return v;
 #elif OPLL_RATE_MAP_MODE==1
    uint8_t v=kOPLLRateToOPL3_SIMPLE[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=SIMPLE raw=%u -> %u\n",raw,v);
    #endif
    return v;
 #else
    uint8_t v=kOPLLRateToOPL3_CALIB[raw & 0x0F];
    #if OPLL_DEBUG_RATE_PICK
    printf("[RATEMAP] MODE=CALIB raw=%u -> %u\n",raw,v);
    #endif
    return v;
 #endif
#endif
}

/* ------------------------------------------------
 * Attack enforce helper
 * ------------------------------------------------ */
static inline uint8_t enforce_min_attack(uint8_t ar,const char* stage,int inst,int op_index){
#if OPLL_FORCE_MIN_ATTACK_RATE > 0
    if(ar < OPLL_FORCE_MIN_ATTACK_RATE){
#if OPLL_DEBUG_ENFORCE_MIN_AR
        printf("[DEBUG] AR-MinClamp inst=%d op=%d %s rawAR=%u -> %u\n",
               inst,op_index,stage,ar,(unsigned)OPLL_FORCE_MIN_ATTACK_RATE);
#endif
        return (uint8_t)OPLL_FORCE_MIN_ATTACK_RATE;
    }
#endif
    return ar;
}


/* ------------------------------------------------
 * Envelope Shape Fix
 * ------------------------------------------------ */
static inline void maybe_shape_fix(int inst, int op_index, uint8_t* ar, uint8_t* dr){
#if OPLL_ENABLE_ENVELOPE_SHAPE_FIX
    if(!ar || !dr) return;
    uint8_t A=*ar, D=*dr;
    if (D > A && (uint8_t)(D - A) >= OPLL_SHAPE_FIX_DR_GAP_THRESHOLD) {
        uint8_t newD = D;
        if (newD > OPLL_SHAPE_FIX_DR_MAX_AFTER) newD = OPLL_SHAPE_FIX_DR_MAX_AFTER;
        if (newD < A) newD = (uint8_t)(A+1);
        if (newD != D) {
#if OPLL_DEBUG_SHAPE_FIX
            printf("[SHAPEFIX] inst=%d op=%d AR=%u DR=%u -> DR'=%u (gap=%u)\n",
                   inst, op_index, A, D, newD, (unsigned)(D-A));
#endif
            *dr = newD;
        }
    }
#endif
}

/* ------------------------------------------------
 * YM2413 patch -> OPL3VoiceParam
 * ------------------------------------------------ */
static void ym2413_patch_to_opl3_with_fb(int inst,const uint8_t *ym2413_regs,OPL3VoiceParam *vp){
    memset(vp,0,sizeof(*vp));
    const uint8_t *src;
    if(inst==0 && ym2413_regs) src=ym2413_regs;
    else if(inst>=1 && inst<=15) src=YM2413_VOICES[inst-1];
    else if(inst>=16 && inst<=20) src=YM2413_RHYTHM_VOICES[inst-16];
    else src=YM2413_VOICES[0];

    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
           inst, src[0],src[1],src[2],src[3]);

    uint8_t raw_mod_ar=(src[2]>>4)&0x0F;
    uint8_t raw_mod_dr= src[2]    &0x0F;
    int ofs=4;
    uint8_t raw_car_ar=(src[ofs+2]>>4)&0x0F;
    uint8_t raw_car_dr= src[ofs+2]    &0x0F;

    // Modulator
    vp->op[0].am  =(src[0]>>7)&1;
    vp->op[0].vib =(src[0]>>6)&1;
    vp->op[0].egt =(src[0]>>5)&1;
    vp->op[0].ksr =(src[0]>>4)&1;
    vp->op[0].mult= src[0]     &0x0F;
    vp->op[0].ksl =(src[1]>>6)&3;
    vp->op[0].tl  = src[1]     &0x3F;
    vp->op[0].ar  = rate_map_pick(raw_mod_ar);
    vp->op[0].dr  = rate_map_pick(raw_mod_dr);
    vp->op[0].sl  =(src[3]>>4)&0x0F;
    vp->op[0].rr  = src[3]     &0x0F;
    vp->op[0].ws  = 1;
    vp->op[0].ar = enforce_min_attack(vp->op[0].ar,"Mod",inst,0);

    printf("[DEBUG] YM2413 patch %d -> RateMap(Mod): rawAR=%u->%u rawDR=%u->%u\n",
           inst,raw_mod_ar,vp->op[0].ar,raw_mod_dr,vp->op[0].dr);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Modulator: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
           inst,
           vp->op[0].am,vp->op[0].vib,vp->op[0].egt,vp->op[0].ksr,vp->op[0].mult,
           vp->op[0].ksl,vp->op[0].tl,vp->op[0].ar,vp->op[0].dr,vp->op[0].sl,vp->op[0].rr,vp->op[0].ws);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Source: %02X %02X %02X %02X\n",
           inst,src[ofs+0],src[ofs+1],src[ofs+2],src[ofs+3]);

    // Carrier
    vp->op[1].am  =(src[ofs+0]>>7)&1;
    vp->op[1].vib =(src[ofs+0]>>6)&1;
    vp->op[1].egt =(src[ofs+0]>>5)&1;
    vp->op[1].ksr =(src[ofs+0]>>4)&1;
    vp->op[1].mult= src[ofs+0]     &0x0F;
    vp->op[1].ksl =(src[ofs+1]>>6)&3;
    vp->op[1].tl  = src[ofs+1]     &0x3F;
    vp->op[1].ar  = rate_map_pick(raw_car_ar);
    vp->op[1].dr  = rate_map_pick(raw_car_dr);
    vp->op[1].sl  =(src[ofs+3]>>4)&0x0F;
    vp->op[1].rr  = src[ofs+3]     &0x0F;
    vp->op[1].ws  = 0;
    vp->op[1].ar = enforce_min_attack(vp->op[1].ar,"Car",inst,1);

    printf("[DEBUG] YM2413 patch %d -> RateMap(Car): rawAR=%u->%u rawDR=%u->%u\n",
           inst,raw_car_ar,vp->op[1].ar,raw_car_dr,vp->op[1].dr);

    printf("[DEBUG] YM2413 patch %d -> OPL3 Carrier: AM=%d VIB=%d EGT=%d KSR=%d MULT=%d KSL=%d TL=%d AR=%d DR=%d SL=%d RR=%d WS=%d\n",
           inst,
           vp->op[1].am,vp->op[1].vib,vp->op[1].egt,vp->op[1].ksr,vp->op[1].mult,
           vp->op[1].ksl,vp->op[1].tl,vp->op[1].ar,vp->op[1].dr,vp->op[1].sl,vp->op[1].rr,vp->op[1].ws);

    vp->fb[0]= (src[0] & 0x07);
    vp->cnt[0]=0;
    vp->is_4op=0;
    vp->voice_no=inst;
    vp->source_fmchip=FMCHIP_YM2413;

    printf("[DEBUG] YM2413 patch %d -> OPL3 Feedback/Alg: FB=%d CNT=%d 4OP=%d VOICE_NO=%d\n",
           inst,vp->fb[0],vp->cnt[0],vp->is_4op,inst);

#if OPLL_ENABLE_ENVELOPE_SHAPE_FIX
    maybe_shape_fix(inst,0,&vp->op[0].ar,&vp->op[0].dr);
    maybe_shape_fix(inst,1,&vp->op[1].ar,&vp->op[1].dr);
#endif

#if OPLL_ENABLE_ARDR_MIN_CLAMP
    if(vp->op[0].ar<2) vp->op[0].ar=2;
    if(vp->op[1].ar<2) vp->op[1].ar=2;
    if(vp->op[0].dr<2) vp->op[0].dr=2;
    if(vp->op[1].dr<2) vp->op[1].dr=2;
#endif

#if OPLL_ENABLE_MOD_TL_ADJ
    {
        int idx=inst; if(idx<0) idx=0; if(idx>15) idx=15;
        uint8_t raw_tl=vp->op[0].tl;
        int add=kModTLAddTable[idx];
        int new_tl=raw_tl+add;
        if(new_tl<0) new_tl=0;
        if(new_tl>63) new_tl=63;
        if(add!=0){
            printf("[DEBUG] YM2413 patch %d -> ModTL adjust: raw=%u add=%d final=%d\n",
                   inst,raw_tl,add,new_tl);
            vp->op[0].tl=(uint8_t)new_tl;
        }
    }
#endif

    printf("[DEBUG] YM2413 patch %d -> OPL3 Adjusted: Modulator AR=%d DR=%d, Carrier AR=%d DR=%d\n",
           inst,vp->op[0].ar,vp->op[0].dr,vp->op[1].ar,vp->op[1].dr);
    printf("[DEBUG] YM2413 patch %d -> OPL3 Final: Modulator TL=%d, Carrier TL=%d\n",
           inst,vp->op[0].tl,vp->op[1].tl);
    printf("\n");
}

/* Global channel 0..17 -> port(0/1), local ch(0..8) */
static inline void opl3_port_and_local(uint8_t ch_global, uint8_t* out_port, uint8_t* out_ch_local) {
    uint8_t port = (ch_global >= 9) ? 1 : 0;
    uint8_t loc  = (uint8_t)(ch_global % 9);
    if (out_port) *out_port = port;
    if (out_ch_local) *out_ch_local = loc;
}


/** OPL3 チャンネルへ音色適用 */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, const CommandOptions *opts) {
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = opl3_opreg_addr(0,ch,0); // slot index (base=0)
    int slot_car = opl3_opreg_addr(0,ch,1); // slot index (base=0)

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

    uint8_t opl3_2n_mod = (uint8_t)((vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, opl3_2n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, opl3_2n_car, opts);

    uint8_t opl3_4n_mod = (uint8_t)(((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F));
    uint8_t opl3_4n_car = (uint8_t)(((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, opl3_4n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, opl3_4n_car, opts);

    uint8_t opl3_6n_mod = (uint8_t)((vp->op[0].ar << 4) | (vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((vp->op[1].ar << 4) | (vp->op[1].dr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, opl3_6n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, opl3_6n_car, opts);

    uint8_t opl3_8n_mod = (uint8_t)((vp->op[0].sl << 4) | (vp->op[0].rr & 0x0F));
    uint8_t opl3_8n_car = (uint8_t)((vp->op[1].sl << 4) | (vp->op[1].rr & 0x0F));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, opl3_8n_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, opl3_8n_car, opts);

    uint8_t c0_val = (uint8_t)(0xC0 | ((vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, c0_val, opts);

    uint8_t opl3_en_mod = (uint8_t)((vp->op[0].ws & 0x07));
    uint8_t opl3_en_car = (uint8_t)((vp->op[1].ws & 0x07));
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, opl3_en_mod, opts);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, opl3_en_car, opts);

    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x20 + slot_mod, opl3_2n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x40 + slot_mod, opl3_4n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x60 + slot_mod, opl3_6n_mod);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x80 + slot_mod, opl3_8n_mod);
    printf("[DEBUG]   Write OPL3 (Ch) 0x%02X: 0x%02X (C0)\n", 0xC0 + ch, c0_val);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0xE0 + slot_mod, opl3_en_mod);

    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x20 + slot_car, opl3_2n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x40 + slot_car, opl3_4n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x60 + slot_car, opl3_6n_car);
    printf("[DEBUG]   Write OPL3 0x%02X: 0x%02X\n", 0x80 + slot_car, opl3_8n_car);
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
    for (int inst = 16; inst <= 20; ++inst) { // ★修正 16..20
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
    opl3_voiceparam_apply(p_music_data, p_vstat, p_state, ch, &vp, opts);

    uint8_t c0 = (uint8_t)(((vp.fb[0] & 0x07) << 1) | (vp.cnt[0] & 0x01));
    c0 |= 0xC0;
    duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, c0, opts);
}

/* ========== エッジ・状態補助 ========== */
static inline bool has_effective_3n(const OpllPendingCh* p, const OpllStampCh* s) {
    return (p && p->has_3n) || (s && s->valid_3n);
}

/* wait 消化時の Note-On 保留タイムアウト監視 */
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

/* ========== KSL/ModTL 簡易実効値デバッグ推定 ========== */
// 簡易: 高ブロックほど TL に + (ksl * 係数) を与えるラフ推定
static inline uint8_t debug_effective_mod_tl(uint8_t raw_tl, uint8_t ksl, uint8_t block) {
    uint8_t add = 0;
    if (block >= 5) add = (uint8_t)(ksl * 2);
    else if (block >= 4) add = (uint8_t)(ksl);
    uint16_t eff = raw_tl + add;
    if (eff > 63) eff = 63;
    return (uint8_t)eff;
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

    uint8_t car_slot = opl3_local_car_slot((uint8_t)ch); // ★修正

    if (e.has_2n && e.note_on_edge) {
        uint8_t reg3n_eff = p->has_3n ? p->reg3n : (s->valid_3n ? s->last_3n : 0x00);
        uint8_t reg2n_eff = p->reg2n; // KeyOn 直前値
        uint8_t reg1n_eff = p->has_1n ? p->reg1n : (s->valid_1n ? s->last_1n : 0x00);
        apply_inst_before_keyon(p_music_data, p_vstat, p_state, ch, reg3n_eff, opts);

        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xA0 + ch, opll_to_opl3_an(reg1n_eff), opts);

        // キャリアTL（VOLのみで絶対指定）
        int8_t inst = (reg3n_eff >> 4) & 0x0F;
        OPL3VoiceParam vp_tmp;
        ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
        uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, reg3n_eff);
        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0x40 + car_slot, car40, opts);
        fprintf(stderr, "[DEBUG] KeyOn carTL write ch=%d slot=%u addr=0x%02X val=0x%02X\n",
                ch, car_slot, 0x40 + car_slot, car40);

#if OPLL_ENABLE_KEYON_DEBUG
        {
            uint16_t fnum = (uint16_t)reg1n_eff | ((reg2n_eff & 0x01) << 8);
            uint8_t block = (reg2n_eff >> 1) & 0x07;
            uint8_t mod_slot = opl3_local_mod_slot((uint8_t)ch);
            uint8_t mod_raw_tl = vp_tmp.op[0].tl;
            uint8_t eff_mod_tl = debug_effective_mod_tl(mod_raw_tl, vp_tmp.op[0].ksl, block);
            uint8_t mod_ksl = vp_tmp.op[0].ksl;
            uint8_t car_raw_tl = vp_tmp.op[1].tl;
            uint8_t car_final_tl = car40 & 0x3F;
            fprintf(stderr,
                "[DEBUG] KeyOnDbg ch=%d inst=%d fnum=%u block=%u modSlot=%u modTLraw=%u modKSL=%u effModTL=%u carTLraw=%u carTLvol=%u FB=%u CNT=%u\n",
                ch, inst, fnum, block, mod_slot, mod_raw_tl, mod_ksl, eff_mod_tl,
                car_raw_tl, car_final_tl, vp_tmp.fb[0], vp_tmp.cnt[0]);
        }
#endif

        duplicate_write_opl3(p_music_data, p_vstat, p_state,
                             0xB0 + ch, opll_to_opl3_bn(reg2n_eff), opts);

    } else if (e.has_2n && e.note_off_edge) {
        uint8_t opl3_bn = opll_to_opl3_bn(opll_make_keyoff(p->reg2n));
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, opl3_bn, opts);

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
                                 0x40 + car_slot, car40, opts);
            fprintf(stderr, "[DEBUG] KeyOff carTL upd ch=%d slot=%u addr=0x%02X val=0x%02X\n",
                    ch, car_slot, 0x40 + car_slot, car40);
        }

    } else {
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
                                 0x40 + car_slot, car40, opts);
            fprintf(stderr, "[DEBUG] Sustain carTL upd ch=%d slot=%u addr=0x%02X val=0x%02X\n",
                    ch, car_slot, 0x40 + car_slot, car40);
        }
        if (need_2n) {
            duplicate_write_opl3(p_music_data, p_vstat, p_state,
                                 0xB0 + ch, opll_to_opl3_bn(p->reg2n), opts);
        }
    }

    // Stamp 更新
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

    // ユーザー音色（$00-$07）は即反映
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
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                g_pending_on_elapsed[ch] = 0;
            } else if (g_stamp[ch].ko) {
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xA0 + ch, opll_to_opl3_an(val), opts);
                g_stamp[ch].last_1n = val; g_stamp[ch].valid_1n = true;
            } else {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }

        } else if (kind == 3) { // $3n
            if (pend_note_on) {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                flush_channel(p_music_data, &p_vgm_context->status, p_state,
                              ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                g_pending_on_elapsed[ch] = 0;
            } else if (g_stamp[ch].ko) {
                int8_t inst = (val >> 4) & 0x0F;
                OPL3VoiceParam vp_tmp;
                ym2413_patch_to_opl3_with_fb(inst, g_ym2413_regs, &vp_tmp);
                uint8_t car40 = make_carrier_40_from_vol(&vp_tmp, val);
                uint8_t car_slot = opl3_local_car_slot((uint8_t)ch);
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0x40 + car_slot, car40, opts);
                fprintf(stderr, "[DEBUG] Live VOL carTL upd ch=%d slot=%u addr=0x%02X val=0x%02X\n",
                        ch, car_slot, 0x40 + car_slot, car40);

                g_stamp[ch].last_3n = val; g_stamp[ch].valid_3n = true;
            } else {
                set_pending_from_opll_write(g_pend, g_stamp, addr, val);
            }

        } else if (kind == 2) { // $2n
            bool ko_next = (val & 0x10) != 0;

            if (!g_stamp[ch].ko && ko_next) {
                if (should_pend(addr, val, &g_stamp[ch], next_wait_samples)) {
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                } else {
                    set_pending_from_opll_write(g_pend, g_stamp, addr, val);
                    if (has_effective_3n(&g_pend[ch], &g_stamp[ch])) {
                        flush_channel(p_music_data, &p_vgm_context->status, p_state,
                                      ch, NULL, opts, &g_pend[ch], &g_stamp[ch]);
                        g_pending_on_elapsed[ch] = 0;
                    }
                }
            } else {
                duplicate_write_opl3(p_music_data, &p_vgm_context->status, p_state,
                                     0xB0 + ch, opll_to_opl3_bn(val), opts);
                g_stamp[ch].last_2n = val; g_stamp[ch].valid_2n = true; g_stamp[ch].ko = ko_next;
            }
        }
    }

    if (is_wait_samples_done == 0 && next_wait_samples > 0) {
        opll_tick_pending_on_elapsed(p_music_data, p_vgm_context, p_state, opts, next_wait_samples);
        vgm_wait_samples(p_music_data, &p_vgm_context->status, next_wait_samples);
        is_wait_samples_done = 1;
    }

    return additional_bytes;
}