#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* forward decl from ymfm_c_api.h to avoid include chain if not needed */
typedef struct ymfm_ctx ymfm_ctx_t;

/* Enabled? */
int  opll_ymfm_trace_enabled(void);

/* Initialize/Shutdown */
void opll_ymfm_trace_init(void);
void opll_ymfm_trace_shutdown(void);

/* Access YMFM ctx (for duration follower init, etc.) */
ymfm_ctx_t* opll_ymfm_trace_get_ctx(void);

/* Mirror an OPLL write into YMFM (0x00-0x3F) */
void opll_ymfm_trace_write(uint8_t addr, uint8_t data);

/* Advance YMFM by wait_samples and log a WAIT row with measurements */
void opll_ymfm_trace_advance(uint32_t wait_samples);

/* NEW: Log a recommended KeyOff (policy decision) to CSV.
   - ch: OPLL ch (0..8)
   - end_db/hold/min_gate/start_grace: your policy threshold and guards
   - since_on: samples since KeyOn
   - below_cnt: consecutive samples below end_db
   - gate_ok: since_on >= min_gate ? 1:0
   - settled: below_cnt >= hold ? 1:0
   - reg2n_hex: current OPLL 2n (for reference), or 0xFF if unknown
*/
void opll_ymfm_trace_log_reco_off(
    int ch,
    float end_db, uint32_t hold, uint32_t min_gate, uint32_t start_grace,
    uint32_t since_on, uint32_t below_cnt, int gate_ok, int settled,
    uint8_t reg2n_hex);

/* NEW: Log a real B0 edge (OPL3 actual write) to CSV and (optionally) console.
   - ko_on: 1 for KeyOn, 0 for KeyOff
   - reg2n_hex: OPLL 2n at that moment (reference), or 0xFF if unknown
*/
void opll_ymfm_trace_log_real_ko(int ch, int ko_on, uint8_t reg2n_hex);

#ifdef __cplusplus
}
#endif