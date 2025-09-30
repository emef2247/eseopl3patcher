#ifndef YM2413_PATCH_CONVERT_H
#define YM2413_PATCH_CONVERT_H

#include <stdint.h>
#include "opl3_state.h"
#include "../vgm/vgm_helpers.h"

void apply_debug_overrides(OPL3VoiceParam *vp, const CommandOptions* opts);

void debug_apply_audible(OPL3VoiceParam *vp, const CommandOptions *opts) ;

void opll_apply_all_debug(OPL3VoiceParam *vp, const CommandOptions *opts);

void apply_audible_sanity (OPL3VoiceParam *vp, const CommandOptions *opts);

void apply_carrier_tl_clamp (OPL3VoiceParam *vp, const CommandOptions *opts);

void finalize_opl3_pair (OPL3VoiceParam *vp, const CommandOptions *opts);

void opl3_apply_debug_adjust(OPL3VoiceParam *vp, const CommandOptions *opts);


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
                                  OPL3VoiceParam *vp, const CommandOptions* opts);

#endif /* YM2413_PATCH_CONVERT_H */