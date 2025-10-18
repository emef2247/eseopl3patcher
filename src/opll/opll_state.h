#ifndef OPLL_STATE_H
#define OPLL_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "../compat_bool.h"
#include "opll2opl3_conv.h"

typedef struct {
    uint8_t  reg[0x200];
    uint8_t  reg_stamp[0x200];
    uint8_t  patches[19][8];
    bool     is_rhythm_mode;
    bool     is_initialized;
    uint8_t lfo_depth;  // Staged A0 values per channel
} OPLLState;

#endif /* OPLL_STATE_H */