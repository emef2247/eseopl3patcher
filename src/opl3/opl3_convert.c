#include "opl3_convert.h"
#include "../opll/ym2413_voice_rom.h"      // OPLL_PATCH, OPLL_PRESETS
#include <math.h>
#include <string.h>


/**
 * Convert YM2413 ROM patch data to two OPL3OperatorParam structures (modulator/carrier).
 * This extracts the parameters for both operators from the given patch index.
 *
 * @param patch_index YM2413 patch index (0..15)
 * @param mod Pointer to OPL3OperatorParam for modulator (output)
 * @param car Pointer to OPL3OperatorParam for carrier (output)
 */
void convert_opll_patch_to_opl3_param(int patch_index, OPL3OperatorParam *mod, OPL3OperatorParam *car) {
    // Check patch index bounds and pointer validity
    if (patch_index < 0 || patch_index > 15 || !mod || !car) {
        memset(mod, 0, sizeof(OPL3OperatorParam));
        memset(car, 0, sizeof(OPL3OperatorParam));
        return;
    }

    // YM2413_VOICES[patch_index] is 16 bytes: mod[0..7], car[8..15]
    const unsigned char *rom = YM2413_VOICES[patch_index];

    // Modulator operator extraction
    mod->am   = (rom[0] >> 7) & 0x01;
    mod->vib  = (rom[0] >> 6) & 0x01;
    mod->egt  = (rom[0] >> 5) & 0x01;
    mod->ksr  = (rom[0] >> 4) & 0x01;
    mod->mult = rom[0] & 0x0F;
    mod->ksl  = (rom[1] >> 6) & 0x03;
    mod->tl   = rom[1] & 0x3F; // TL is lower 6 bits
    mod->ar   = (rom[2] >> 4) & 0x0F;
    mod->dr   = rom[2] & 0x0F;
    mod->sl   = (rom[3] >> 4) & 0x0F;
    mod->rr   = rom[3] & 0x0F;
    mod->ws   = 0; // YM2413 has no waveform select

    // Carrier operator extraction
    car->am   = (rom[8] >> 7) & 0x01;
    car->vib  = (rom[8] >> 6) & 0x01;
    car->egt  = (rom[8] >> 5) & 0x01;
    car->ksr  = (rom[8] >> 4) & 0x01;
    car->mult = rom[8] & 0x0F;
    car->ksl  = (rom[9] >> 6) & 0x03;
    car->tl   = rom[9] & 0x3F; // TL is lower 6 bits
    car->ar   = (rom[10] >> 4) & 0x0F;
    car->dr   = rom[10] & 0x0F;
    car->sl   = (rom[11] >> 4) & 0x0F;
    car->rr   = rom[11] & 0x0F;
    car->ws   = 0;
}

/**
 * Register all YM2413 ROM patches into OPL3VoiceDB during OPL3 initialization.
 * Each ROM patch is converted to OPL3VoiceParam format and registered in the database.
 *
 * @param db Pointer to OPL3VoiceDB to register voices into
 */
static void register_all_opll_patches_to_opl3_voice_db(OPL3VoiceDB *db) {
    for (int i = 0; i < 16; ++i) {
        OPL3VoiceParam vp;
        memset(&vp, 0, sizeof(vp));
        // Convert YM2413 ROM patch to OPL3 operator parameters
        convert_opll_patch_to_opl3_param(i, &vp.op[0], &vp.op[1]);
        vp.is_4op = OPL3_MODE_2OP;
        vp.fb[0]  = 0; // YM2413 has no feedback/algorithm info in ROM, set to 0 or use external table if available
        vp.cnt[0] = 0;
        vp.source_fmchip = FMCHIP_YM2413;
        vp.voice_no = i;
        opl3_voice_db_find_or_add(db, &vp);
    }
}


/**
 * Attenuate TL (Total Level) using volume ratio.
 * @param orig_val Original TL register value.
 * @param v_ratio Volume ratio (0.0 .. 1.0).
 * @return TL value modified by ratio.
 */
static uint8_t apply_tl_with_ratio(uint8_t orig_val, double v_ratio) {
    if (v_ratio == 1.0) return orig_val;
    // TL is lower 6 bits
    uint8_t tl = orig_val & 0x3F;
    double dB = 0.0;
    if (v_ratio < 1.0) {
        dB = -20.0 * log10(v_ratio); // 20*log10(ratio) [dB]
    }
    // 1 step = 0.75dB
    int add = (int)(dB / 0.75 + 0.5); // to round off
    int new_tl = tl + add;
    if (new_tl > 63) new_tl = 63;
    return (orig_val & 0xC0) | (new_tl & 0x3F);
}

