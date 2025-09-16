#ifndef OPLL_TO_OPL3_WRAPPER_H
#define OPLL_TO_OPL3_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_voice.h"
#include "../opl3/opl3_event.h"

void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db);

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

typedef struct {
    uint8_t last_1n; bool valid_1n;
    uint8_t last_2n; bool valid_2n; /* $2n の最後に出した値 */
    uint8_t last_3n; bool valid_3n;
    bool ko; /* 直近に出力した $2n の KO(bit4) */
} OpllStampCh;

typedef struct {
    bool has_1n; uint8_t reg1n; /* $1n */
    bool has_2n; uint8_t reg2n; /* $2n */
    bool has_3n; uint8_t reg3n; /* $3n */
} OpllPendingCh;

typedef struct {
    bool has_2n;
    bool note_on_edge;   /* ko:0→1 */
    bool note_off_edge;  /* ko:1→0 */
    bool ko_next;
} PendingEdgeInfo;

static inline uint8_t opll_make_keyoff(uint8_t val) {
    return (uint8_t)(val & ~(1u << 4)); /* KO(bit4) を落とす */
}

#define YM2413_NUM_CH 9
extern OpllPendingCh g_pend[YM2413_NUM_CH];
extern OpllStampCh   g_stamp[YM2413_NUM_CH];

#endif //OPLL_TO_OPL3_WRAPPER_H