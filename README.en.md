# eseopl3patcher

A tool to convert VGM files for YM2413, YM3812, YM3526, Y8950, VRC7, and YMF281B sound chips into YMF262 (OPL3) VGM files.

---

## Overview

`eseopl3patcher` is a command-line tool that converts VGM files for OPL-family sound chips (YM2413, YM3812, YM3526, Y8950) into the OPL3 (YMF262) format.  
It features chorus (detune) effects using OPL3’s extended channels, as well as flexible stereo/panning and volume controls.  
When converting YM2413/OPLL, you can also select compatible voice presets such as VRC7, YMF281B, and YM2423.

---

## Supported Chips

- **YM2413  (OPLL)**
- **VRC7    (OPLL VRC7 compatible preset)**
- **YMF281B (OPLLP YMF281B compatible preset)**
- **YM2423   (OPLL-X) [Added in v2.1.0]**
- **YM3812 (OPL2)**
- **YM3526 (OPL)**
- **Y8950 (OPL, ADPCM part not converted)**
- Supports VGM data format v1.50 and above

---

## Features

- Converts OPL2/OPLL commands to OPL3 (YMF262) commands
- Chorus/detune effects using OPL3’s extended channels (ch9–17)
- Panning and volume adjustment (`-ch_panning`, `-vr0`, `-vr1`)
- When converting YM2413, you can select compatible voice presets (YM2413, VRC7, YMF281B, YM2423) with `--preset` [since v2.1.0]
- Automatically generates GD3 tags (conversion info, parameters)
- Detailed debug mode (`-verbose`)
- Automatically detects or explicitly selects among multiple OPL-family chips
- Flexible conversion control via command-line options
- **Supports simultaneous playback of YM2413 and OPL3 with the `--keep_source_vgm` option**

---

## Detune & Correction

- `-detune <value>` : Applies a detune amount (in percent, e.g. `20` for +20%) to all frequencies.
    - The detune curve is automatically compensated for high and low notes.
    - Even for extreme values (e.g. `100`), the actual effect is limited by `detune_limit`.
- `-detune_limit <value>` : Maximum detune amount (absolute value, e.g. `4` means within ±4).
    - Prevents excessive pitch changes from detune so that the result remains natural.

---

## YM2413/OPLL Preset & Voice Source Selection (enhanced in v2.1.0)

- `--preset <YM2413|VRC7|YMF281B|YM2423>`
    - Selects which compatible voice preset is used for YM2413 (OPLL) conversion.
    - `YM2413`: Original OPLL voices (default)
    - `VRC7`: VRC7 compatible voice preset
    - `YMF281B`: YMF281B compatible voice preset
    - `YM2423`: OPLL-X compatible voice preset
    - Example: `--preset VRC7`
    - Has no effect when converting chips other than YM2413/OPLL.

- `--preset_source <YMVOICE|YMFM|EXPERIMENT>`
    - Selects the source for the compatible voice preset:
        - `YMVOICE`: Voices from the [ym-voice project](https://github.com/digital-sound-antiques/ym-voice)
        - `YMFM`: Voices from [Copyright-free-OPLL(x)-ROM-patches](https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
        - `EXPERIMENT`: Experimental version based on YMFM voices with custom modifications
    - **For YM2423, only `YMFM` or `EXPERIMENT` can be selected** (`YMVOICE` will internally use the same as `YMFM`).

---

## Simultaneous Playback of YM2413 and OPL3 (`--keep_source_vgm`)

- `--keep_source_vgm`  
    Only available when the input is YM2413 VGM data.  
    **This option leaves the original YM2413 register write commands at the start of the converted VGM.**
    - Allows you to create VGM data that plays both YM2413 (e.g. MSX-MUSIC) and OPL3 (YMF262) simultaneously.
    - Intended for systems such as MSX Music + pseudo OPL3 RAM, SoundCoreSLOT EX|EXG + pseudo OPL3 RAM, NanoDrive7, etc., where YM2413 and OPL3 can play together.
    - In normal conversion, YM2413 commands are removed, but this option enables "dual playback".

    **Use cases:**
    - Simultaneous output for both MSX-MUSIC (YM2413) and OPL3 expansion RAM on MSX
    - Dual FM playback on NanoDrive7 or SoundCoreSLOT EX/EXG expansion cards
    - Mixing both FM chips in VGMPlayer, etc.

---

## Main Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `<input.vgm>` | Input VGM file | Required |
| `<detune>` | Detune value (percent, e.g. 20, -8) | Required |
| `[keyon_wait]` | KeyOn/Off wait time (in samples) | 0 |
| `[creator]` | Creator name (for GD3 tag) | eseopl3patcher |
| `-o <output.vgm>` | Output filename | `<input>OPL3.vgm` |
| `-ch_panning <0|1>` | Panning mode | 0 |
| `-vr0 <float>` | Port0 volume ratio | 1.0 |
| `-vr1 <float>` | Port1 volume ratio | 0.8 |
| `-detune_limit <float>` | Detune upper limit | 4.0 |
| `--preset <YM2413|VRC7|YMF281B|YM2423>` | Compatible preset for YM2413 conversion | YM2413 |
| `--preset_source <YMVOICE|YMFM|EXPERIMENT>` | Source for compatible voice preset | YMFM |
| `--keep_source_vgm` | Keep YM2413 commands for dual playback | Disabled |
| `--convert-ym2413` | Convert YM2413 only | (auto) |
| `--convert-ym3812` | Convert YM3812 only | (auto) |
| `--convert-ym3526` | Convert YM3526 only | (auto) |
| `--convert-y8950` | Convert Y8950 only | (auto) |
| `-verbose` | Detailed debug output | Off |

---

## Usage

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] \
    [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] \
    [-detune_limit <float>] [--preset <YM2413|VRC7|YMF281B|YM2423>] \
    [--preset_source <YMVOICE|YMFM|EXPERIMENT>] \
    [--keep_source_vgm] [--convert-ym2413] [--convert-ym3812] [--convert-ym3526] [--convert-y8950] \
    [-verbose]
```

**Options can be specified in any order and are optional except `<input.vgm>` and `<detune>`.**

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
eseopl3patcher song.vgm 10 --preset YM2423 --preset_source EXPERIMENT
```

---

## Download

[![Download latest release](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

You can download binaries for Linux and Windows from the [latest release page](https://github.com/emef2247/eseopl3patcher/releases/latest).

---

## Building

```sh
make win
# or
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```

---

## License

MIT License  
---

## Author

[@emef2247](https://github.com/emef2247)