/**
 * Write a value to the OPL3 register mirror and update internal state flags.
 * Always writes to the register mirror (reg[]).
 * @param p_state Pointer to OPL3State.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param port OPL3 port (0 or 1).
 * @param reg Register address.
 * @param value Value to write.
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value) {
    int reg_addr = reg + (port ? 0x100 : 0x000);
    p_state->reg_stamp[reg_addr] = p_state->reg[reg_addr];
    p_state->reg[reg_addr] = value;
    // Update rhythm mode or OPL3 mode initialized flags if needed
    if (reg_addr == 0x105) {
        p_state->opl3_mode_initialized = (value & 0x01) != 0;
    }
    if (reg_addr == 0x0BD) {
        p_state->rhythm_mode = (value & 0x20) != 0;
    }
    // Write to VGM stream
    forward_write(p_music_data, port, reg, value);
}

/**
 * Detune helper for FM channels (used for frequency detune effects).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @param regA Frequency LSB.
 * @param regB Frequency MSB.
 * @param detune Detune amount (percent).
 * @param p_outA Pointer to output detuned LSB.
 * @param p_outB Pointer to output detuned MSB.
 */
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB) {
    // Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode
    if ((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode)) {
        *p_outA = regA;
        *p_outB = regB;
        return;
    }
    uint16_t fnum = ((regB & 3) << 8) | regA;
    double delta = fnum * (detune / 100.0);
    int fnum_detuned = (int)(fnum + delta + 0.5);

    if (fnum_detuned < 0) fnum_detuned = 0;
    if (fnum_detuned > 1023) fnum_detuned = 1023;

    *p_outA = (uint8_t)(fnum_detuned & 0xFF);
    *p_outB = (regB & 0xFC) | ((fnum_detuned >> 8) & 3);
}

/**
 * Judge OPL3 register type based on YM2413 register address.
 * Used to map YM2413 register writes to OPL3 register conversion logic.
 *
 * YM2413 register map reference:
 *  0x00-0x07: Modulator operator parameters (ch 0-8)
 *  0x10-0x18: Carrier operator parameters (ch 0-8)
 *  0x20-0x28: Frequency (F-Number) LSB (ch 0-8)
 *  0x30-0x38: KeyOn/Block/F-Number MSB (ch 0-8)
 *  0x0E: Rhythm mode / BD
 *  Others: Control, etc.
 *
 * This function maps the typical YM2413 register writes to the corresponding
 * OPL3 register types for conversion purposes.
 *
 * @param ym2413_reg YM2413 register address.
 * @return OPL3 register type (enum value).
 */
opl3_regtype_t opl3_judge_regtype_from_ym2413(uint8_t ym2413_reg) {
    if ((ym2413_reg >= 0x20 && ym2413_reg <= 0x28)) {
        // Frequency LSB for channel
        return OPL3_REGTYPE_FREQ_LSB;
    }
    if ((ym2413_reg >= 0x30 && ym2413_reg <= 0x38)) {
        // KeyOn/Block/F-Number MSB for channel
        return OPL3_REGTYPE_FREQ_MSB;
    }
    if (ym2413_reg == 0x0E) {
        // Rhythm mode / BD register
        return OPL3_REGTYPE_RHYTHM_BD;
    }
    if ((ym2413_reg >= 0x00 && ym2413_reg <= 0x07) ||
        (ym2413_reg >= 0x10 && ym2413_reg <= 0x18)) {
        // Operator parameters (modulator/carrier) - treat as OP_TL for volume and envelope control
        return OPL3_REGTYPE_OP_TL;
    }
    // Control registers, stereo, or other
    return OPL3_REGTYPE_OTHER;
}

/**
 * Main OPL3 register write handler for YM2413 register mapping (OPLL to OPL3).
 * This uses opl3_judge_regtype_from_ym2413 to convert YM2413 reg space to OPL3 regtype.
 */
