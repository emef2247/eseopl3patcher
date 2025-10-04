# YMFM OPLL VGM Trace Tool

## Purpose
- Feed VGM files directly into YMFM's OPLL (YM2413) implementation
- Produce CSV timeline with key events (KO_ON/OFF + WAIT stats)
- Produce VCD (Value Change Dump) waveform traces for signal analysis
- Use instrumented YMFM source to capture internal state

## Build
```bash
cd tools/opll_trace
make clean && make
```

This builds `vgmrender_opll_trace` with both CSV and VCD tracing enabled.

## Usage
```bash
# Basic usage - outputs to default files
./vgmrender_opll_trace input.vgm -o output.wav

# Specify custom output paths via environment variables
OPLL_TRACE_CSV=my_trace.csv OPLL_VCD=my_dump.vcd ./vgmrender_opll_trace input.vgm -o output.wav
```

## Output Files

### CSV Trace (enabled with -DESEOPL3_OPLL_TRACE)
Default: `opll_trace.csv`

Columns:
- `session_id`: Incremented on each key-on event
- `ch`: Channel number (or -1 for WAIT events)
- `t_samples`: Cumulative sample timestamp
- `event`: KO_ON, KO_OFF, or WAIT
- `reg2n_hex`: Register 0x20+ch value at key events

### VCD Trace (enabled with -DESEOPL3_OPLL_VCD)
Default: `opll_dump.vcd`

Signals captured for Channel 0:
- `lfo_am_counter`: LFO amplitude modulation counter (8 bits)
- `out_l`, `out_r`: Left/right output levels (32 bits)
- `ch0_key`: Key on/off state (1 bit)
- `ch0_freq`: Block and frequency value (16 bits)
- `ch0_instr`: Instrument/patch number (8 bits)
- `ch0_vol`: Volume/TL (8 bits)
- `ch0_mod_phase`: Modulator operator phase (32 bits)
- `ch0_mod_env`: Modulator envelope level (16 bits)
- `ch0_mod_eg_state`: Modulator EG state (8 bits)
- `ch0_car_phase`: Carrier operator phase (32 bits)
- `ch0_car_env`: Carrier envelope level (16 bits)
- `ch0_car_eg_state`: Carrier EG state (8 bits)

View VCD files with waveform viewers like GTKWave, or parse programmatically.

## Build Configuration

The Makefile defines:
- `-DESEOPL3_OPLL_TRACE`: Enables CSV trace output
- `-DESEOPL3_OPLL_VCD`: Enables VCD trace output

Both can be used independently or together.