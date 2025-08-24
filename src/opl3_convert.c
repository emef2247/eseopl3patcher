#include <stdint.h>
#include <stdbool.h>
#include "vgm_helpers.h"

// --- Channel registers state ---
typedef struct {
    uint8_t regA[9];
    uint8_t regB[9];
    uint8_t regC[9];
    bool rhythm_mode;
    bool opl3_mode_initialized;
} OPL3State;

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

// --- Port writes for reg types ---
// Return: number of bytes output to port1
int apply_to_ports(dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state, int ch, const char *regType, uint8_t val, double detune, int opl3_keyon_wait) {
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
        
        if (ch % 2 == 0) {
            // Even channel: port0 gets right channel, port1 gets left channel
            port0_panning = 0x20;  // Right channel (bit 5)
            port1_panning = 0x10;  // Left channel (bit 4)
        } else {
            // Odd channel: port0 gets left channel, port1 gets right channel
            port0_panning = 0x10;  // Left channel (bit 4)
            port1_panning = 0x20;  // Right channel (bit 5)
        }
        
        forward_write(music_data, 0, 0xC0 + ch, val | port0_panning);
        forward_write(music_data, 1, 0xC0 + ch, val | port1_panning); port1_bytes += 3;
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
int duplicate_write_opl3(dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state, uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait) {
    int port1_bytes = 0;
    if (reg == 0x01) {
        // TODO: OPL3 mode change
    } else if (reg == 0x05) {
        // TODO: OPL3 mode change
    } else if (reg == 0x02 || reg == 0x03 || reg == 0x04 || reg == 0x08) {
        forward_write(music_data, 0, reg, val);
    } else if (reg == 0xBD) {
        handle_bd(music_data, state, val);
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        int ch = reg - 0xA0;
        state->regA[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "A", val, detune, opl3_keyon_wait);
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        int ch = reg - 0xB0;
        state->regB[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "B", val, detune, opl3_keyon_wait);
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        state->regC[ch] = val;
        port1_bytes += apply_to_ports(music_data, vstat, state, ch, "C", val, detune, opl3_keyon_wait);
    } else {
        forward_write(music_data, 0, reg, val);
        forward_write(music_data, 1, reg, val); port1_bytes += 3;
    }
    return port1_bytes;
}

// --- OPL3 initialization sequence ---
void opl3_init(dynbuffer_t *music_data) {

    // Port 1 initialization (missing in old code)
    forward_write(music_data, 1, 0x05, 0x01);  // Set register 0x05 on port 1
    forward_write(music_data, 1, 0x04, 0x00);  // Set register 0x04 on port 1

    // --- Port 0 Initialization ---
    // $01: LSI TEST register (should be 0 in normal operation)
    forward_write(music_data, 0, 0x01, 0x00);
    // $08: NTS (Note Select); selects F-NUMBER LSB source
    forward_write(music_data, 0, 0x08, 0x00);

    // $20-$35: Operator registers (AM, VIB, EGT, KSR, MULT)
    forward_write(music_data, 0, 0x20, 0x21); // OP1: AM=0, VIB=0, EGT=1, KSR=0, MULT=1
    forward_write(music_data, 0, 0x21, 0x06); // OP2: AM=0, VIB=0, EGT=0, KSR=1, MULT=6
    forward_write(music_data, 0, 0x22, 0x31);
    forward_write(music_data, 0, 0x23, 0x31);
    forward_write(music_data, 0, 0x24, 0x02);
    forward_write(music_data, 0, 0x25, 0x61);
    forward_write(music_data, 0, 0x28, 0x31);
    forward_write(music_data, 0, 0x29, 0x00);
    forward_write(music_data, 0, 0x2A, 0x31);
    forward_write(music_data, 0, 0x2B, 0x61);
    forward_write(music_data, 0, 0x2C, 0x00);
    forward_write(music_data, 0, 0x2D, 0x32);
    forward_write(music_data, 0, 0x30, 0x31);
    forward_write(music_data, 0, 0x31, 0x31);
    forward_write(music_data, 0, 0x32, 0x07);
    forward_write(music_data, 0, 0x33, 0x31);
    forward_write(music_data, 0, 0x34, 0x32);
    forward_write(music_data, 0, 0x35, 0x02);

    // $40-$45: Modulation/Volume registers (KSL, TL)
    forward_write(music_data, 0, 0x40, 0x3F); // OP1: KSL=0, TL=0x3F (max attenuation)
    forward_write(music_data, 0, 0x41, 0x3F);
    forward_write(music_data, 0, 0x42, 0x8F);
    forward_write(music_data, 0, 0x43, 0x3F);
    forward_write(music_data, 0, 0x44, 0x3F);
    forward_write(music_data, 0, 0x45, 0x06);

    // $48-$4D: More operator setup (KSL, TL)
    forward_write(music_data, 0, 0x48, 0x3F);
    forward_write(music_data, 0, 0x49, 0x3F);
    forward_write(music_data, 0, 0x4A, 0x3F);
    forward_write(music_data, 0, 0x4B, 0x3F);
    forward_write(music_data, 0, 0x4C, 0x3F);
    forward_write(music_data, 0, 0x4D, 0x3F);

    // $50-$55: Output amplitude (KSL, TL)
    forward_write(music_data, 0, 0x50, 0x3F);
    forward_write(music_data, 0, 0x51, 0x3F);
    forward_write(music_data, 0, 0x52, 0x3F);
    forward_write(music_data, 0, 0x53, 0x3F);
    forward_write(music_data, 0, 0x54, 0x3F);
    forward_write(music_data, 0, 0x55, 0x3F);

    // $60-$75: Frequency envelope (AR/DR)
    forward_write(music_data, 0, 0x60, 0xF4); // OP1: AR=0xF, DR=0x4
    forward_write(music_data, 0, 0x61, 0xF5);
    forward_write(music_data, 0, 0x62, 0x93);
    forward_write(music_data, 0, 0x63, 0xF1);
    forward_write(music_data, 0, 0x64, 0xF5);
    forward_write(music_data, 0, 0x65, 0x72);
    forward_write(music_data, 0, 0x68, 0x93);
    forward_write(music_data, 0, 0x69, 0xFC);
    forward_write(music_data, 0, 0x6A, 0x51);
    forward_write(music_data, 0, 0x6B, 0x72);
    forward_write(music_data, 0, 0x6C, 0xFA);
    forward_write(music_data, 0, 0x6D, 0x71);
    forward_write(music_data, 0, 0x70, 0xF4);
    forward_write(music_data, 0, 0x71, 0x51);
    forward_write(music_data, 0, 0x72, 0xEC);
    forward_write(music_data, 0, 0x73, 0xF1);
    forward_write(music_data, 0, 0x74, 0x71);
    forward_write(music_data, 0, 0x75, 0xF8);

    // $80-$95: Envelope SL/RR (Sustain/Release Rate)
    forward_write(music_data, 0, 0x80, 0xE8);
    forward_write(music_data, 0, 0x81, 0x0C);
    forward_write(music_data, 0, 0x82, 0x02);
    forward_write(music_data, 0, 0x83, 0x78);
    forward_write(music_data, 0, 0x84, 0x08);
    forward_write(music_data, 0, 0x85, 0x0B);
    forward_write(music_data, 0, 0x88, 0x02);
    forward_write(music_data, 0, 0x89, 0x05);
    forward_write(music_data, 0, 0x8A, 0x28);
    forward_write(music_data, 0, 0x8B, 0x0B);
    forward_write(music_data, 0, 0x8C, 0x17);
    forward_write(music_data, 0, 0x8D, 0x48);

    // $90-$95: LFO / waveform setup
    forward_write(music_data, 0, 0x90, 0xE8);
    forward_write(music_data, 0, 0x91, 0x28);
    forward_write(music_data, 0, 0x92, 0x26);
    forward_write(music_data, 0, 0x93, 0x78);
    forward_write(music_data, 0, 0x94, 0x48);
    forward_write(music_data, 0, 0x95, 0x16);

    // $A0-$A8: Key on/off & channel setup (F-Number LSB)
    forward_write(music_data, 0, 0xA0, 0x01);
    forward_write(music_data, 0, 0xA1, 0xAD);
    forward_write(music_data, 0, 0xA2, 0x87);
    forward_write(music_data, 0, 0xA3, 0x01);
    forward_write(music_data, 0, 0xA4, 0xAD);
    forward_write(music_data, 0, 0xA5, 0x01);
    forward_write(music_data, 0, 0xA6, 0x01);
    forward_write(music_data, 0, 0xA7, 0x01);
    forward_write(music_data, 0, 0xA8, 0x01);

    // $B0-$B8: Channel control / rhythm
    forward_write(music_data, 0, 0xB0, 0x00);
    forward_write(music_data, 0, 0xB1, 0x00);
    forward_write(music_data, 0, 0xB2, 0x16);
    forward_write(music_data, 0, 0xB3, 0x00);
    forward_write(music_data, 0, 0xB4, 0x00);
    forward_write(music_data, 0, 0xB5, 0x00);
    forward_write(music_data, 0, 0xB6, 0x00);
    forward_write(music_data, 0, 0xB7, 0x00);
    forward_write(music_data, 0, 0xB8, 0x12);

    // $BD: Rhythm key-on, rhythm mode
    forward_write(music_data, 0, 0xBD, 0xC0);

    // $C0-$C8: Operator specific frequencies (DAM, DVB, RYT, etc.)
    forward_write(music_data, 0, 0xC0, 0x3A);
    forward_write(music_data, 0, 0xC1, 0x36);
    forward_write(music_data, 0, 0xC2, 0x38);
    forward_write(music_data, 0, 0xC3, 0x38);
    forward_write(music_data, 0, 0xC4, 0x3E);
    forward_write(music_data, 0, 0xC5, 0x3C);
    forward_write(music_data, 0, 0xC6, 0x3A);
    forward_write(music_data, 0, 0xC7, 0x3C);
    forward_write(music_data, 0, 0xC8, 0x1A);

    // $E0-$F5: Extended registers
    forward_write(music_data, 0, 0xE0, 0x00);
    forward_write(music_data, 0, 0xE1, 0x00);
    forward_write(music_data, 0, 0xE2, 0x01);
    forward_write(music_data, 0, 0xE3, 0x00);
    forward_write(music_data, 0, 0xE4, 0x00);
    forward_write(music_data, 0, 0xE5, 0x00);
    forward_write(music_data, 0, 0xE8, 0x01);
    forward_write(music_data, 0, 0xE9, 0x02);
    forward_write(music_data, 0, 0xEA, 0x00);
    forward_write(music_data, 0, 0xEB, 0x00);
    forward_write(music_data, 0, 0xEC, 0x00);
    forward_write(music_data, 0, 0xED, 0x00);
    forward_write(music_data, 0, 0xF0, 0x00);
    forward_write(music_data, 0, 0xF1, 0x00);
    forward_write(music_data, 0, 0xF2, 0x00);
    forward_write(music_data, 0, 0xF3, 0x00);
    forward_write(music_data, 0, 0xF4, 0x00);
    forward_write(music_data, 0, 0xF5, 0x00);

    // --- Port 1 Initialization ---
    // $01: LSI TEST register (should be 0 in normal operation)
    forward_write(music_data, 1, 0x01, 0x00);
    // $08: NTS (Note Select); selects F-NUMBER LSB source
    forward_write(music_data, 1, 0x08, 0x00);

    // $20-$35: Operator registers
    forward_write(music_data, 1, 0x20, 0x06);
    forward_write(music_data, 1, 0x21, 0x07);
    forward_write(music_data, 1, 0x22, 0x31);
    forward_write(music_data, 1, 0x23, 0x02);
    forward_write(music_data, 1, 0x24, 0x02);
    forward_write(music_data, 1, 0x25, 0x32);
    forward_write(music_data, 1, 0x28, 0x31);
    forward_write(music_data, 1, 0x29, 0x31);
    forward_write(music_data, 1, 0x2A, 0x07);
    forward_write(music_data, 1, 0x2B, 0x61);
    forward_write(music_data, 1, 0x2C, 0x31);
    forward_write(music_data, 1, 0x2D, 0x02);
    forward_write(music_data, 1, 0x30, 0x00);
    forward_write(music_data, 1, 0x31, 0x31);
    forward_write(music_data, 1, 0x32, 0x0C);
    forward_write(music_data, 1, 0x33, 0x00);
    forward_write(music_data, 1, 0x34, 0x61);
    forward_write(music_data, 1, 0x35, 0x12);

    // $40-$55: Operator volume and KSL
    forward_write(music_data, 1, 0x40, 0x3F);
    forward_write(music_data, 1, 0x41, 0x3F);
    forward_write(music_data, 1, 0x42, 0x3F);
    forward_write(music_data, 1, 0x43, 0x3F);
    forward_write(music_data, 1, 0x44, 0x3F);
    forward_write(music_data, 1, 0x45, 0x3F);
    forward_write(music_data, 1, 0x48, 0x3F);
    forward_write(music_data, 1, 0x49, 0x3F);
    forward_write(music_data, 1, 0x4A, 0x3F);
    forward_write(music_data, 1, 0x4B, 0x3F);
    forward_write(music_data, 1, 0x4C, 0x3F);
    forward_write(music_data, 1, 0x4D, 0x3F);
    forward_write(music_data, 1, 0x50, 0x3F);
    forward_write(music_data, 1, 0x51, 0x3F);
    forward_write(music_data, 1, 0x52, 0x3F);
    forward_write(music_data, 1, 0x53, 0x3F);
    forward_write(music_data, 1, 0x54, 0x3F);
    forward_write(music_data, 1, 0x55, 0x3F);

    // $60-$75: Frequency envelope (AR/DR)
    forward_write(music_data, 1, 0x60, 0xF5);
    forward_write(music_data, 1, 0x61, 0xEC);
    forward_write(music_data, 1, 0x62, 0x51);
    forward_write(music_data, 1, 0x63, 0xF5);
    forward_write(music_data, 1, 0x64, 0xF8);
    forward_write(music_data, 1, 0x65, 0x71);
    forward_write(music_data, 1, 0x68, 0x93);
    forward_write(music_data, 1, 0x69, 0xF4);
    forward_write(music_data, 1, 0x6A, 0xEC);
    forward_write(music_data, 1, 0x6B, 0x72);
    forward_write(music_data, 1, 0x6C, 0xF1);
    forward_write(music_data, 1, 0x6D, 0xF8);
    forward_write(music_data, 1, 0x70, 0xFC);
    forward_write(music_data, 1, 0x71, 0x93);
    forward_write(music_data, 1, 0x72, 0xF6);
    forward_write(music_data, 1, 0x73, 0xFA);
    forward_write(music_data, 1, 0x74, 0x72);
    forward_write(music_data, 1, 0x75, 0xFB);

    // $80-$95: LFO / sustain
    forward_write(music_data, 1, 0x80, 0x0C);
    forward_write(music_data, 1, 0x81, 0x26);
    forward_write(music_data, 1, 0x82, 0x28);
    forward_write(music_data, 1, 0x83, 0x08);
    forward_write(music_data, 1, 0x84, 0x16);
    forward_write(music_data, 1, 0x85, 0x48);
    forward_write(music_data, 1, 0x88, 0x02);
    forward_write(music_data, 1, 0x89, 0xE8);
    forward_write(music_data, 1, 0x8A, 0x26);
    forward_write(music_data, 1, 0x8B, 0x0B);
    forward_write(music_data, 1, 0x8C, 0x78);
    forward_write(music_data, 1, 0x8D, 0x16);

    // $90-$95: Key on / channel control
    forward_write(music_data, 1, 0x90, 0x05);
    forward_write(music_data, 1, 0x91, 0x02);
    forward_write(music_data, 1, 0x92, 0x08);
    forward_write(music_data, 1, 0x93, 0x17);
    forward_write(music_data, 1, 0x94, 0x0B);
    forward_write(music_data, 1, 0x95, 0x47);

    // $A0-$A8: Key on / operator setup (F-Number LSB)
    forward_write(music_data, 1, 0xA0, 0xAD);
    forward_write(music_data, 1, 0xA1, 0x01);
    forward_write(music_data, 1, 0xA2, 0x01);
    forward_write(music_data, 1, 0xA3, 0x01);
    forward_write(music_data, 1, 0xA4, 0x01);
    forward_write(music_data, 1, 0xA5, 0x01);
    forward_write(music_data, 1, 0xA6, 0xAD);
    forward_write(music_data, 1, 0xA7, 0x82);
    forward_write(music_data, 1, 0xA8, 0x01);

    // $B0-$B8: Channel control
    forward_write(music_data, 1, 0xB0, 0x00);
    forward_write(music_data, 1, 0xB1, 0x00);
    forward_write(music_data, 1, 0xB2, 0x00);
    forward_write(music_data, 1, 0xB3, 0x00);
    forward_write(music_data, 1, 0xB4, 0x00);
    forward_write(music_data, 1, 0xB5, 0x00);
    forward_write(music_data, 1, 0xB6, 0x00);
    forward_write(music_data, 1, 0xB7, 0x00);
    forward_write(music_data, 1, 0xB8, 0x0A);

    // $BD: Rhythm control
    forward_write(music_data, 1, 0xBD, 0x00);

    // $C0-$C8: Operator specific frequencies
    forward_write(music_data, 1, 0xC0, 0x36);
    forward_write(music_data, 1, 0xC1, 0x2A);
    forward_write(music_data, 1, 0xC2, 0x3C);
    forward_write(music_data, 1, 0xC3, 0x38);
    forward_write(music_data, 1, 0xC4, 0x3A);
    forward_write(music_data, 1, 0xC5, 0x2A);
    forward_write(music_data, 1, 0xC6, 0x3E);
    forward_write(music_data, 1, 0xC7, 0x38);
    forward_write(music_data, 1, 0xC8, 0x3A);

    // $E0-$ED: Extended registers
    forward_write(music_data, 1, 0xE0, 0x00);
    forward_write(music_data, 1, 0xE1, 0x00);
    forward_write(music_data, 1, 0xE2, 0x00);
    forward_write(music_data, 1, 0xE3, 0x00);
    forward_write(music_data, 1, 0xE4, 0x00);
    forward_write(music_data, 1, 0xE5, 0x00);
    forward_write(music_data, 1, 0xE8, 0x01);
    forward_write(music_data, 1, 0xE9, 0x00);
    forward_write(music_data, 1, 0xEA, 0x00);
    forward_write(music_data, 1, 0xEB, 0x00);
    forward_write(music_data, 1, 0xEC, 0x00);
    forward_write(music_data, 1, 0xED, 0x00);
    // $F0-$F5: Final frequency / key-on
    forward_write(music_data, 1, 0xF0, 0x02);
    forward_write(music_data, 1, 0xF1, 0x01);
    forward_write(music_data, 1, 0xF2, 0x00);
    forward_write(music_data, 1, 0xF3, 0x00);
    forward_write(music_data, 1, 0xF4, 0x00);
    forward_write(music_data, 1, 0xF5, 0x02);
}