int duplicate_write_opl3_ym2413(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
) {
    int port1_bytes = 0;
    int ch = -1;
    opl3_regtype_t reg_type = opl3_judge_regtype_from_ym2413(reg);

    // (以下、duplicate_write_opl3と同じロジックでreg_typeを使う)
    if (reg_type == OPL3_REGTYPE_FREQ_LSB) {
        ch = reg - 0x20;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_FREQ_MSB) {
        ch = reg - 0x30;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_CH_CTRL) {
        // YM2413にはこの区分は少ないが、必要なら実装
    } else if (reg_type == OPL3_REGTYPE_OP_TL) {
        ch = (reg >= 0x10) ? (reg - 0x10) : (reg - 0x00); // mod/car判定
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_RHYTHM_BD) {
        handle_bd(p_music_data, p_state, val);
    } else {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val);
        port1_bytes += 3;
    }
    return port1_bytes;
}

/**
 * Judge OPL3 register type based on register offset.
 * @param reg Register address.
 * @return OPL3 register type enum.
 */
opl3_regtype_t opl3_judge_regtype(uint8_t reg) {
    if (reg >= 0xA0 && reg <= 0xA8) return OPL3_REGTYPE_FREQ_LSB;
    if (reg >= 0xB0 && reg <= 0xB8) return OPL3_REGTYPE_FREQ_MSB;
    if (reg >= 0xC0 && reg <= 0xC8) return OPL3_REGTYPE_CH_CTRL;
    if (reg >= 0x40 && reg <= 0x55) return OPL3_REGTYPE_OP_TL;
    if (reg == 0xBD) return OPL3_REGTYPE_RHYTHM_BD;
    return OPL3_REGTYPE_OTHER;
}

/**
 * Apply register value to OPL3/OPL2 ports (used for multi-port/stereo applications).
 * Uses context struct for all relevant info.
 * @param ctx Pointer to opl3_convert_ctx_t with conversion context.
 * @return Number of bytes written to port 1.
 */
int opl3_apply_to_ports(const opl3_convert_ctx_t *ctx) {
    int port1_bytes = 0;
    const int ch = ctx->ch;

 
    // Only perform voice registration on KeyOn event (when writing to FREQ_MSB and KEYON bit transitions 0->1)
    if (ctx->reg_type == OPL3_REGTYPE_FREQ_MSB) {
        // Get the previous and new KEYON bit values
        uint8_t prev_val = ctx->p_state->reg_stamp[0xB0 + ch];
        uint8_t keyon_prev = prev_val & 0x20;
        uint8_t keyon_new  = ctx->val & 0x20;
        // KeyOn occurs: extract and register voice parameters for this channel
        if (!keyon_prev && keyon_new) {
            OPL3VoiceParam vp;
            // Always zero-initialize the whole structure before extracting parameters
            memset(&vp, 0, sizeof(OPL3VoiceParam));
            // Extract voice parameters for the current channel from the OPL3 state
            extract_voice_param(ctx->p_state, ch, &vp);
            // Set additional fields as needed before DB registration
            vp.source_fmchip = ctx->p_state->source_fmchip;
            // Register or find voice in the database
            opl3_voice_db_find_or_add(&ctx->p_state->voice_db, &vp);

        }
    }


    if (ctx->reg_type == OPL3_REGTYPE_FREQ_LSB) {
        // Only write port0 for A0..A8
        if ((ctx->p_state->reg[0xB0 + ch]) & 0x20) {
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xA0 + ch, ctx->val);
        }
    } else if (ctx->reg_type == OPL3_REGTYPE_FREQ_MSB) {
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        // forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xA0 + ch, ctx->p_state->reg[0xA0 + ch]);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xB0 + ch, ctx->val);

        vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        detune_if_fm(ctx->p_state, ch, ctx->p_state->reg[0xA0 + ch], ctx->p_state->reg[0xB0 + ch], ctx->detune, &detunedA, &detunedB);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0xA0 + ch, detunedA); port1_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && ctx->p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && ctx->p_state->rhythm_mode))) {
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0xB0 + ch, detunedB); port1_bytes += 3;
        }
        vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);
    } else if (ctx->reg_type == OPL3_REGTYPE_CH_CTRL) {
        uint8_t port0_panning, port1_panning;
        // Stereo panning (alternating for stereo effect)
        if (ctx->ch_panning) {
            if (ch % 2 == 0) {
                port0_panning = 0x50;
                port1_panning = 0xA0;
            } else {
                port0_panning = 0xA0;
                port1_panning = 0x50;
            }
        } else {
            port0_panning = 0xA0;
            port1_panning = 0x50;
        }
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xC0 + ch, ctx->val | port0_panning);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0xC0 + ch, ctx->val | port1_panning); port1_bytes += 3;
    } else if (ctx->reg_type == OPL3_REGTYPE_OP_TL) {
        uint8_t val0 = apply_tl_with_ratio(ctx->val, ctx->v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(ctx->val, ctx->v_ratio1);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0x40 + ch, val0);
        opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0x40 + ch, val1); port1_bytes += 3;
    }
    return port1_bytes;
}

