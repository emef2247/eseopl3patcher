#include <stdint.h>
#include <stdbool.h>
#include "vgm_helpers.h"
#include <math.h>

// --- Channel registers state ---
typedef struct {
    uint8_t regA[9];
    uint8_t regB[9];
    uint8_t regC[9];
    bool rhythm_mode;
    bool opl3_mode_initialized;
} OPL3State;

// TL (Total Level) attenuation using v_ratio
uint8_t apply_tl_with_ratio(uint8_t orig_val, double v_ratio) {
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

// --- Detune helper ---
// detune: percentage value (e.g., 2.5 means +2.5% detune, 100.0 is +100%)
// If the user specifies 1.0, interpret as 100% (multiply by 100 internally)
void detune_if_fm(OPL3State *state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *outA, uint8_t *outB) {
    if (ch >= 6 && state->rhythm_mode) {
        *outA = regA;
        *outB = regB;
        return;
    }
    uint16_t fnum = ((regB & 3) << 8) | regA;
    // detune is always interpreted as a percent value (e.g., 0.2 means 0.2%)
    double delta = fnum * (detune / 100.0);
    int fnum_detuned = (int)(fnum + delta + 0.5);

    if (fnum_detuned < 0) fnum_detuned = 0;
    if (fnum_detuned > 1023) fnum_detuned = 1023;

    *outA = (uint8_t)(fnum_detuned & 0xFF);
    *outB = (regB & 0xFC) | ((fnum_detuned >> 8) & 3);
}

// Return: number of bytes output to port1
// v_ratio0/v_ratio1: volume ratio for port0/port1 (0.0~1.0)
int apply_to_ports(dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state, int ch, const char *regType, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1) {
    int port1_bytes = 0;
    if (regType[0] == 'A') {
        if ((state->regB[ch] & 0x20)) {
            forward_write(music_data, 0, 0xA0 + ch, val);
            // port0 only
        }
    } else if (regType[0] == 'B') {
        forward_write(music_data, 0, 0xB0 + ch, val);
        forward_write(music_data, 0, 0xA0 + ch, state->regA[ch]);
        forward_write(music_data, 0, 0xB0 + ch, val);

        vgm_wait_samples(music_data, vstat, opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        detune_if_fm(state, ch, state->regA[ch], state->regB[ch], detune, &detunedA, &detunedB);

        forward_write(music_data, 1, 0xA0 + ch, detunedA); port1_bytes += 3;
        if (!(ch >= 6 && ch <= 8 && state->rhythm_mode)) {
            forward_write(music_data, 1, 0xB0 + ch, detunedB); port1_bytes += 3;
        }
        vgm_wait_samples(music_data, vstat, opl3_keyon_wait);
    } else if (regType[0] == 'C') {
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
    
        forward_write(music_data, 0, 0xC0 + ch, val | port0_panning);
        forward_write(music_data, 1, 0xC0 + ch, val | port1_panning); port1_bytes += 3;
    } else if (regType[0] == 'T') {
        // TL (Total Level) register: apply volume attenuation
        // TL register: 0x40-0x55 (per operator) -- here we assume regType 'T' is used for TL register
        // TL is 6 bits (0-63), lower value = higher volume
        uint8_t val0 = apply_tl_with_ratio(val, v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(val, v_ratio1);
        forward_write(music_data, 0, 0x40 + ch, val0);
        forward_write(music_data, 1, 0x40 + ch, val1); port1_bytes += 3;
    }
    return port1_bytes;
}

// --- Rhythm mode register handler ---
void handle_bd(dynbuffer_t *music_data, OPL3State *state, uint8_t val) {
    state->rhythm_mode = (val & 0x20) != 0;
    forward_write(music_data, 0, 0xbd, val);
}

// --- Main OPL3/OPL2 reg write handler ---
// Return: bytes output to port1
int duplicate_write_opl3(dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state, uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1) {
    int port1_bytes = 0;

    if (reg == 0x01) {
        // TODO: OPL3 mode change
    } else if (reg == 0x05) {
        // TODO: OPL3 mode change
    } else if (reg == 0x02 || reg == 0x03 || reg == 0x04 || reg == 0x08) {
        forward_write(music_data, 0, reg, val);
    } else if (reg >= 0x40 && reg <= 0x55) {
        int ch = reg - 0x40;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "T", val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    } else if (reg == 0xBD) {
        handle_bd(music_data, state, val);
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        int ch = reg - 0xA0;
        state->regA[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "A", val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        int ch = reg - 0xB0;
        state->regB[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "B", val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        state->regC[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "C", val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    } else {
        forward_write(music_data, 0, reg, val);
        forward_write(music_data, 1, reg, val); port1_bytes += 3;
    }
    return port1_bytes;
}

// --- OPL3 initialization sequence ---
void opl3_init(dynbuffer_t *music_data, int stereo_mode) {

    // -------------------------
    // New OPL3 Global Registers (Port1 only)
    // -------------------------
    // 0x104 : 4-Operator Connections
    //   bit0 = Enable 4-OP mode for CH0 + CH3
    //   bit1 = Enable 4-OP mode for CH1 + CH4
    //   bit2 = Enable 4-OP mode for CH2 + CH5
    //   bit3–7 = Reserved
    // 0x105 : OPL3 mode / stereo enable
    //   bit0 = OPL3 enable (must be 1 for OPL3 mode)
    //   bit1 = Left/Right output enable (stereo control)
    //   bit2–7 = Reserved
    forward_write(music_data, 1, 0x05, 0x01);  // OPL3 enable (OPL3 mode set)
    forward_write(music_data, 1, 0x04, 0x00);  // Waveform select (default)

    // --- Port 0 General Initialization ---
    // LSI TEST and Note Select registers
    forward_write(music_data, 0, 0x01, 0x00);  // LSI TEST register (should be 0)
    forward_write(music_data, 0, 0x08, 0x00);  // NTS (Note Select)

    // --- Port 1 LSI TEST Initialization ---
    forward_write(music_data, 1, 0x01, 0x00);  // LSI TEST register (should be 0)

    // -------------------------
    // Channel-Level Control
    // -------------------------
    // 0xC0–0xC8 : Channel control (Feedback & Algorithm)
    //   bit0–2 = Algorithm (carrier/modulator connection)
    //   bit3–5 = Feedback (modulation feedback level)
    //   bit4 = Left (CHA)
    //   bit5 = Right (CHB)
    //   bit6 = Left (CHC)
    //   bit7 = Right (CHD)
    // $C0-$C8: Port0 ch0-ch8, Port1 ch9-ch17
    for (uint8_t i = 0; i < 9; ++i) {
        // Port0: ch = i (0-8)
        if (stereo_mode) {
            uint8_t value0 = (i % 2 == 0) ? 0xA0 : 0x50;
            forward_write(music_data, 0, 0xC0 + i, value0);
        } else {
            forward_write(music_data, 0, 0xC0 + i, 0x50);
        }

        // Port1: ch = i + 9 (9-17)
        if (stereo_mode) {
            uint8_t value1 = ((i + 9) % 2 == 0) ? 0xA0 : 0x50;
            forward_write(music_data, 1, 0xC0 + i, value1);
        } else {
            forward_write(music_data, 1, 0xC0 + i, 0xA0);
        }
    }
    // 0xE0–0xF5 : Waveform Select (0–7)
    //   000 = Sine
    //   001 = Half-sine
    //   010 = Absolute-sine
    //   011 = Quarter-sine
    //   100 = Log-sawtooth
    //   101 = Exp-sawtooth
    //   110 = Square
    //   111 = Derived waveform (complex)
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs)/sizeof(ext_regs[0]); ++i) {
        forward_write(music_data, 0, ext_regs[i], 0x00);  // Port 0
        forward_write(music_data, 1, ext_regs[i], 0x00);  // Port 1
    }

    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        forward_write(music_data, 1, reg, 0x00);
    }
}