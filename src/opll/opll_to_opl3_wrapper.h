#ifndef ESEOPL3PATCHER_OPLL_TO_OPL3_WRAPPER_H
#define ESEOPL3PATCHER_OPLL_TO_OPL3_WRAPPER_H

#include "../opl3/opl3_convert.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include <string.h>

/** Pending / stamp structs */
typedef struct {
    uint8_t has_1n, has_2n, has_3n;
    uint8_t reg1n, reg2n, reg3n;
} OpllPendingCh;

typedef struct {
    uint8_t valid_1n, valid_2n, valid_3n;
    uint8_t last_1n, last_2n, last_3n;
    uint8_t ko;
} OpllStampCh;

/** Utility clear functions */
static inline void opll_pending_clear(OpllPendingCh *p) { memset(p, 0, sizeof(*p)); }
static inline void stamp_clear(OpllStampCh *s) { memset(s, 0, sizeof(*s)); }

typedef struct {
    uint8_t has_2n;
    uint8_t ko_next;
    uint8_t note_on_edge;
    uint8_t note_off_edge;
} PendingEdgeInfo;

/** Public wrapper API */
void opll_set_program_args(int argc, char **argv);
void opll_init(OPL3State *p_state, const CommandOptions* opts) ;

int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t addr, uint8_t val, uint16_t next_wait_samples,
    const CommandOptions *opts);

void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db, CommandOptions* opts);

/** Gate compensation debt access (for duplicate_write_opl3) */
extern uint32_t* opll_get_gate_comp_debt_ptr(void);

#endif /* ESEOPL3PATCHER_OPLL_TO_OPL3_WRAPPER_H */