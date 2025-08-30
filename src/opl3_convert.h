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
    OPL3VoiceDB voice_db;            // Voice database
    // Add more fields as needed (e.g., uint64_t time_stamp)
} OPL3State;

// OPL3 register type for conversion logic (meaningful, not just A/B/C/T)
typedef enum {
    OPL3_REGTYPE_FREQ_LSB,   // 0xA0-0xA8 : Frequency LSB (FNUM low)
    OPL3_REGTYPE_FREQ_MSB,   // 0xB0-0xB8 : Frequency MSB (FNUM high, KEYON, BLOCK)
    OPL3_REGTYPE_CH_CTRL,    // 0xC0-0xC8 : Channel control (Feedback, Algorithm, Panning)
    OPL3_REGTYPE_OP_TL,      // 0x40-0x55 : Operator TL (Total Level)
    OPL3_REGTYPE_RHYTHM_BD,  // 0xBD      : Rhythm mode/BD
    OPL3_REGTYPE_OTHER       // All other registers
} opl3_regtype_t;

// Conversion context for register write (not OPL3-specific, for conversion logic)
typedef struct {
    dynbuffer_t *p_music_data;
    vgm_status_t *p_vstat;
    OPL3State *p_state;
    int ch;
    opl3_regtype_t reg_type;
    uint8_t reg;        // Actual register offset (e.g. 0xA0.., 0xB0.., etc)
    uint8_t val;
    double detune;
    int opl3_keyon_wait;
    int ch_panning;
    double v_ratio0;
    double v_ratio1;
} opl3_convert_ctx_t;

// Write a value to the OPL3 register mirror and update internal state flags.
void opl3_write_reg(OPL3State *p_state, dynbuffer_t *p_music_data, int port, uint8_t reg, uint8_t value);

// Detune helper for FM channels (used for frequency detune effects)
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB);

// Judge OPL3 register type based on register offset
opl3_regtype_t opl3_judge_regtype(uint8_t reg);

// Apply register value to OPL3/OPL2 ports (used for multi-port/stereo applications)
int apply_to_ports(const opl3_convert_ctx_t *ctx);

// Rhythm mode register handler
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

// OPL3 initialization sequence for both ports
void opl3_init(dynbuffer_t *p_music_data, int stereo_mode, OPL3State *p_state);

#endif // OPL3_CONVERT_H