#ifndef YM2413_PATCH_CONVERT_H
#define YM2413_PATCH_CONVERT_H

#include <stdint.h>
#include "opl3_state.h"

/* YM2413 patch number -> OPL3VoiceParam (既存 static 実装を移植)
 * inst:
 *   0 = user / custom (ym2413_regs 参照)
 *   1..15 = melodic
 *   16..20 = rhythm voices
 * ym2413_regs:
 *   inst==0 のときに使用。それ以外は NULL 可
 */
void ym2413_patch_to_opl3_with_fb(int inst,
                                  const uint8_t *ym2413_regs,
                                  OPL3VoiceParam *vp);

#endif /* YM2413_PATCH_CONVERT_H */