#include "ymfm_trace_csv.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef YMFM_TRACE_FS
#define YMFM_TRACE_FS 44100u
#endif

static FILE* s_csv = NULL;
static int   s_focus_ch = -1;          /* channel currently being traced */
static unsigned s_session_id = 0;      /* increments on each KO-ON */
static uint64_t s_t_samples = 0;       /* elapsed samples within current session */

static void csv_write_header(void) {
    if (!s_csv) return;
    fprintf(s_csv,
        "session_id,ch,t_samples,t_ms,wait_samples,mean_abs,rms_db,nz,"
        "phase_mod,att_mod,att_mod_db,phase_car,att_car,att_car_db,"
        "event,reg2n_hex,reco_end_db, reco_hold, reco_min_gate, reco_start_grace, reco_since_on, reco_below_cnt, reco_gate_ok, reco_settled\n");
    fflush(s_csv);
}

void ymfm_trace_csv_init(void) {
    const char* path = getenv("ESEOPL3_YMFM_TRACE_CSV");
    if (!path || !*path) return;
    s_csv = fopen(path, "w");
    if (!s_csv) {
        fprintf(stderr, "[YMFM-CSV] failed to open: %s\n", path);
        return;
    }
    csv_write_header();
    s_focus_ch = -1;
    s_session_id = 0;
    s_t_samples = 0;
    fprintf(stderr, "[YMFM-CSV] logging to: %s\n", path);
}

void ymfm_trace_csv_shutdown(void) {
    if (s_csv) {
        fclose(s_csv);
        s_csv = NULL;
    }
    s_focus_ch  = -1;
    s_session_id = 0;
    s_t_samples  = 0;
}

int ymfm_trace_csv_get_focus_ch(void) {
    return s_focus_ch;
}

void ymfm_trace_csv_on_ko_edge(int ch, int ko_on, unsigned char reg2n) {
    if (!s_csv) return;

    double t_ms = (double)s_t_samples * 1000.0 / (double)YMFM_TRACE_FS;
    if (ko_on) {
        s_focus_ch = ch;
        s_session_id++;
        s_t_samples = 0;
        t_ms = 0.0;
        fprintf(s_csv, "%u,%d,%llu,%.6f,%u,%.6f,%.2f,%u,%d,%d,%.2f,%d,%d,%.2f,%s,%02X\n",
                s_session_id, s_focus_ch,
                (unsigned long long)s_t_samples, t_ms,
                0u, 0.0, -240.0, 0u,
                -1, -1, -240.0, -1, -1, -240.0,
                "KO_ON", (unsigned)reg2n);
    } else {
        fprintf(s_csv, "%u,%d,%llu,%.6f,%u,%.6f,%.2f,%u,%d,%d,%.2f,%d,%d,%.2f,%s,%02X\n",
                s_session_id, (s_focus_ch >= 0 ? s_focus_ch : ch),
                (unsigned long long)s_t_samples, t_ms,
                0u, 0.0, -240.0, 0u,
                -1, -1, -240.0, -1, -1, -240.0,
                (s_focus_ch == ch) ? "KO_OFF" : "KO_OFF_OTHER", (unsigned)reg2n);
    }
    fflush(s_csv);
}

void ymfm_trace_csv_on_wait(
    uint32_t wait_samples,
    float mean_abs, float rms_db, uint32_t nz,
    int phase_mod, int att_mod, float att_mod_db,
    int phase_car, int att_car, float att_car_db
) {
    if (!s_csv) return;
    if (s_focus_ch < 0) return;

    double t_ms_before = (double)s_t_samples * 1000.0 / (double)YMFM_TRACE_FS;

    fprintf(s_csv, "%u,%d,%llu,%.6f,%u,%.6f,%.2f,%u,%d,%d,%.2f,%d,%d,%.2f,%s,%s\n",
            s_session_id, s_focus_ch,
            (unsigned long long)s_t_samples, t_ms_before,
            (unsigned)wait_samples,
            (double)mean_abs, (double)rms_db, (unsigned)nz,
            phase_mod, att_mod, (double)att_mod_db,
            phase_car, att_car, (double)att_car_db,
            "WAIT", "");

    s_t_samples += wait_samples;
    fflush(s_csv);
}