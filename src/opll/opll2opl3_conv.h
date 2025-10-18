#ifndef ESEOPL3PATCHER_OPLL2OPL3_CONV_H
#define ESEOPL3PATCHER_OPLL2OPL3_CONV_H

#include "../opl3/opl3_convert.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include <string.h>

void opll2opl3_init_scheduler  (VGMContext *p_vgmctx, const CommandOptions *p_opts);
int  opll2opl3_command_handler (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, uint16_t wait_samples, const CommandOptions *p_opts);

#endif /* ESEOPL3PATCHER_OPLL2OPL3_CONV_H */