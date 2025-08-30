#ifndef OPL3_CONVERT_H
#define OPL3_CONVERT_H

#include <stdint.h>
#include <stdbool.h>
#include "vgm_helpers.h"
#include "opl3_voice.h"

#define OPL3_NUM_CHANNELS 18
#define OPL3_REGISTER_SIZE 0x200 // 0x100 registers per port, 2 ports

// OPL3State structure holds all OPL3 emulation state, including voice DB.
typedef struct OPL3State {
    uint8_t reg[OPL3_REGISTER_SIZE]; // Full OPL3 register mirror (0x000-0x1FF)
    bool rhythm_mode;                // Rhythm mode flag, updated on BD writes
    bool opl3_mode_initialized;      // OPL3 mode flag, updated on 0x105 writes
    OPL3VoiceDB voice_db;            // Voice database (no global g_voice_db anymore)
} OPL3State;

// Write a value to the OPL3 register mirror and update internal state flags.
// This function also writes to the actual device/output via forward_write.
void opl3_write_reg(OPL3State *p_state, dynbuffer_t *p_music_data, int port, uint8_t reg, uint8_t value);

// Detune helper for FM channels (used for frequency detune effects)
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB);

// Apply register value to OPL3/OPL2 ports (used for multi-port/stereo applications)
int apply_to_ports(
    dynbuffer_t *p_music_data,
    vgm_status_t *p_vstat,
    OPL3State *p_state,
    int ch,
    const char *p_reg_type,
    uint8_t val,
    double detune,
    int opl3_keyon_wait,
    int ch_panning,
    double v_ratio0,
    double v_ratio1
);

// Rhythm mode register handler (handles rhythm mode state updates and writes)
void handle_bd(dynbuffer_t *p_music_data, OPL3State *p_state, uint8_t val);

// Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring)
int duplicate_write_opl3(
    dynbuffer_t *p_music_data,
    vgm_status_t *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
);

// OPL3 initialization sequence for both ports (zeroes registers, sets stereo/mono, etc)
void opl3_init(dynbuffer_t *p_music_data, int stereo_mode, OPL3State *p_state);

#endif // OPL3_CONVERT_H