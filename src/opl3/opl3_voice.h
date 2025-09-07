#ifndef OPL3_VOICE_H
#define OPL3_VOICE_H

#include <stdint.h>
#include "../vgm/vgm_helpers.h"

#define OPL3_MODE_2OP 0
#define OPL3_MODE_4OP 1
#define OPL3_DB_INITIAL_SIZE 16

typedef struct OPL3State OPL3State;

typedef struct {
    uint8_t am;
    uint8_t vib;
    uint8_t egt;
    uint8_t ksr;
    uint8_t mult;
    uint8_t ksl;
    uint8_t tl;
    uint8_t ar;
    uint8_t dr;
    uint8_t sl;
    uint8_t rr;
    uint8_t ws;
} OPL3OperatorParam;

typedef struct {
    int is_4op;                // 0: 2op, 1: 4op (YMF262 spec)
    OPL3OperatorParam op[4];   // [0..1] for 2op, [0..3] for 4op
    uint8_t fb[2];             // Feedback for each 2op pair
    uint8_t cnt[2];            // Connection type for each 2op pair
    FMChipType source_fmchip;  // Source FM chip type for this voice
    int voice_no;              // Unique voice ID assigned by the voice database
} OPL3VoiceParam;

typedef struct {
    OPL3VoiceParam *p_voices;
    int count;
    int capacity;
} OPL3VoiceDB;

void opl3_voice_db_init(OPL3VoiceDB *p_db);
void opl3_voice_db_free(OPL3VoiceDB *p_db);
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, OPL3VoiceParam *p_vp);
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

int is_4op_channel(const OPL3State *p_state, int ch);
int get_opl3_channel_mode(const OPL3State *p_state, int ch);
void extract_voice_param(const OPL3State *p_state, OPL3VoiceParam *p_out);

#endif // OPL3_VOICE_H