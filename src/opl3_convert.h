#ifndef OPL3_CONVERT_H
#define OPL3_CONVERT_H

#include <stdint.h>
#include <stdbool.h>
#include "vgm_helpers.h"

#define OPL3_NUM_CHANNELS 18
// --- Channel registers state ---
typedef struct {
    uint8_t regA[OPL3_NUM_CHANNELS];
    uint8_t regB[OPL3_NUM_CHANNELS];
    uint8_t regC[OPL3_NUM_CHANNELS];
    bool rhythm_mode;
    bool opl3_mode_initialized;
} OPL3State;

// Detune helper for FM channels
void detune_if_fm(OPL3State *pState, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *pOutA, uint8_t *pOutB);

// Apply register value to OPL3/OPL2 ports
int apply_to_ports(dynbuffer_t *pMusicData, vgm_status_t *vstat, OPL3State *pState, int ch, const char *pRegType, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1);

// Rhythm mode register handler
void handle_bd(dynbuffer_t *pMusicData, OPL3State *pState, uint8_t val);

// Main OPL3/OPL2 register write handler (OPL3 chorus effect)
int duplicate_write_opl3(dynbuffer_t *pMusicData, vgm_status_t *vstat, OPL3State *pState, uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1);
// OPL3 initialization sequence for both ports
void opl3_init(dynbuffer_t *pMusicData, int stereo_mode);
#endif // OPL3_CONVERT_H