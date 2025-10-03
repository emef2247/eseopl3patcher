#ifndef ESEOPL3PATCHER_YMFM_TRACE_CSV_H
#define ESEOPL3PATCHER_YMFM_TRACE_CSV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Initialize CSV logger if ESEOPL3_YMFM_TRACE_CSV is set.
 * Writes header once. No-op if env is unset or file open fails.
 */
void ymfm_trace_csv_init(void);

/* Close CSV if open. Safe to call multiple times. */
void ymfm_trace_csv_shutdown(void);

/* Notify KO edge.
 * ko_on: 1 for KO-ON, 0 for KO-OFF
 * reg2n: raw YM2413 2n value written
 * This function switches the "focus" channel to ch on KO-ON.
 */
void ymfm_trace_csv_on_ko_edge(int ch, int ko_on, unsigned char reg2n);

/* Return current focus channel (or -1 if none). */
int ymfm_trace_csv_get_focus_ch(void);

/* Log a wait window measurement for the current focus note (if any).
 * mean_abs/rms_db/nz: the values measured over this wait window.
 * wait_samples: window length in samples.
 * EG fields:
 *  - phase_mod/phase_car: envelope_state as int (0..5), -1 if unavailable
 *  - att_mod/att_car: raw envelope attenuation 0..1023 (0=max), -1 if unavailable
 *  - att_mod_db/att_car_db: approximate dB conversion from raw attenuation
 */
void ymfm_trace_csv_on_wait(
    uint32_t wait_samples,
    float mean_abs, float rms_db, uint32_t nz,
    int phase_mod, int att_mod, float att_mod_db,
    int phase_car, int att_car, float att_car_db
);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_YMFM_TRACE_CSV_H */