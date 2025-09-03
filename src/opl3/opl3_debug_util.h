#ifndef OPL3_DEBUG_UTIL_H
#define OPL3_DEBUG_UTIL_H

#include "opl3_convert.h"
#include "opl3_voice.h"

/**
 * Print the contents of an OPL3VoiceParam structure (operator and voice fields).
 * This function prints all operator parameters, is4op, patch number, and feedback/connection for each pair.
 *
 * @param vp Pointer to OPL3VoiceParam to print.
 */
void print_opl3_voice_param(const OPL3VoiceParam *vp) ;

/**
 * Returns the current number of voices registered in the database.
 *
 * @param p_db Pointer to OPL3VoiceDB.
 * @return Number of voices in the database.
 */
static inline int opl3_voice_db_count(const OPL3VoiceDB *p_db) {
    return p_db->count;
}

/**
 * Returns a pointer to the last (most recently added) voice in the database.
 *
 * @param p_db Pointer to OPL3VoiceDB.
 * @return Pointer to last OPL3VoiceParam, or NULL if empty.
 */
static inline const OPL3VoiceParam* opl3_voice_db_last(const OPL3VoiceDB *p_db) {
    return (p_db->count > 0) ? &p_db->p_voices[p_db->count-1] : NULL;
}

/**
 * Extern declaration for OPL3VoiceParam comparison function.
 */
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

#endif // OPL3_DEBUG_UTIL_H