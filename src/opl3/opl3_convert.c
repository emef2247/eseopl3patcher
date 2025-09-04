#include "opl3_convert.h"
#include <math.h>
#include <string.h>

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
    int addtional_bytes = 0;

    if (reg == 0x01 || reg == 0x02 || reg == 0x03 || reg == 0x04) {
        // Write only to port 0
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg == 0x05) {
        // Handle mode register (only port 1)
        // Always OPL3 mode
        opl3_write_reg(p_state, p_music_data, 1, 0x05, val & 0x1);
    } else if (reg >= 0x40 && reg <= 0x55) {
        uint8_t val0 = apply_tl_with_ratio(val, v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(val, v_ratio1);
        opl3_write_reg(p_state, p_music_data, 0, reg, val0);
        opl3_write_reg(p_state, p_music_data, 1, reg, val1); addtional_bytes += 3;
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        // Only write port0 for A0..A8
        int ch = reg - 0xA0;

        if ((p_state->reg[0xB0 + ch]) & 0x20) {
            opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, val);
        } else {
            // Only update the register buffer (No dump to vgm)
            p_state->reg_stamp[reg] = p_state->reg[reg];
            p_state->reg[reg] = val;
        }
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        // Only perform voice registration on KeyOn event (when writing to FREQ_MSB and KEYON bit transitions 0->1)
        // Get the previous and new KEYON bit values
        uint8_t prev_val = p_state->reg_stamp[reg];
        uint8_t keyon_prev = prev_val & 0x20;
        uint8_t keyon_new  = val & 0x20;
        // KeyOn occurs: extract and register voice parameters for this channel
        if (!keyon_prev && keyon_new) {
            OPL3VoiceParam vp;
            // Always zero-initialize the whole structure before extracting parameters
            memset(&vp, 0, sizeof(OPL3VoiceParam));
            // Extract voice parameters from the OPL3 state
            extract_voice_param(p_state, &vp);
            // Set additional fields as needed before DB registration
            vp.source_fmchip = p_state->source_fmchip;
            // Register or find voice in the database
            opl3_voice_db_find_or_add(&p_state->voice_db, &vp);
        }
        
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        // forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        int ch = reg - 0xB0;
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, p_state->reg[0xA0 + ch]);
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        // Extra 3 bytes
        addtional_bytes += 3;
        vgm_wait_samples(p_music_data, p_vstat, opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        detune_if_fm(p_state, ch, p_state->reg[0xA0 + ch], val, detune, &detunedA, &detunedB);
        opl3_write_reg(p_state, p_music_data, 1, 0xA0 + ch, detunedA); addtional_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode))) {
            opl3_write_reg(p_state, p_music_data, 1, reg, detunedB); addtional_bytes += 3;
        }
        vgm_wait_samples(p_music_data, p_vstat, opl3_keyon_wait);
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        // Stereo panning implementation based on channel number
        // Even channels: port0->right, port1->left
        // Odd channels: port0->left, port1->right
        // This creates alternating stereo placement for a stereo effect
        uint8_t port0_panning, port1_panning;
        if (ch_panning) {
            // Apply stereo panning
            if (ch % 2 == 0) {
                // Even channel: port0 gets right channel, port1 gets left channel
                port0_panning = 0x50;  // Right channel (bit 4 and bit 6)
                port1_panning = 0xA0;  // Left channel (bit 5 and bit 7)
            } else {
                // Odd channel: port0 gets left channel, port1 gets right channel
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
            }
        } else {
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
        }
        opl3_write_reg(p_state, p_music_data, 0, 0xC0 + ch, (0xF & val) | port0_panning);
        opl3_write_reg(p_state, p_music_data, 1, 0xC0 + ch, (0xF & val) | port1_panning); addtional_bytes += 3;
    } else if (reg == 0xBD) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg >= 0xE0 && reg <= 0xF5) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    } else {
        // Write to both ports
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    }
    return addtional_bytes;
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
