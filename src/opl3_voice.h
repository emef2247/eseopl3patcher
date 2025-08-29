#ifndef OPL3_VOICE_H
#define OPL3_VOICE_H

#include <stdint.h>
#include "opl3_convert.h"

// OPL3 operator parameter set (TL excluded)
typedef struct {
    uint8_t am, vib, egt, ksr, mult;
    uint8_t ksl, ar, dr, sl, rr, ws;
} OPL3OperatorParam;

// 2op/4op compatible voice structure
typedef struct {
    int is_4op; // 1: 2op, 2: 4op
    OPL3OperatorParam op[4]; // [0..1]: 2op, [0..3]: 4op
    uint8_t fb[2];   // Feedback for each 2op pair
    uint8_t cnt[2];  // Connection type for each 2op pair
} OPL3VoiceParam;

// Dynamic array for voice database
typedef struct {
    OPL3VoiceParam *voices;
    int count;
    int capacity;
} OPL3VoiceDB;

// Utility
void opl3_voice_db_init(OPL3VoiceDB *db);
void opl3_voice_db_free(OPL3VoiceDB *db);
int opl3_voice_db_find_or_add(OPL3VoiceDB *db, const OPL3VoiceParam *vp);
int is_4op_channel(const uint8_t reg_104, int ch);
void extract_voice_param(const OPL3State *state, int ch, uint8_t reg_104, OPL3VoiceParam *out);

#endif // OPL3_VOICE_H