#ifndef ESEOPL3PATCHER_OPLL_YMFM_TRACE_H
#define ESEOPL3PATCHER_OPLL_YMFM_TRACE_H

#include <stdint.h>

void opll_ymfm_trace_init(void);
void opll_ymfm_trace_shutdown(void);
int  opll_ymfm_trace_enabled(void);

void opll_ymfm_trace_write(uint8_t addr, uint8_t data);
void opll_ymfm_trace_advance(uint32_t wait_samples);

#endif