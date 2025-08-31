#ifndef OPL3_DEBUG_UTIL_H
#define OPL3_DEBUG_UTIL_H

#include "opl3_convert.h"
#include "opl3_voice.h"

// Print the voice parameters with voice ID, total count, FM chip type (as string), and patch number.
void print_opl3_voice_param(const OPL3State *p_state, const OPL3VoiceParam *vp);

// Print the registers, voice ID, FM chip type (as string), and patch number for each channel in OPL3State.
void print_opl3_state_and_voice(const OPL3State *p_state);

// Returns the current number of voices registered in the database.
static inline int opl3_voice_db_count(const OPL3VoiceDB *p_db) {
    return p_db->count;
}

// Returns a pointer to the last (most recently added) voice in the database.
static inline const OPL3VoiceParam* opl3_voice_db_last(const OPL3VoiceDB *p_db) {
    return (p_db->count > 0) ? &p_db->p_voices[p_db->count-1] : NULL;
}

// Extern declaration for OPL3VoiceParam comparison function
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

#endif // OPL3_DEBUG_UTIL_H