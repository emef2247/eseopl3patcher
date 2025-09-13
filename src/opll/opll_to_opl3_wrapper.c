#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_convert.h"
#include "ym2413_voice_rom.h"
#include "nukedopll_voice_rom.h"
#include "opll_to_opl3_wrapper.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/**
 * Convert OPLL VOL (0..15) and OPL3 base TL (0..63) to final TL for a slot
 * 0 = maximum volume, 63 = minimum volume
 * @param base_tl  Base TL from OPL3VoiceParam (0..63)
 * @param opll_vol OPLL VOL value (0..15, 0=最大音, 15=最小音)
 * @return final TL (0..63)
 */
/**
 * Convert YM2413 VOL to final OPL3 TL for carrier.
 * Uses minimum(TL, scaled VOL) for natural volume control.
 */
static uint8_t opll_vol_to_opl3_tl(uint8_t base_tl, uint8_t opll_vol)
{
    // Scale VOL 0-15 → 0-63
    uint8_t vol_tl = (opll_vol * 63) / 15;

    // Return smaller value (より大きな音量になる方)
    return (base_tl < vol_tl) ? base_tl : vol_tl;
}


double calc_opll_frequency (double clock, unsigned char block, unsigned short fnum) {
    printf("[DEBUG] calc_opllfrequency: clock=%.0f block=%u fnum=%u\n", clock, block, fnum);
    // f_out = (clock / 72) * 2^(block - 1) * (FNUM / 512)
    // freq = base * 2^(block-1) * (fnum / 512)
    return (clock / 72.0) * ldexp((double)fnum / 512.0, block - 1);
}

/**
 * Apply an OPL3VoiceParam to a specified OPL3 channel by writing the appropriate registers.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus.
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index (0-8).
 * @param vp Pointer to OPL3VoiceParam structure.
 * @param detune Detune amount (percent).
 * @param opl3_keyon_wait KeyOn/Off wait (in samples).
 * @param ch_panning Channel panning mode.
 * @param v_ratio0 Volume ratio for port 0.
 * @param v_ratio1 Volume ratio for port 1.
 * @return Total bytes written to port 1.
 */
