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
        forward_write(music_data, 0, 0xC0 + ch, val | 0x10);
        forward_write(music_data, 1, 0xC0 + ch, val | 0x20); port1_bytes += 3;
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

    // --- Port 1 Initialization ---
    // $01: LSI TEST register (should be 0 in normal operation)
    forward_write(music_data, 1, 0x01, 0x00);

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
    forward_write(music_data, 0, 0xEE, 0x00);
    forward_write(music_data, 0, 0xEF, 0x00);

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
    forward_write(music_data, 1, 0xEE, 0x00);
    forward_write(music_data, 1, 0xEF, 0x00); 

    // $F0-$F5: Final frequency / key-on
    forward_write(music_data, 1, 0xF0, 0x02);
    forward_write(music_data, 1, 0xF1, 0x01);
    forward_write(music_data, 1, 0xF2, 0x00);
    forward_write(music_data, 1, 0xF3, 0x00);
    forward_write(music_data, 1, 0xF4, 0x00);
    forward_write(music_data, 1, 0xF5, 0x02);
}