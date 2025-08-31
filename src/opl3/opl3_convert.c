#include "opl3_convert.h"
#include <math.h>
#include <string.h>

/**
 * Helper: Attenuate TL (Total Level) using volume ratio.
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
 * This always writes to the register mirror (reg[]).
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value) {
    int reg_addr = reg + (port ? 0x100 : 0x000);
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
 * Judge OPL3 register type based on register offset.
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
 * Returns: number of bytes written to port 1.
 */
int apply_to_ports(const opl3_convert_ctx_t *ctx) {
    int port1_bytes = 0;
    const int ch = ctx->ch;
    if (ctx->reg_type == OPL3_REGTYPE_FREQ_LSB) {
        // Only write port0 for A0..A8
        forward_write(ctx->p_music_data, 0, 0xA0 + ch, ctx->val);
    } else if (ctx->reg_type == OPL3_REGTYPE_FREQ_MSB) {
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        forward_write(ctx->p_music_data, 0, 0xA0 + ch, ctx->p_state->reg[0xA0 + ch]);
        forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);

        vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        detune_if_fm(ctx->p_state, ch, ctx->p_state->reg[0xA0 + ch], ctx->p_state->reg[0xB0 + ch], ctx->detune, &detunedA, &detunedB);

        forward_write(ctx->p_music_data, 1, 0xA0 + ch, detunedA); port1_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && ctx->p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && ctx->p_state->rhythm_mode))) {
            forward_write(ctx->p_music_data, 1, 0xB0 + ch, detunedB); port1_bytes += 3;
        }
        vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);
    } else if (ctx->reg_type == OPL3_REGTYPE_CH_CTRL) {
        // Stereo panning (alternating for stereo effect)
        uint8_t port0_panning, port1_panning;
        if (ctx->ch_panning) {
            if (ch % 2 == 0) {
                port0_panning = 0x50;  // Right channel
                port1_panning = 0xA0;  // Left channel
            } else {
                port0_panning = 0xA0;  // Left channel
                port1_panning = 0x50;  // Right channel
            }
        } else {
            port0_panning = 0xA0;  // Left channel
            port1_panning = 0x50;  // Right channel
        }
        forward_write(ctx->p_music_data, 0, 0xC0 + ch, ctx->val | port0_panning);
        forward_write(ctx->p_music_data, 1, 0xC0 + ch, ctx->val | port1_panning); port1_bytes += 3;
    } else if (ctx->reg_type == OPL3_REGTYPE_OP_TL) {
        uint8_t val0 = apply_tl_with_ratio(ctx->val, ctx->v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(ctx->val, ctx->v_ratio1);
        forward_write(ctx->p_music_data, 0, 0x40 + ch, val0);
        forward_write(ctx->p_music_data, 1, 0x40 + ch, val1); port1_bytes += 3;
    }
    return port1_bytes;
}

/**
 * Rhythm mode (BD) register handler.
 */
void handle_bd(VGMBuffer *p_music_data, OPL3State *p_state, uint8_t val) {
    p_state->rhythm_mode = (val & 0x20) != 0;
    opl3_write_reg(p_state, p_music_data, 0, 0xBD, val);
}

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * Returns: bytes written to port 1.
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
        port1_bytes += apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_FREQ_MSB) {
        ch = reg - 0xB0;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_CH_CTRL) {
        ch = reg - 0xC0;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_OP_TL) {
        ch = reg - 0x40;
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_convert_ctx_t ctx = {p_music_data, p_vstat, p_state, ch, reg_type, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1};
        port1_bytes += apply_to_ports(&ctx);
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
    p_state->rhythm_mode = false;
    p_state->opl3_mode_initialized = false;
    p_state->source_fmchip = source_fmchip;

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

/**
 * Convert OPLL preset instrument to OPL3 voice parameters and register as a voice.
 * This is a stub for compatibility; implementation depends on OPLL/OPL3 patch mapping.
 */
int register_opll_patch_as_opl3_voice(OPL3State *state, int inst) {
    // Stub: no actual patch database mapping here
    // Return error for now
    (void)state;
    (void)inst;
    return -1;
}