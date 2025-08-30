#ifndef OPL3_VOICE_H
#define OPL3_VOICE_H

#include <stdint.h>


#define OPL3_MODE_2OP 0
#define OPL3_MODE_4OP 1

// Forward declaration of OPL3State (full definition in opl3_convert.h)
typedef struct OPL3State OPL3State;

// OPL3 operator parameter set (TL excluded)
typedef struct {
    uint8_t am, vib, egt, ksr, mult;
    uint8_t ksl, ar, dr, sl, rr, ws;
} OPL3OperatorParam;

// 2op/4op compatible voice structure
typedef struct {
    int is_4op;                // 1: 2op, 2: 4op
    OPL3OperatorParam op[4];   // [0..1]: 2op, [0..3]: 4op
    uint8_t fb[2];             // Feedback for each 2op pair
    uint8_t cnt[2];            // Connection type for each 2op pair
} OPL3VoiceParam;

// Dynamic array for voice database
typedef struct {
    OPL3VoiceParam *p_voices;
    int count;
    int capacity;
} OPL3VoiceDB;


// Voice DB utility functions
void opl3_voice_db_init(OPL3VoiceDB *p_db);
void opl3_voice_db_free(OPL3VoiceDB *p_db);
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, const OPL3VoiceParam *p_vp);

// Compare two OPL3VoiceParam (compare only meaningful fields)
// Extern so it can be used from other modules (e.g. debug print)
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

// Check if given channel is in 4op mode (returns 1 for 4op, 0 for 2op; now uses OPL3State pointer)
int is_4op_channel(const OPL3State *p_state, int ch);

int get_opl3_channel_mode(const OPL3State *p_state, int ch);

// Extract voice parameters for the given logical channel (0..17).
void extract_voice_param(const OPL3State *p_state, int ch, OPL3VoiceParam *p_out);

#endif // OPL3_VOICE_H