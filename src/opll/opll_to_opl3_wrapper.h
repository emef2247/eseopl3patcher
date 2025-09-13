#ifndef OPLL_TO_OPL3_WRAPPER_H
#define OPLL_TO_OPL3_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_voice.h"
#include "../opl3/opl3_event.h"

void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db);
int opll_write_register(VGMBuffer *p_music_data, VGMContext *p_vgm_context, OPL3State *p_state,uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning,double v_ratio0, double v_ratio1);

#endif //OPLL_TO_OPL3_WRAPPER_H