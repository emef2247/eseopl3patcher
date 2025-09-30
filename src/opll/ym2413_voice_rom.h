#ifndef YM2413_VOICE_ROM_H
#define YM2413_VOICE_ROM_H

/**
 * YM2413 (OPLL) Built-in Instrument ROM - Official Preset Table
 * -------------------------------------------------------------
 * Source:
 *   - Yamaha YM2413 Application Manual (1986), "INSTRUMENT DATA" Table (p. 19, Table 3)
 *     https://map.grauw.nl/resources/sound/yamaha_ym2413.pdf
 *   - Used by many emulators (e.g., openMSX, MAME) as the canonical reference.
 * Notes:
 *   - Each melodic preset: 8 bytes [modulator(4), carrier(4)]
 *   - Array index: [0]=Violin (Preset 1), [1]=Guitar (2), ..., [14]=Electric Guitar (15)
 *   - INST register value 1..15 corresponds to array index+1.
 *   - Byte format:
 *       [0]=AM/VIB/EG/KSR/ML, [1]=KSL/TL, [2]=AR/DR, [3]=SL/RR
 *       (AM:bit7, VIB:6, EG:5, KSR:4, ML:3-0 / KSL:7-6, TL:5-0 / AR:7-4, DR:3-0 / SL:7-4, RR:3-0)
 *   - User patch (INST=0) is not included here; should be set at runtime.
 */

static const unsigned char YM2413_VOICES[15][8] = {
    // Violin (Preset 1)
    {0x71,0x61,0x1E,0x17, 0xD0,0x78,0x00,0x17},
    // Guitar (Preset 2)
    {0x13,0x41,0x16,0x0E, 0xD8,0xF6,0x23,0x12},
    // Piano (Preset 3)
    {0x13,0x01,0x19,0x0F, 0xD8,0xF6,0x13,0x02},
    // Flute (Preset 4)
    {0x31,0x61,0x1A,0x0D, 0xC8,0x64,0x70,0x13},
    // Clarinet (Preset 5)
    {0x22,0x21,0x1E,0x06, 0xE0,0x76,0x22,0x12},
    // Oboe (Preset 6)
    {0x21,0x61,0x1D,0x07, 0x82,0x6A,0x20,0x11},
    // Trumpet (Preset 7)
    {0x23,0x21,0x22,0x17, 0xA2,0x64,0x21,0x61},
    // Organ (Preset 8)
    {0x61,0x61,0x0E,0x07, 0xA0,0x71,0x00,0x13},
    // Horn (Preset 9)
    {0x23,0x21,0x1E,0x07, 0xE0,0x71,0x22,0x21},
    // Synthesizer (Preset 10)
    {0x21,0x20,0x0C,0x08, 0xA0,0x73,0x22,0x21},
    // Harpsichord (Preset 11)
    {0x13,0x41,0x18,0x0F, 0xF8,0xF4,0x23,0x13},
    // Vibraphone (Preset 12)
    {0x31,0x61,0x0C,0x08, 0xB0,0x72,0x73,0x13},
    // Synth Bass (Preset 13)
    {0x61,0x61,0x1E,0x07, 0xD0,0x70,0x00,0x17},
    // Wood Bass (Preset 14)
    {0x21,0x61,0x1E,0x07, 0xC8,0x76,0x22,0x13},
    // Electric Guitar (Preset 15)
    {0x13,0x01,0x1A,0x0F, 0xE8,0xF6,0x13,0x02}
};

/*
  YM2413 Instrument Map:
    INST=1  Violin          [0]
    INST=2  Guitar          [1]
    INST=3  Piano           [2]
    INST=4  Flute           [3]
    INST=5  Clarinet        [4]
    INST=6  Oboe            [5]
    INST=7  Trumpet         [6]
    INST=8  Organ           [7]
    INST=9  Horn            [8]
    INST=10 Synthesizer     [9]
    INST=11 Harpsichord     [10]
    INST=12 Vibraphone      [11]
    INST=13 Synth Bass      [12]
    INST=14 Wood Bass       [13]
    INST=15 Electric Guitar [14]
    INST=0  User patch (not included here)
*/

/**
 * YM2413 (OPLL) Built-in Rhythm Instrument ROM - Official Rhythm Table
 * --------------------------------------------------------------------
 * Source:
 *   - Yamaha YM2413 Application Manual (1986), "RHYTHM INSTRUMENT DATA" Table (p. 20, Table 4)
 *     https://map.grauw.nl/resources/sound/yamaha_ym2413.pdf
 *   - Used for rhythm mode channels (BD, SD, TOM, CYM, HH).
 * Notes:
 *   - Each entry: 8 bytes (for BD: 2OP, for SD/TOM/CYM/HH: 1OP, use first 4 bytes)
 *   - Array index: [0]=Bass Drum (BD), [1]=Snare Drum (SD), [2]=Tom-Tom (TOM), [3]=Cymbal (CYM), [4]=Hi-Hat (HH)
 *   - When using voice id 15-19 for rhythm, map as:
 *       15=BD, 16=SD, 17=TOM, 18=CYM, 19=HH
 *   - Channel/operator relation:
 *       BD  = ch6 (mod+car, 2OP)
 *       SD  = ch7 (car)
 *       TOM = ch8 (mod)
 *       CYM = ch8 (car)
 *       HH  = ch7 (mod)
 */

static const unsigned char YM2413_RHYTHM_VOICES[5][8] = {
    // Bass Drum (BD, 2OP: modulator[4], carrier[4])
    {0x21,0x01,0x0C,0x07, 0xA1,0x01,0x0C,0x07},
    // Snare Drum (SD, 1OP: modulator[4], rest zero)
    {0x01,0x01,0x08,0x05, 0,0,0,0},
    // Tom-Tom (TOM, 1OP: modulator[4], rest zero)
    {0x01,0x01,0x0A,0x04, 0,0,0,0},
    // Cymbal (CYM, 1OP: modulator[4], rest zero)
    {0x11,0x01,0x08,0x05, 0,0,0,0},
    // Hi-Hat (HH, 1OP: modulator[4], rest zero)
    {0x31,0x01,0x08,0x04, 0,0,0,0}
};

/*
  Rhythm Instrument Map:
    [0] Bass Drum (BD, 2OP)
    [1] Snare Drum (SD, 1OP)
    [2] Tom-Tom (TOM, 1OP)
    [3] Cymbal (CYM, 1OP)
    [4] Hi-Hat (HH, 1OP)
    (For OPLL voice_id: 15=BD, 16=SD, 17=TOM, 18=CYM, 19=HH)
*/

#endif // YM2413_VOICE_ROM_H