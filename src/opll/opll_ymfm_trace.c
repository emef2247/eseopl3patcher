#include "opll_ymfm_trace.h"
#include "../compat_string.h"
#include "../compat_bool.h"
#include "compat_case.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // getenv, atoi

#include "../../include/ymfm_c_api.h"

/* 環境変数
   - ESEOPL3_YMFM_TRACE: 1/true で有効
   - ESEOPL3_YMFM_TRACE_MIN_WAIT: このサンプル数未満の待ちはログ抑制（測定は実施）(例: 512)
   - ESEOPL3_YMFM_TRACE_VERBOSE: 1 で全待ちを出力（抑制しない）
*/

static int s_enabled = 0;
static ymfm_ctx_t* s_ctx = NULL;
static uint32_t s_min_log_wait = 512;
static int s_verbose = 0;

// KOエッジ検出用
static uint8_t s_prev_reg2n[9] = {0};
static uint8_t s_prev_ko[9]    = {0};
static int s_init_done = 0;

static inline int parse_bool_env(const char* v) {
    if (!v) return 0;
    return (v[0]=='1'||v[0]=='y'||v[0]=='Y'||v[0]=='t'||v[0]=='T');
}

void opll_ymfm_trace_init(void) {
#if defined(USE_YMFM) && USE_YMFM
    const char* env = getenv("ESEOPL3_YMFM_TRACE");
    s_enabled = parse_bool_env(env);
    const char* vmin = getenv("ESEOPL3_YMFM_TRACE_MIN_WAIT");
    if (vmin && vmin[0]) {
        int v = atoi(vmin);
        if (v >= 0) s_min_log_wait = (uint32_t)v;
    }
    s_verbose = parse_bool_env(getenv("ESEOPL3_YMFM_TRACE_VERBOSE"));
    if (!s_enabled) return;

    // YM2413既定クロック/サンプルレート
    s_ctx = ymfm_opll_create(3579545, 44100);
    if (!s_ctx) {
        fprintf(stderr, "[YMFM] init failed, disabling trace\n");
        s_enabled = 0;
    } else {
        printf("[YMFM] trace enabled (clk=3579545 fs=44100)\n");
    }

    memset(s_prev_reg2n, 0, sizeof(s_prev_reg2n));
    memset(s_prev_ko,    0, sizeof(s_prev_ko));
    s_init_done = 1;
#else
    s_enabled = 0;
    (void)s_ctx; (void)s_min_log_wait; (void)s_verbose;
#endif
}

void opll_ymfm_trace_shutdown(void) {
    if (s_ctx) ymfm_destroy(s_ctx);
    s_ctx = NULL;
    s_enabled = 0;
    s_init_done = 0;
}

int opll_ymfm_trace_enabled(void) {
    return s_enabled;
}

static inline int ch_from_addr_local(uint8_t addr) {
    if (addr >= 0x20 && addr <= 0x28) return addr - 0x20;
    return -1;
}

void opll_ymfm_trace_write(uint8_t addr, uint8_t data) {
    if (!s_enabled || !s_ctx) return;

    // KOエッジ検出（B0 mirrorの元となる reg2n）
    int ch = ch_from_addr_local(addr);
    if (ch >= 0) {
        uint8_t ko = (data & 0x10) ? 1 : 0;
        if (!s_init_done) {
            s_prev_reg2n[ch] = data;
            s_prev_ko[ch]    = ko;
        } else {
            if (ko != s_prev_ko[ch]) {
                if (ko) {
                    printf("[YMFM][KO-ON ] ch=%d reg2n=%02X -> %02X\n", ch, s_prev_reg2n[ch], data);
                } else {
                    printf("[YMFM][KO-OFF] ch=%d reg2n=%02X -> %02X\n", ch, s_prev_reg2n[ch], data);
                }
                s_prev_ko[ch] = ko;
            }
            s_prev_reg2n[ch] = data;
        }
    }

    ymfm_opll_write(s_ctx, addr, data);
    // 詳細な書込ログはVerboseのみ
    if (s_verbose) {
        printf("[YMFM][W] addr=%02X data=%02X\n", addr, data);
    }
}

void opll_ymfm_trace_advance(uint32_t wait_samples) {
    if (!s_enabled || !s_ctx || wait_samples == 0) return;

    // 測定は常に行う（内部的に状態を進める）が、表示はしきい値で抑制
    float rms_db = ymfm_step_and_measure_db(s_ctx, wait_samples);
    float mean_abs = ymfm_step_and_measure(s_ctx, 0); // 0指定でも最後の値を返す前提で呼ばない。上で更新済み。
    uint32_t nz = ymfm_get_last_nonzero(s_ctx);

    if (s_verbose || wait_samples >= s_min_log_wait) {
        printf("[YMFM][S] wait=%u mean_abs=%.6f rms_db=%.2f nz=%u\n",
               (unsigned)wait_samples, (double)mean_abs, (double)rms_db, (unsigned)nz);
        ymfm_debug_print(s_ctx, "acc");
    }
}