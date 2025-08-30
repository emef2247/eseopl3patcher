#ifndef OPL3_CONVERT_H
#define OPL3_CONVERT_H

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

// Detune helper for FM channels
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB);

// Apply register value to OPL3/OPL2 ports
int apply_to_ports(dynbuffer_t *p_music_data, vgm_status_t *p_vstat, OPL3State *p_state, int ch, const char *p_reg_type, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1);

// Rhythm mode register handler
void handle_bd(dynbuffer_t *p_music_data, OPL3State *p_state, uint8_t val);

// Main OPL3/OPL2 register write handler (OPL3 chorus effect)
int duplicate_write_opl3(dynbuffer_t *p_music_data, vgm_status_t *p_vstat, OPL3State *p_state, uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1);
// OPL3 initialization sequence for both ports
void opl3_init(dynbuffer_t *p_music_data, int stereo_mode);

#endif // OPL3_CONVERT_H