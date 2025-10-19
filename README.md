# eseopl3patcher

A tool to convert VGM files for YM2413, YM3812, YM3526, Y8950, VRC7, and YMF281B chips into YMF262 (OPL3) VGM files.

---

## Overview

`eseopl3patcher` is a command-line tool for converting VGM files supporting OPL chips (YM2413, YM3812, YM3526, Y8950) into OPL3 (YMF262) format.  
It features chorus/detune effects using OPL3's extended channels, flexible stereo and volume settings, and lets you choose instrument ROMs for YM2413/OPLL conversion (VRC7 and YMF281B supported).

---

## Supported Chips

- **YM2413 (OPLL)**
- **VRC7 (OPLL VRC7 preset)**
- **YMF281B (OPLLP YMF281B preset)**
- **YM3812 (OPL2)**
- **YM3526 (OPL)**
- **Y8950 (OPL, ADPCM is not converted)**
- Supports VGM version 1.50 and newer

---

## Main Features

- Converts OPL2/OPLL commands to OPL3 (YMF262) commands
- Detune/chorus effect using OPL3 extended channels (ch9–17)
- Panning and volume control (`-ch_panning`, `-vr0`, `-vr1`)
- Selectable instrument ROMs for YM2413 conversion (`--preset`)
- Automatic GD3 tag generation (conversion info and parameters)
- Detailed debug mode (`-verbose`)
- Automatic or manual chip selection when multiple OPL chips are present
- Flexible conversion control via command-line options
- **`--keep_source_vgm` option for simultaneous YM2413 and OPL3 playback**

---

## Detune and Correction

- `-detune <value>` : Overall detune amount (percent, e.g. `20` → +20%)
    - Detune curve is automatically adjusted by pitch range: detune is weaker for very high/low notes
    - Extreme values are clamped by `detune_limit`
- `-detune_limit <value>` : Maximum detune amount (absolute value, e.g. `4` → limited to ±4)
    - Prevents unnatural pitch shifts by limiting total detune

---

## YM2413/OPLL Preset Selection

- `--preset <YM2413|VRC7|YMF281B>`  
    Choose the instrument ROM for YM2413 (OPLL) conversion. VRC7 and YMF281B presets are supported.
    - `YM2413`: Original OPLL instrument set (default)
    - `VRC7`: VRC7 instrument ROM
    - `YMF281B`: YMF281B compatible ROM
    - Example: `--preset VRC7`
    - Has no effect when converting other chips

---

## Simultaneous YM2413 and OPL3 Playback (`--keep_source_vgm`)

- `--keep_source_vgm`  
    Available only when converting YM2413 input.  
    **Retains original YM2413 register commands at the beginning of the output VGM file.**
    - Enables simultaneous playback on both YM2413 (MSX-MUSIC, etc.) and OPL3 (YMF262) devices
    - Useful for dual-FM environments such as MSX Music + pseudo-OPL3 RAM, SoundCoreSLOT EX|EXG + pseudo-OPL3 RAM, NanoDrive7, etc.
    - Normally, YM2413 commands are removed; with this option, both chips can play together

    **Use cases:**
    - Simultaneous VGM output to MSX-MUSIC (YM2413) and OPL3 RAM expansion on MSX
    - Dual FM playback on NanoDrive7, SoundCoreSLOT EX/EXG, etc.
    - Mixing both FM chips using VGMPlayer

---

## Main Command-line Options

| Option | Description | Default |
|--------|-------------|---------|
| `<input.vgm>` | Input VGM file | Required |
| `<detune>` | Detune amount (%; e.g. 20, -8) | Required |
| `[keyon_wait]` | KeyOn/Off wait time (samples) | 0 |
| `[creator]` | Creator name (GD3 tag) | eseopl3patcher |
| `-o <output.vgm>` | Output file name | `<input>OPL3.vgm` |
| `-ch_panning <0|1>` | Panning mode | 0 |
| `-vr0 <float>` | Port0 volume ratio | 1.0 |
| `-vr1 <float>` | Port1 volume ratio | 0.8 |
| `-detune_limit <float>` | Detune amount limit | 4.0 |
| `--preset <YM2413|VRC7|YMF281B>` | Instrument ROM for YM2413 conversion | YM2413 |
| `--keep_source_vgm` | Retain YM2413 commands for simultaneous OPL3 playback | Off |
| `--convert-ym2413` | Convert only YM2413 | (Auto) |
| `--convert-ym3812` | Convert only YM3812 | (Auto) |
| `--convert-ym3526` | Convert only YM3526 | (Auto) |
| `--convert-y8950` | Convert only Y8950 | (Auto) |
| `-verbose` | Detailed debug output | Off |

---

## Usage

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] \
    [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] \
    [-detune_limit <float>] [--preset <YM2413|VRC7|YMF281B>] \
    [--keep_source_vgm] [--convert-ym2413] [--convert-ym3812] [--convert-ym3526] [--convert-y8950] \
    [-verbose]
```

**Options can appear in any order; only `<input.vgm>` and `<detune>` are required.**

---

### Examples

```sh
eseopl3patcher song.vgm 20
eseopl3patcher song.vgm 100 -o song_OPL3.vgm -ch_panning 1 -detune_limit 4
eseopl3patcher song.vgm 25 --preset VRC7
eseopl3patcher song.vgm 15 --convert-ym3526 --preset YMF281B
eseopl3patcher song.vgm 2.5 -vr1 0.8
eseopl3patcher song.vgm -8 -ch_panning 1 -verbose
eseopl3patcher song.vgm 30 --keep_source_vgm
```

---

## Download

[![Download latest release](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows binaries are available from the [latest release page](https://github.com/emef2247/eseopl3patcher/releases/latest).

---

## Build

```sh
make win
# or
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```

---

## VGM Tools Integration

For testing and analysis, official VGM tools (`vgm2txt`, `VGMPlay`, etc.) can be automatically downloaded and built into the `tools/` directory:

```bash
bash tools/fetch_vgm_tools.sh
```

See the `tools/` directory README for details.

---

## License

MIT License  
External tools and code follow their respective licenses.

---

## Author

[@emef2247](https://github.com/emef2247)