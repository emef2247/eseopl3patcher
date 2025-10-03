# YMFM Trace Quick Guide

What this trace shows
- Mirrors YM2413 register writes into YMFM (YM2413 core), stepping time on each VGM wait.
- Prints coarse audio response metrics for each wait:
  - mean_abs: mean absolute amplitude (0..1 normalized)
  - rms_db: RMS in dBFS (0 dBFS = full scale)
  - nz: count of non-zero samples in the window

Enable
- Build with YMFM: `make USE_YMFM=1 -j`
- Run with trace: `ESEOPL3_YMFM_TRACE=1 ./build/bin/eseopl3patcher <vgm> 0`

Environment variables
- ESEOPL3_YMFM_TRACE=1
  - Enable the YMFM trace.
- ESEOPL3_YMFM_TRACE_MIN_WAIT=512
  - Suppress printing for small waits (< N samples). Measurement still runs.
  - Useful to reduce noise; large waits (e.g., 11036 samples) are printed.
- ESEOPL3_YMFM_TRACE_VERBOSE=1
  - Print every write ([W]) and every step ([S]) regardless of wait size.

KeyOn/KeyOff edge logging
- Writing to reg2n ($20-$28) toggles KO bit. The trace prints the edges:
  - [YMFM][KO-ON ] ch=X reg2n=old -> new
  - [YMFM][KO-OFF] ch=X reg2n=old -> new
- After KO-OFF, long waits often show rms_db ~= -inf (clamped) and nz=0 (fully silent).

Interpreting the metrics
- Short waits after KO-ON typically show small but non-zero mean_abs/rms_db.
- Long waits after KO-OFF (KeyOff) should trend toward silence: nz -> 0, rms_db -> very low.
- If all windows are zero:
  - Check that address/data port write sequence is used (done in our bridge).
  - Ensure VGM actually hits KO-ON edges with valid frequency/instrument.

Next steps
- Add per-operator EG phase/level getters (analysis build) to inspect decays precisely.
- Add gate auto-estimator using tail dB at next KeyOn.