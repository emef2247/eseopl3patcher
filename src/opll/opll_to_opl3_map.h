#ifndef ESEOPL3PATCHER_OPLL_TO_OPL3_MAP_H
#define ESEOPL3PATCHER_OPLL_TO_OPL3_MAP_H

#include <stdint.h>
#include "../opl3/opl3_state.h"

/* OPLLユーザー音色（8バイト：00..07）からOPL3VoiceParamを生成する
 * inst[0] = 00h: mod AM/VIB/EGT/KSR/MULT
 * inst[1] = 01h: car AM/VIB/EGT/KSR/MULT
 * inst[2] = 02h: [7:6]=car KSL, [5:0]=mod TL
 * inst[3] = 03h: [7:6]=mod KSL, [4]=car rect, [3]=mod rect, [2:0]=FB
 * inst[4] = 04h: mod AR/DR
 * inst[5] = 05h: car AR/DR
 * inst[6] = 06h: mod SL/RR
 * inst[7] = 07h: car SL/RR
 * volume_nibble: 0..15（0=最大音量, 15=最小）
 */
void opll_to_opl3_map_from_bytes(const uint8_t inst[8], uint8_t volume_nibble, OPL3VoiceParam* out);

/* バイト分解済みを直接渡す版（必要なら） */
void opll_to_opl3_map_from_regs(
    uint8_t mod_ctrl, uint8_t car_ctrl, uint8_t carKsl_modTl, uint8_t misc_fb_rect_ksl,
    uint8_t mod_ar_dr, uint8_t car_ar_dr, uint8_t mod_sl_rr, uint8_t car_sl_rr,
    uint8_t volume_nibble, OPL3VoiceParam* out
);

#endif /* ESEOPL3PATCHER_OPLL_TO_OPL3_MAP_H */