/**
 * Rhythm mode (BD) register handler.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_state Pointer to OPL3State.
 * @param val Value for rhythm mode register.
 */
void handle_bd(VGMBuffer *p_music_data, OPL3State *p_state, uint8_t val) {
    p_state->rhythm_mode = (val & 0x20) != 0;
    opl3_write_reg(p_state, p_music_data, 0, 0xBD, val);
}

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus.
 * @param p_state Pointer to OPL3State.
 * @param reg Register address.
 * @param val Value to write.
 * @param detune Detune amount.
 * @param opl3_keyon_wait KeyOn/Off wait (in samples).
 * @param ch_panning Channel panning mode.
 * @param v_ratio0 Volume ratio for port 0.
 * @param v_ratio1 Volume ratio for port 1.
 * @return Bytes written to port 1.
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
) {
    int port1_bytes = 0;
    int ch = -1;
    opl3_regtype_t reg_type = opl3_judge_regtype(reg);

    if (reg_type == OPL3_REGTYPE_FREQ_LSB) {
        ch = reg - 0xA0;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_FREQ_MSB) {
        ch = reg - 0xB0;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_CH_CTRL) {
        ch = reg - 0xC0;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_OP_TL) {
        ch = reg - 0x40;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += opl3_apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_RHYTHM_BD) {
        handle_bd(p_music_data, p_state, val);
    } else {
        // All other registers: mirror to both ports
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val);
        port1_bytes += 3;
    }
    return port1_bytes;
}

/**
 * OPL3 initialization sequence for both ports.
 * Sets FM chip type in OPL3State and initializes register mirror.
 */
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip) {
    if (!p_state) return;
    memset(p_state->reg, 0, sizeof(p_state->reg));
    memset(p_state->reg_stamp, 0, sizeof(p_state->reg_stamp));
    p_state->rhythm_mode = false;
    p_state->opl3_mode_initialized = false;
    p_state->source_fmchip = source_fmchip;

     // Initialize OPL3VoiceDB
    opl3_voice_db_init(&p_state->voice_db);

    if (source_fmchip == FMCHIP_YM2413) {
        // Register all OPLL presets as OPL3 voices (for patch lookup)
        register_all_opll_patches_to_opl3_voice_db(&p_state->voice_db);
    }

    // OPL3 global registers (Port 1 only)
    opl3_write_reg(p_state, p_music_data, 1, 0x05, 0x01);  // OPL3 enable
    opl3_write_reg(p_state, p_music_data, 1, 0x04, 0x00);  // Waveform select

    // Port 0 general init
    opl3_write_reg(p_state, p_music_data, 0, 0x01, 0x00);  // LSI TEST
    opl3_write_reg(p_state, p_music_data, 0, 0x08, 0x00);  // NTS

    // Port 1 general init
    opl3_write_reg(p_state, p_music_data, 1, 0x01, 0x00);

    // Channel-level control
    for (uint8_t i = 0; i < 9; ++i) {
        // Port 0: channels 0-8
        if (stereo_mode) {
            uint8_t value0 = (i % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, value0);
        } else {
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, 0x50);
        }
        // Port 1: channels 9-17 (registers 0xC0-0xC8)
        if (stereo_mode) {
            uint8_t value1 = ((i + 9) % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, value1);
        } else {
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, 0xA0);
        }
    }

    // Waveform select registers (port 0 and 1)
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs) / sizeof(ext_regs[0]); ++i) {
        opl3_write_reg(p_state, p_music_data, 0, ext_regs[i], 0x00);
        opl3_write_reg(p_state, p_music_data, 1, ext_regs[i], 0x00);
    }
    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        opl3_write_reg(p_state, p_music_data, 1, reg, 0x00);
    }

}
