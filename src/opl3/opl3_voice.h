#ifndef ESEOPL3PATCHER_OPL3_VOICE_H
#define ESEOPL3PATCHER_OPL3_VOICE_H

#include "opl3_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void opl3_voice_db_init(OPL3VoiceDB *db);
void opl3_voice_db_free(OPL3VoiceDB *db);
int  opl3_voice_db_find_or_add(OPL3VoiceDB *db, OPL3VoiceParam *vp); /* 非 const に統一 */
int  opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);
void extract_voice_param(const OPL3State *p_state, OPL3VoiceParam *out); /* const state */

#ifdef __cplusplus
}
#endif
#endif /* ESEOPL3PATCHER_OPL3_VOICE_H */