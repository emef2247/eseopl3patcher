#ifndef OPL3_VOICE_REGISTRY_H
#define OPL3_VOICE_REGISTRY_H
#include "../vgm/vgm_helpers.h"
#include "opl3_state.h"

/* 全 YM2413 標準パッチ + リズムを DB に登録 */
void opl3_register_all_ym2413(OPL3VoiceDB *db, const CommandOptions *opts) ;

#endif /* OPL3_VOICE_REGISTRY_H */