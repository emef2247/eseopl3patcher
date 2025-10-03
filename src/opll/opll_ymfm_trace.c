#include "opll_ymfm_trace.h"
#include "../compat_string.h"
#include "../compat_bool.h"
#include <stdio.h>
#include <stdlib.h>

/* デフォルト: YMFM無効時はNO-OP実装（ビルド常時成功用） */
#if !(defined(USE_YMFM) && USE_YMFM)

void opll_ymfm_trace_init(void) {}
void opll_ymfm_trace_shutdown(void) {}
int  opll_ymfm_trace_enabled(void) { return 0; }
void opll_ymfm_trace_write(uint8_t addr, uint8_t data) { (void)addr; (void)data; }
void opll_ymfm_trace_advance(uint32_t wait_samples) { (void)wait_samples; }

#else

#include "../../include/ymfm_c_api.h"

static int s_enabled = 0;
static ymfm_ctx_t* s_ctx = NULL;

void opll_ymfm_trace_init(void) {
    const char* env = getenv("ESEOPL3_YMFM_TRACE");
    s_enabled = (env && (env[0]=='1' || env[0]=='y' || env[0]=='Y' || compat_strcasecmp(env,"true")==0));
    if (!s_enabled) return;
    s_ctx = ymfm_opll_create(3579545, 44100);
    if (!s_ctx) {
        fprintf(stderr, "[YMFM] init failed, disabling trace\n");
        s_enabled = 0;
    } else {
        printf("[YMFM] trace enabled (clk=3579545 fs=44100)\n");
    }
}

void opll_ymfm_trace_shutdown(void) {
    if (s_ctx) ymfm_destroy(s_ctx);
    s_ctx = NULL;
    s_enabled = 0;
}

int opll_ymfm_trace_enabled(void) {
    return s_enabled;
}

void opll_ymfm_trace_write(uint8_t addr, uint8_t data) {
    if (!s_enabled || !s_ctx) return;
    ymfm_opll_write(s_ctx, addr, data);
    printf("[YMFM][W] addr=%02X data=%02X\n", addr, data);
}

void opll_ymfm_trace_advance(uint32_t wait_samples) {
    if (!s_enabled || !s_ctx || wait_samples == 0) return;
    float mean_abs = ymfm_step_and_measure(s_ctx, wait_samples);
    printf("[YMFM][S] wait=%u mean_abs=%.6f\n", (unsigned)wait_samples, (double)mean_abs);
    ymfm_debug_print(s_ctx, "acc");
}

#endif