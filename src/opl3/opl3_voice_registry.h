#ifndef OPL3_VOICE_REGISTRY_H
#define OPL3_VOICE_REGISTRY_H

#include "opl3_state.h"

/* 全 YM2413 標準パッチ + リズムを DB に登録 */
void opl3_register_all_ym2413(OPL3VoiceDB *db);

#endif /* OPL3_VOICE_REGISTRY_H */