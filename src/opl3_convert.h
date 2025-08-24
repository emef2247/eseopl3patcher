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
void detune_if_fm(OPL3State *state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *outA, uint8_t *outB);

// Apply register value to OPL3/OPL2 ports
int apply_to_ports(
    dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state,
    int ch, const char *regType, uint8_t val, double detune, int opl3_keyon_wait);

// Rhythm mode register handler
void handle_bd(dynbuffer_t *music_data, OPL3State *state, uint8_t val);

// Main OPL3/OPL2 register write handler (OPL3 chorus effect)
int duplicate_write_opl3(dynbuffer_t *music_data, vgm_status_t *vstat, OPL3State *state, uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait);
    
// OPL3 initialization sequence for both ports
void opl3_init(dynbuffer_t *music_data);

#endif // OPL3_CONVERT_H