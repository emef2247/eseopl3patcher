#include "opll_ymfm_trace.h"
#include "../compat_string.h"
#include "../compat_bool.h"
#include "compat_case.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // getenv, atoi

#include "../../include/ymfm_c_api.h"
#include "ymfm_trace_csv.h"

/* 環境変数
   - ESEOPL3_YMFM_TRACE: 1/true で有効
   - ESEOPL3_YMFM_TRACE_MIN_WAIT: このサンプル数未満の待ちはログ抑制（測定は実施）(例: 512)
   - ESEOPL3_YMFM_TRACE_VERBOSE: 1 で全待ち/イベントを出力（抑制しない）
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

    ymfm_trace_csv_init();
    atexit(ymfm_trace_csv_shutdown);
#else
    s_enabled = 0;
    (void)s_ctx; (void)s_min_log_wait; (void)s_verbose;
#endif
}

#ifdef __GNUC__
__attribute__((weak))
#endif
void ymfm_trace_csv_on_reco_off(
    int ch,
    float end_db, uint32_t hold, uint32_t min_gate, uint32_t start_grace,
    uint32_t since_on, uint32_t below_cnt, int gate_ok, int settled,
    uint8_t reg2n_hex)
{
    (void)ch; (void)end_db; (void)hold; (void)min_gate; (void)start_grace;
    (void)since_on; (void)below_cnt; (void)gate_ok; (void)settled; (void)reg2n_hex;
    /* 応急処置: 何もしない（CSVは未出力）。本実装が入ると自動で置き換わる */
}

#ifdef __GNUC__
__attribute__((weak))
#endif
void ymfm_trace_csv_on_real_ko(int ch, int ko_on, uint8_t reg2n_hex)
{
    (void)ch; (void)ko_on; (void)reg2n_hex;
    /* 応急処置: 何もしない（CSVは未出力）。本実装が入ると自動で置き換わる */
}

ymfm_ctx_t* opll_ymfm_trace_get_ctx(void) {
    return s_ctx;
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
                    if (s_verbose) printf("[YMFM][KO-ON ] ch=%d reg2n=%02X -> %02X\n", ch, s_prev_reg2n[ch], data);
                    ymfm_trace_csv_on_ko_edge(ch, 1, data);
                } else {
                    if (s_verbose) printf("[YMFM][KO-OFF] ch=%d reg2n=%02X -> %02X\n", ch, s_prev_reg2n[ch], data);
                    ymfm_trace_csv_on_ko_edge(ch, 0, data);
                }
                s_prev_ko[ch] = ko;
            }
            s_prev_reg2n[ch] = data;
        }
    }

    ymfm_opll_write(s_ctx, addr, data);
    if (s_verbose) {
        printf("[YMFM][W] addr=%02X data=%02X\n", addr, data);
    }
}

/* 生attenuation(0..1023)→近似dB換算（0で0dB、1023で約-96dB） */
static inline float eg_att_to_db_approx(int att_raw) {
    if (att_raw < 0) return -240.0f;
    if (att_raw > 1023) att_raw = 1023;
    return -(float)att_raw * (96.0f / 1023.0f);
}

void opll_ymfm_trace_advance(uint32_t wait_samples) {
    if (!s_enabled || !s_ctx || wait_samples == 0) return;

    float rms_db = ymfm_step_and_measure_db(s_ctx, wait_samples);
    float mean_abs = ymfm_step_and_measure(s_ctx, 0);
    uint32_t nz = ymfm_get_last_nonzero(s_ctx);

    // EG（フォーカスch）を取得
    int focus = ymfm_trace_csv_get_focus_ch();
    int ph_m = -1, ph_c = -1;
    int att_m = -1, att_c = -1;
    float att_m_db = -240.0f, att_c_db = -240.0f;
    if (focus >= 0) {
        ph_m = ymfm_get_op_env_phase(s_ctx, focus, 0);
        att_m = ymfm_get_op_env_att(s_ctx, focus, 0);
        ph_c = ymfm_get_op_env_phase(s_ctx, focus, 1);
        att_c = ymfm_get_op_env_att(s_ctx, focus, 1);
        att_m_db = eg_att_to_db_approx(att_m);
        att_c_db = eg_att_to_db_approx(att_c);
    }

    ymfm_trace_csv_on_wait(
        wait_samples, mean_abs, rms_db, nz,
        ph_m, att_m, att_m_db,
        ph_c, att_c, att_c_db
    );

    if (s_verbose || wait_samples >= s_min_log_wait) {
        printf("[YMFM][S] wait=%u mean_abs=%.6f rms_db=%.2f nz=%u\n",
               (unsigned)wait_samples, (double)mean_abs, (double)rms_db, (unsigned)nz);
        ymfm_debug_print(s_ctx, "acc");
    }
}

/* ===== NEW: 勧告KeyOffと実B0エッジをCSVへ ===== */

/* 推奨KeyOff（RECO_OFF）をCSVに1行書く */
void opll_ymfm_trace_log_reco_off(
    int ch,
    float end_db, uint32_t hold, uint32_t min_gate, uint32_t start_grace,
    uint32_t since_on, uint32_t below_cnt, int gate_ok, int settled,
    uint8_t reg2n_hex)
{
    if (!s_enabled) return;
    /* CSVに拡張列付きで出力（ヘッダは ymfm_trace_csv_init 側で拡張） */
    ymfm_trace_csv_on_reco_off(
        ch, end_db, hold, min_gate, start_grace,
        since_on, below_cnt, gate_ok, settled, reg2n_hex
    );
    if (s_verbose) {
        printf("[YMFM][RECO-OFF] ch=%d end_db=%.2f hold=%u min_gate=%u grace=%u since_on=%u below=%u gate_ok=%d settled=%d reg2n=%02X\n",
               ch, (double)end_db, hold, min_gate, start_grace,
               since_on, below_cnt, gate_ok, settled, reg2n_hex);
    }
}

/* 実際にOPL3へ書いたB0エッジ（REAL_KO_ON/OFF）をCSVに1行書く */
void opll_ymfm_trace_log_real_ko(int ch, int ko_on, uint8_t reg2n_hex) {
    if (!s_enabled) return;
    ymfm_trace_csv_on_real_ko(ch, ko_on ? 1 : 0, reg2n_hex);
    if (s_verbose) {
        printf(ko_on ? "[YMFM][REAL-KO-ON ] ch=%d reg2n=%02X\n"
                     : "[YMFM][REAL-KO-OFF] ch=%d reg2n=%02X\n",
               ch, reg2n_hex);
    }
}