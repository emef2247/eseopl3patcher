#ifndef OPLL_TO_OPL3_WRAPPER_H
#define OPLL_TO_OPL3_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_voice.h"
#include "../opl3/opl3_event.h"

void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db);

uint8_t opl3_make_keyoff(uint8_t val) ;

/**
 * OPLL to OPL3 register conversion entrypoint.
 * Now takes a CommandOptions pointer for control options.
 */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    uint16_t next_wait_samples,
    const CommandOptions *opts
);
#endif //OPLL_TO_OPL3_WRAPPER_H