int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, double detune, int opl3_keyon_wait,
    int ch_panning, double v_ratio0, double v_ratio1) {
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = ch;
    int slot_car = ch + 3;

    // Modulator
    uint8_t reg20_mod = (vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, reg20_mod, detune, opl3_keyon_wait,ch_panning, v_ratio0, v_ratio1);

    uint8_t reg40_mod = ((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, reg40_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    
    uint8_t reg60_mod = ((vp->op[0].ar & 0x0F) << 4) | (vp->op[0].dr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, reg60_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    uint8_t reg80_mod = ((vp->op[0].sl & 0x0F) << 4) | (vp->op[0].rr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, reg80_mod, detune, opl3_keyon_wait,ch_panning, v_ratio0, v_ratio1);

    uint8_t regE0_mod = (vp->op[0].ws & 0x07);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, regE0_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    // Carrier
    uint8_t reg20_car = (vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, reg20_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    uint8_t reg40_car = ((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, reg40_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    uint8_t reg60_car = ((vp->op[1].ar & 0x0F) << 4) | (vp->op[1].dr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, reg60_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    uint8_t reg80_car = ((vp->op[1].sl & 0x0F) << 4) | (vp->op[1].rr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, reg80_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    uint8_t regE0_car = (vp->op[1].ws & 0x07);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, regE0_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

    // Feedback/connection
    uint8_t regC0 = ((vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, regC0, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    return bytes;
}

/**
 * Convert YM2413 patch to OPL3VoiceParam
 * inst: 0 = user patch, 1..15 = ROM patch
 * ym2413_regs: 8-byte user patch, only valid if inst==0
 */
static void ym2413_patch_to_opl3_with_fb(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp) {
    memset(vp, 0, sizeof(OPL3VoiceParam));
    const uint8_t *src;
    if (inst == 0 && ym2413_regs) {
        src = ym2413_regs; // $00-$07: user patch
    } else {
        src = YM2413_VOICES[inst-1];
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

    // フィードバック値（mod[0] の下位3ビット）
    vp->fb[0] = (src[0] & 0x07);
    // Feedback を少し弱める（要調整）
    //if (vp->fb[0] > 0) vp->fb[0]--;

    vp->cnt[0] = 0;
    vp->is_4op = 0;
    vp->voice_no = inst;
    vp->source_fmchip = FMCHIP_YM2413;

    // --- AR/DR correction ---
    if (vp->op[0].ar < 2) vp->op[0].ar = 2;
    if (vp->op[1].ar < 2) vp->op[1].ar = 2;
    if (vp->op[0].dr < 2) vp->op[0].dr = 2;
    if (vp->op[1].dr < 2) vp->op[1].dr = 2;
    // EG を弱め補正
    // if (vp->op[0].dr > 0) vp->op[0].dr+=2;
    // if (vp->op[1].dr > 0) vp->op[1].dr+=2;
    // if (vp->op[0].rr > 0) vp->op[0].rr+=2;
    // if (vp->op[1].rr > 0) vp->op[1].rr+=2;
}


/**
 * Register all YM2413 ROM patches into OPL3VoiceDB during OPL3 initialization.
 * Each ROM patch is converted to OPL3VoiceParam format and registered in the database.
 * YM2413_VOICES[0] = Violin, [1]=Guitar, ..., [14]=Synth Bass, [15]=User patch (not registered as preset).
 */
 void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db) {
    for (int i = 0; i < 16; ++i) {
        printf("ROM[%d]:", i);
        for (int j = 0; j < 8; ++j) printf(" %02x", YM2413_VOICES[i][j]);
        printf("\n");
    }
    // Register only preset patches [0..14] (Violin..Synth Bass)
    for (int inst = 1; inst <= 15; ++inst) {
        OPL3VoiceParam vp;
        // YM2413プリセット音色からOPL3VoiceParamへ変換（FB値も含む）
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp);
        // voice_dbへ登録
        opl3_voice_db_find_or_add(db, &vp);
    }
}

// --- YM2413→OPL3 conversion state ---
#define YM2413_REGS_SIZE 0x40
static uint8_t ym2413_regs_stamp[YM2413_REGS_SIZE] = {0};
static uint8_t ym2413_regs[YM2413_REGS_SIZE] = {0};

uint8_t opll_make_keyoff (uint8_t val) {
    return (val & 0xEF) | 0x80;
}

// --- YM2413→OPL3 register conversion main ---
int opll_write_register(VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning,double v_ratio0, double v_ratio1) {
    ym2413_regs_stamp[reg] = ym2413_regs[reg];
    ym2413_regs[reg] = val;
    int additional_bytes = 0;

    if (reg <= 0x07 || reg == 0x0E) return 0; // ignore other registers

    // $10-$18: FNUM LSB (buffer only)
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg - 0x10;

        // --- OPLL → fnum/block/keyon 抽出 ---
        // YM2413仕様準拠でレジスタ値を抽出
        uint8_t  fnum_lsb = ym2413_regs[0x10 + ch];
        uint8_t  reg_msb  = ym2413_regs[0x20 + ch];
        uint16_t fnum_9b  = ((reg_msb & 0x01) << 8) | fnum_lsb;   // FNUM[8], FNUM[7:0]
        uint8_t  block    = (reg_msb >> 1) & 0x07;
        uint8_t  keyon    = (reg_msb & 0x10) ? 1 : 0;
        fnum_9b &= 0x1FF; // 9bit
        block   &= 0x07;  // 3bit

        // 範囲チェック
        if (fnum_9b > 511) {
            printf("[WARNING] YM2413 FNUM out of range: %d (ch=%d)\n", fnum_9b, ch);
            fnum_9b &= 0x1FF;
        }
        if (block > 7) {
            printf("[WARNING] YM2413 BLOCK out of range: %d (ch=%d)\n", block, ch);
            block &= 0x07;
        }

        // fnum, blockの値をOPL3用に変換
        double f_opll = calc_opll_frequency(p_vgm_context->source_fm_clock, block, fnum_9b);
        uint8_t inst = (ym2413_regs[0x30 + ch] >> 4) & 0x0F;
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, ym2413_regs, &vp);

        uint8_t opl3_block;
        uint16_t opl3_fnum;
        double err;
        opl3_find_fnum_block_with_ml_cents(f_opll, p_vgm_context->target_fm_clock, &opl3_block, &opl3_fnum, &err, (int)block, vp.op[0].mult, vp.op[1].mult);
        double f_opl3 = calc_opl3_frequency(p_vgm_context->target_fm_clock, opl3_block, opl3_fnum);

        printf("[DEBUG]　f_opll %9.3f |opl3_block %4d opl3_fnum  %4d |f_opl3 %9.3f |err %7.3f\n",
               f_opll,
               opl3_block, opl3_fnum,
               f_opl3,
               err);

        uint8_t out_lsb = opl3_fnum & 0xFF;      // FNUM LSB

        printf("[DEBUG][YM2413 REGS $1n] ch=%d $1n=%02x (fnum_9b=%d, block=%d, keyon=%d)\n", ch, ym2413_regs[0x10 + ch], fnum_9b, block, keyon);
        printf("[DEBUG][YMF262 REGS $An] ch=%d $An=%02x \n", ch, out_lsb);
       additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0xA0 + ch, out_lsb, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio0);
        return additional_bytes;
    }
    // $20-$28: KeyOn/FNUM MSB/Block
    else if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg - 0x20;

        // --- OPLL → fnum/block/keyon 抽出 ---
        // YM2413仕様準拠でレジスタ値を抽出
        uint8_t  fnum_lsb = ym2413_regs[0x10 + ch];
        uint8_t  reg_msb  = ym2413_regs[0x20 + ch];
        uint16_t fnum_9b  = ((reg_msb & 0x01) << 8) | fnum_lsb;   // FNUM[8], FNUM[7:0]
        uint8_t  block    = (reg_msb >> 1) & 0x07;
        uint8_t  keyon    = (reg_msb & 0x10) ? 1 : 0;

        // 範囲チェック
        fnum_9b &= 0x1FF;
        block   &= 0x07;

        // 範囲チェック
        if (fnum_9b > 511) {
            printf("[WARNING] YM2413 FNUM out of range: %d (ch=%d)\n", fnum_9b, ch);
            fnum_9b &= 0x1FF;
        }
        if (block > 7) {
            printf("[WARNING] YM2413 BLOCK out of range: %d (ch=%d)\n\n", block, ch);
            block &= 0x07;
        }


        uint8_t inst = (ym2413_regs[0x30 + ch] >> 4) & 0x0F;
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, ym2413_regs, &vp);

        // --- Patch apply ---
        // --- KeyOff ---
        additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0xB0 + ch, opll_make_keyoff(val), detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
        additional_bytes += opl3_voiceparam_apply(p_music_data, &(p_vgm_context->status), p_state, ch, &vp, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        // --- TL+VOL補正 ---
        uint8_t base_tl = vp.op[1].tl;        // 音色由来のキャリア TL
        uint8_t vol = ym2413_regs[0x30 + ch] & 0x0F;
        uint8_t final_tl = opll_vol_to_opl3_tl(base_tl, vol);
        additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0x30 + ch, final_tl, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        // --- fnum/block変換 ---
        double f_opll = calc_opll_frequency(p_vgm_context->source_fm_clock, block, fnum_9b);
        uint8_t opl3_block;
        uint16_t opl3_fnum;
        double err;
        opl3_find_fnum_block_with_ml_cents(f_opll, p_vgm_context->target_fm_clock, &opl3_block, &opl3_fnum, &err, (int)block,vp.op[0].mult, vp.op[1].mult);
        double f_opl3 = calc_opl3_frequency(p_vgm_context->target_fm_clock, opl3_block, opl3_fnum);

        // --- OPL3のレジスタ出力値 ---
        uint8_t out_msb = ((opl3_fnum >> 8) & 0x03)   // fnum上位2bit
                        | (opl3_block << 2)           // block (3bit)
                        | (keyon ? 0x20 : 0);         // KeyOn

        // デバッグ出力
        printf("[DEBUG][YM2413 REGS $2n] ch=%d inst=%d vol=%d block=%d fnum_9b=%03X keyon=%d freq=%.2fHz\n",
            ch, inst, vol, block, fnum_9b, keyon, f_opll);
        
        // --- KeyOn/Off ---
        printf("[DEBUG][YMF262 REGS $Bn] ch=%d inst=%d vol=%d opl3_block=%d opl3_fnum=%03X keyon=%d freq=%.2fHz\n",
               ch, inst, vol, opl3_block, opl3_fnum, keyon, f_opl3);

        // Write KeyOff
        additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0xB0 + ch, opll_make_keyoff(val), detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        // Write FNUM MSB/Block/KeyOn
        additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0xB0 + ch, out_msb, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
        return additional_bytes;
    }

    // $30-$38: instrument/volume (INST/VOL) immediate handling
    else if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg - 0x30;
        uint8_t inst = (val >> 4) & 0x0F;

        // --- Register user patch if needed ---
        if (inst == 0) {
            // User patchの場合は、現時点でのレジスタ$01-$07を元にOPL3VoiceParamを生成し、voice_dbへ登録する
            // (同じ内容のパッチが既に登録されていればそれを使う)
            OPL3VoiceParam user_vp;
            ym2413_patch_to_opl3_with_fb(inst, ym2413_regs, &user_vp);
            int found = opl3_voice_db_find_or_add(&p_state->voice_db, &user_vp);
            printf("[DEBUG] User patch (INST=0) registered to voice_db (voice_no=%d, found=%d)\n", user_vp.voice_no, found);
        }

        // --- TL+VOL補正（即時反映） ---
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, ym2413_regs, &vp);
        uint8_t base_tl = vp.op[1].tl;        // 音色由来のキャリア TL
        uint8_t vol = ym2413_regs[0x30 + ch] & 0x0F;
        uint8_t final_tl = opll_vol_to_opl3_tl(base_tl, vol);

        // VOL(TL)書き込み
        additional_bytes += duplicate_write_opl3(p_music_data, &(p_vgm_context->status), p_state, 0x40 + ch, ((vp.op[0].ksl & 0x03) << 6) | (final_tl & 0x3F), detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
        printf("[DEBUG] VOL update: ch=%d inst=%d vol=%d -> TL_mod=%d (slot_mod=%d), TL_car=%d (slot_car=%d)\n",
            ch, inst, vol, vp.op[0].tl, ch, vp.op[1].tl, ch + 3);
        return additional_bytes;
    }

    return additional_bytes;
}