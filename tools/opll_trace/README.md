# ym2413_vgm_trace (Standalone YMFM OPLL CSV tracer)

Purpose:
- Feed a VGM file directly into upstream YMFM's OPLL (YM2413) implementation
- Produce a CSV timeline similar to eseopl3patcher tracing (KO_ON/OFF + WAIT stats)
- Keep upstream source unmodified

Build (example using g++):
```
g++ -std=c++17 -O2 -I../../third_party/ymfm_upstream/src \
    main.cpp vgm_parser.cpp csv_writer.cpp opll_probe.cpp \
    -o ym2413_vgm_trace
```

Run:
```
./ym2413_vgm_trace input.vgm trace.csv
```

CSV columns:
- session_id      : Incremented each KO_ON
- ch              : Channel of the event (WAIT rows: focus channel or -1)
- t_samples       : Cumulative sample time
- wait_samples    : WAIT row only: number of samples advanced
- mean_abs        : Mean absolute sample amplitude
- rms_db          : RMS expressed in dBFS (fullscale=32768)
- nz              : Index (1-based) of last non-zero sample inside that wait window
- att_*           : (placeholders: -1 / -240.00) reserved for future envelope integration
- event           : KO_ON / KO_OFF / WAIT
- reg2n_hex       : Raw reg2n at KO events

Next steps:
1. Envelope extraction
   - Add a bridge to read operator attenuation & phase from ymfm::opll_device
2. Add RECO_OFF logic (policy-based recommended key off)
3. Integrate 'actual vs recommended' KeyOff gap analysis