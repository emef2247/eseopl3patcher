#ifndef OPL3_DEBUG_UTIL_H
#define OPL3_DEBUG_UTIL_H

#include "opl3_convert.h"
#include "opl3_voice.h"

// Print a voice parameter structure (all operators and channel fields)
void print_opl3_voice_param(const OPL3VoiceParam *vp);

static inline int opl3_voice_db_count(const OPL3VoiceDB *p_db) {
    return p_db->count;
}
static inline const OPL3VoiceParam* opl3_voice_db_last(const OPL3VoiceDB *p_db) {
    return (p_db->count > 0) ? &p_db->p_voices[p_db->count-1] : NULL;
}
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

void debug_dump_opl3_voiceparam(int ch, const OPL3VoiceParam* vp, uint8_t fnum_lsb, uint8_t fnum_msb, uint8_t block, uint8_t keyon);


#endif // OPL3_DEBUG_UTIL_H