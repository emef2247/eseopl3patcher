#include <math.h>
#include <stdio.h>
#include "../opl3/opl3_convert.h"
#include "../opll/opll_state.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include "../opll/ym2413_voice_roms.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>  // getenv
#include <stdarg.h>
#include "opll2opl3_conv.h"

#define YM2413_REGS_SIZE 0x40

// Virtual channel IDs for rhythm part
#define CH_BD  6   // Bass Drum
#define CH_SD  7   // Snare Drum / Tom
#define CH_CYM 8   // Cymbal / HiHat

#define SAMPLE_RATE 44100.0
#define MIN_GATE_MS 2         /* ensure at least ~2ms key-on */
#define MIN_GATE_SAMPLES ((uint32_t)((MIN_GATE_MS * SAMPLE_RATE) / 1000.0 + 0.5))
#define MAX_PENDING_MS 50     /* max time to wait for missing params */
#define MAX_PENDING_SAMPLES ((uint32_t)((MAX_PENDING_MS * SAMPLE_RATE) / 1000.0 + 0.5))

static inline bool is_keyon_bit_set(uint8_t val) { return (val & 0x10) != 0; }

static void opll_debug_log(const CommandOptions *opts, const char *fmt, ...) {
    if (!opts || !opts->debug.verbose) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static inline void opll2opl3_debug_log(VGMContext *p_vgmctx, char *tag1,const char *tag2, int ch, const CommandOptions *p_opts) {
    if (p_opts->debug.verbose) {
        OPLL2OPL3_Scheduler      *s = &(p_vgmctx->opll_state.sch);
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        fprintf(stderr,
        "[OPLL2OPL3][%s][%s] virtual_time:%llu emit_time:%llu ch=%d TL(%d) Voice(%d) FnumL(%d) --- "
        "Active %d Pending %d PendingOff %d PrevKey %d KeyNow %d\n",
        tag1,tag2,
        (unsigned long long)s->virtual_time,
        (unsigned long long)s->emit_time,
        ch,
        p->has_tl, p->has_voice, p->has_fnum_low,
        p->is_active, p->is_pending, p->is_pending_keyoff,
        p->has_keybit_stamp, p->has_keybit
        );
    }
}

void opll2opl3_init_command_buffer(VGMContext *p_vgmctx, const CommandOptions *p_opts) {
        OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);
        p_command_buffer->ch = -1;
        p_command_buffer->has_reg0x1n = false;
        p_command_buffer->has_reg0x2n = false;
        p_command_buffer->has_reg0x3n = false;
        p_command_buffer->reg0x1n = 0x0;
        p_command_buffer->reg0x2n = 0x0;
        p_command_buffer->reg0x3n = 0x0;
        p_command_buffer->wait_count = 0;
        p_command_buffer->wait1 = 0;
        p_command_buffer->wait2 = 0;
}

void opll2opl3_init_scheduler(VGMContext *p_vgmctx, const CommandOptions *p_opts) {
    OPLL2OPL3_Scheduler *s = &(p_vgmctx->opll_state.sch);
    memset(s, 0, sizeof(*s));
    s->virtual_time = 0;
    s->emit_time = 0;
    s->wait_count = 0;

    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ch++) {
        memset(&s->ch[ch], 0, sizeof(OPLL2OPL3_PendingChannel));
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];

        // Register-arrival flags
        p->has_fnum_low   = false; // 1n (0x10..0x18)
        p->has_fnum_high  = false; // 2n (0x20..0x28 includes key bit and block)
        p->has_tl         = false; // 0x30..0x38
        p->has_voice      = false; // 0x30..0x38 upper bits or 0x00..0x07 user patch

        p->ignore_first_tl = true; /* 常に初期は破棄モード */

        // Register cache (for edge detect + diagnostics)
        uint8_t last_reg_10 = 0xFF;
        uint8_t last_reg_20 = 0xFF;
        uint8_t last_reg_30 = 0xFF;

        // Key/edge state
        p->has_keybit_stamp = false;  // last observed register key bit (register state)
        p->has_keybit      = false;  // last seen register's key bit presence in current update
        p->is_pending     = false ; // we delayed keyon because not all pieces arrived
        p->is_active      = false ; // we have emitted KeyOn for this channel in OPL output
        p->is_pending_keyoff = false; //  a KeyOff is pending to be flushed (used to ensure min gate)
        p->is_keyoff_forced  = false; // internal marker for forced (retrigger) off

    /* stored param values (latest seen while pending) */
        p->fnum_low = 0;
        p->fnum_high = 0;   // lower 2 bits used
        p->fnum_comb = 0;  // assembled 10-bit fnum (maintain)
        p->block = 0;
        p->tl = 0;
        p->voice_id = 0;

    /* time stamp */
        p->keyon_time = 0;    // virtual time when KeyOn was first seen
    }
}

/** Calculate OPLL frequency for debugging */
double calc_opll_frequency(double clock, unsigned char block, unsigned short fnum) {
    // YM2413 (OPLL) frequency calculation based on observed behavior:
    // f ≈ (clock / 72) / 2^18 * fnum * 2^block
    // Example: clock=3579545, block=2, fnum=500 -> approximately 379.3 Hz
    const double base = (clock / 72.0) / 262144.0; // 2^18
    return base * (double)fnum * ldexp(1.0, block);
}

/* Convert OPLL fnum/block to OPL3 fnum/block (10-bit fnum, 3bit block)
   returns true if conversion within range */
bool convert_fnum_block_from_opll_to_opl3(double opll_clock, double opl3_clock, uint8_t opll_block, uint16_t opll_fnum, uint16_t *best_f, uint8_t *best_b)
{

    double freq = calc_opll_frequency(opll_clock, opll_block, opll_fnum);
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_pref_block(freq, opl3_clock, best_b, best_f, &best_err_cents, (int)opll_block);
    return true;
}

/**
 * OPLL to OPL3 frequency mapping with optional fast-path.
 * Fast-path heuristics:
 * - OPLL→OPL3 (dst≈4×src): passthrough (FNUM/BLOCK unchanged)
 * - OPLL→OPL2/Y8950 (dst≈src): FNUM×2, BLOCK unchanged, clamp to 1023
 * - Otherwise: precise mapping using opl3_find_fnum_block_with_ml_cents
 */
static void opll2opl3_freq_mapping(uint8_t reg1n, uint8_t reg2n, double src_clock, double dst_clock, uint8_t *p_best_f, uint8_t *p_best_block)
{
    uint8_t  opll_block = (uint8_t)((reg2n >> 1) & 0x07);
    uint16_t opll_fnum9 = (uint16_t)(((reg2n & 0x01) << 8) | reg1n);

    // Skip if fnum==0 (prevents 0Hz KeyOn)
    if (opll_fnum9 == 0) {
        if (p_best_block) *p_best_f = reg1n;
        if (p_best_block) {
            uint8_t fnum_msb_2b = (reg2n & 0x01);
            uint8_t block_3b    = (reg2n >> 1) & 0x07;
            *p_best_block = (uint8_t)(fnum_msb_2b | (block_3b << 2));
        }
        if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
            fprintf(stderr, "[FREQMAP] (skip: fnum==0) OPLL blk=%u fnum=%u\n",
                    opll_block, opll_fnum9);
        }
        return;
    }

    // Precise mapping (default behavior)
    double freq = calc_opll_frequency(src_clock, opll_block, opll_fnum9);
    unsigned char best_b = 0;
    unsigned short best_f = 0;
    double best_err_cents = 0.0;
    opl3_find_fnum_block_with_ml_cents(freq, dst_clock, &best_b, &best_f, &best_err_cents,
                                       (int)opll_block, 0.0, 0.0);

    *p_best_f      = best_f;
    *p_best_block =  best_b;

    if (getenv("ESEOPL3_FREQMAP_DEBUG")) {
        fprintf(stderr,
            "[FREQMAP] OPLL blk=%u fnum=%u Hz=%.6f -> OPL3 blk=%u fnum=%u (err_cents=%.2f)\n",
            opll_block, opll_fnum9, freq, (unsigned)best_b, (unsigned)best_f, best_err_cents);
    }
}


int  opll2opl3_emit_reg_write(VGMContext *p_vgmctx, uint8_t addr, uint8_t val, const CommandOptions *p_opts) 
{
    // Emit actual OPL3 write (handles dual port if needed)
    OPLL2OPL3_Scheduler *s = &(p_vgmctx->opll_state.sch);
    uint8_t last_val =  s->last_emitted_reg_val[addr];
    bool first_access = !s->accessed[addr];
    int wrote_bytes = 0;

    if (p_opts->debug.verbose) {
        fprintf(stderr, "[EMIT][Reg Write] time=%u addr=%02X val=%02X emit_time=%u\n", (unsigned)s->virtual_time, addr, val, (unsigned)s->emit_time);
    }
   if (first_access || val != last_val) {
        int bytes = duplicate_write_opl3(p_vgmctx, addr, val, p_opts);
        if (should_account_addtional_bytes_pre_loop(&(p_vgmctx->status))) wrote_bytes += bytes;

        s->accessed[addr] = true;
    }
    s->last_emitted_reg_val[addr] = val;

    return wrote_bytes;
}

int emit_wait(VGMContext *p_vgmctx, uint16_t samples, OPLL2OPL3_Scheduler *s,const CommandOptions *p_opts) {
    int wrote_bytes = 0;
    if (samples == 0) {
        if (p_opts->debug.verbose) {
            fprintf(stderr,"[EMIT][WAIT] emit_time was ignored by samples = %u\n", samples);
        }
        return wrote_bytes;
    }

    wrote_bytes += vgm_wait_samples(p_vgmctx, samples);
    // Advance emitted timeline
    s->emit_time += samples;
    if (p_opts->debug.verbose) {
        fprintf(stderr,"[EMIT][WAIT] emit_time advanced by %u -> %u\n", samples, (unsigned)s->emit_time);
    }
    return wrote_bytes;
}

/** Returns true if register is OPLL FNUM (pitch lower 8bit) */
static inline bool is_opll_fnum_reg(uint8_t reg)
{
    return (reg >= 0x10 && reg <= 0x18);
}

/** Returns true if register is KeyOn */
static inline bool is_opll_keyon_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) != 0;  // bit4=KeyOn
    return false;
}

/** Returns true if register is KeyOff */
static inline bool is_opll_keyoff_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) == 0;  // bit4=0ならKeyOff
    return false;
}

/** Returns true if register is TL (Total Level/Volume) */
static inline bool is_opll_tl_reg(uint8_t reg)
{
    return (reg >= 0x30 && reg <= 0x38);
}

/** Returns true if register is voice/instrument */
static inline bool is_opll_voice_reg(uint8_t reg)
{
    // User patch definition
    if (reg <= 0x07)
        return true;
    // Channel-specific instrument setting
    if (reg >= 0x30 && reg <= 0x38)
        return true;
    return false;
}

/**
 * Get operator slot number (modulator) for OPL3.
 */
static inline uint8_t opl3_local_mod_slot(uint8_t ch_local) {
    return (uint8_t)((ch_local % 3) + (ch_local / 3) * 8);
}

/**
 * Get operator slot number (carrier) for OPL3.
 */
static inline uint8_t opl3_local_car_slot(uint8_t ch_local) {
    return (uint8_t)(opl3_local_mod_slot(ch_local) + 3);
}

/**
 * Get OPL3 operator register address.
 */
static inline uint8_t opl3_opreg_addr(uint8_t base, uint8_t ch_local, int is_carrier) {
    uint8_t slot = is_carrier ? opl3_local_car_slot(ch_local) : opl3_local_mod_slot(ch_local);
    return (uint8_t)(base + slot);
}

/** Sustain Level (SL) passthrough (0-15) */
static inline uint8_t opll2opl_sl(uint8_t sl) { return sl & 0x0F; }

/** Compute unique hash for OPLL voice parameters */
uint32_t compute_opll_voice_hash(uint8_t voice_id, uint8_t tl, uint8_t algo_fb, uint8_t wave_bits)
{
    return ((uint32_t)voice_id << 24)
         | ((uint32_t)tl << 16)
         | ((uint32_t)algo_fb << 8)
         | (uint32_t)wave_bits;
}

/* --- YM2413→OPL変換補助 --- */
static inline uint8_t _R(uint8_t rate) {
    if (rate > 15) return 15;
    if (rate == 0) return 0;
    return rate + 1;
}

static inline uint8_t _KLFix(uint8_t kl) {
    switch (kl) {
        case 1: return 2;
        case 2: return 1;
        case 3: return 3;
        default: return kl;
    }
}

static inline int get_mod_offset(int ch) {
    static const int tbl[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    return tbl[ch];
}

static inline uint8_t ym2413_to_opl3_tl(uint8_t inst_tl, uint8_t volume) {
    int tl = (inst_tl << 2) + (volume << 2);
    if (tl > 63) tl = 63;
    return (uint8_t)tl;
}

/*
 * YMFM_YM2413_VOICES[18][8] → EXPERIMENT_YM2413_PRESET[18][8]
 * Each voice is commented with its number and English instrument name.
 */
void convert_ymfm_2413_to_experiment(const uint8_t ymfm[18][8], uint8_t exp[18][8]) {
    // Copy all 18×8 values from YMFM as base
    for (int i = 0; i < 18; ++i)
        for (int j = 0; j < 8; ++j)
            exp[i][j] = ymfm[i][j];

    // 1: Violin
    exp[0][4] = (ymfm[0][4] & 0x0F) | 0xD0; // Modulator TL: force total level upper bits to 0xD
    exp[0][5] = (ymfm[0][5] & 0xF0) | 0x08; // Modulator RS/AM/FM: replace lower nibble with 0x8

    // 2: Guitar
    exp[1][4] = (ymfm[1][4] & 0xF8) | 0xD8; // Modulator TL: preserve upper 5 bits, set lower 3 bits to 0x18

    // 3: Piano
    exp[2][5] = ymfm[2][5] + 0x10;          // Modulator RS/AM/FM: increase by 0x10 (adds brightness)
    exp[2][6] = (ymfm[2][6] & 0xF0) | 0x21; // Carrier AR: keep upper nibble, replace lower with 0x1

    // 4: Flute
    exp[3][0] = (ymfm[3][0] & 0xF0) | 0x11; // Modulator AR: preserve upper bits, set lower to 0x1
    exp[3][4] = (ymfm[3][4] & 0xF0) | 0x8D; // Modulator TL: change lower nibble to 0xD (softer tone)

    // 5: Clarinet
    exp[4][0] = (ymfm[4][0] & 0xF0) | 0x32; // Modulator AR: set lower nibble to 0x2
    exp[4][4] = (ymfm[4][4] & 0xE0) | 0xE1; // Modulator TL: preserve top 3 bits, apply new lower pattern 0xE1
    exp[4][6] = (ymfm[4][6] & 0xF0) | 0x01; // Carrier AR: adjust lower bits to soften attack

    // 6: Oboe
    exp[5][6] = ymfm[5][6] & 0xF0;          // Carrier AR: clear lower nibble (remove fast attack)

    // 7: Trumpet
    exp[6][5] = ymfm[6][5] & ~0x0E;         // Modulator RS/AM/FM: clear bits 1–3 (disable certain mod flags)
    exp[6][6] = (ymfm[6][6] & 0xF0) | 0x11; // Carrier AR: preserve upper nibble, set lower to 0x1

    // 8: Organ
    exp[7][0] = (ymfm[7][0] & 0xF0) | 0x33; // Modulator AR: slightly longer attack
    exp[7][2] = ymfm[7][2];                 // Modulator SR: unchanged
    exp[7][3] = (ymfm[7][3] & 0xF0) | 0x13; // Modulator SL/RR: adjust lower nibble
    exp[7][4] = (ymfm[7][4] & 0xF0) | 0xB0; // Modulator TL: set lower nibble for mellower tone
    exp[7][5] = (ymfm[7][5] & 0xF0) | 0x70; // Modulator RS/AM/FM: adjust to 0x70

    // 9: Horn
    exp[8][0] = (ymfm[8][0] & 0xF0) | 0x61; // Modulator AR: lower nibble → 0x1 for smoother attack

    // 10: Synthesizer
    exp[9][0] = ymfm[9][0];                 // Modulator AR: unchanged
    exp[9][5] = (ymfm[9][5] & 0xF0) | 0xF0; // Modulator RS/AM/FM: set lower nibble fully on (rich harmonics)

    // 11: Harpsichord
    exp[10][0] = (ymfm[10][0] & 0xF0) | 0x33; // Modulator AR: tighter attack
    exp[10][4] = (ymfm[10][4] & 0xF0) | 0xEA; // Modulator TL: modify lower nibble (bright timbre)
    exp[10][5] = (ymfm[10][5] & 0xF0) | 0xEF; // Modulator RS/AM/FM: add strong modulation depth

    // 12: Vibraphone
    exp[11][0] = ymfm[11][0];                  // Modulator AR: unchanged
    exp[11][1] = (ymfm[11][1] & 0xC0) | 0xC1;  // Modulator DR: preserve upper 2 bits, lower nibble 0x1
    exp[11][2] = ymfm[11][2] & ~((1 << 1) | (1 << 2) | (1 << 5)); 
    // Modulator SR: clear bits 1,2,5 to reduce sustain and modulation

    // 13: Synth Bass
    exp[12][4] = (ymfm[12][4] & 0xF0) | 0xD2;  // Modulator TL: adjust brightness
    exp[12][5] = ymfm[12][5];                  // Modulator RS/AM/FM: unchanged
    exp[12][6] = (ymfm[12][6] & 0xF0) | 0x40;  // Carrier AR: lower nibble 0x0 → slower attack

    // 14: Acoustic Bass
    exp[13][2] = (ymfm[13][2] & 0xF0) | 0x55;  // Modulator SR: replace lower nibble
    exp[13][4] = (ymfm[13][4] & 0x0F) | 0xE4;  // Modulator TL: modify upper bits for softer tone
    exp[13][5] = (ymfm[13][5] & 0xF0) | 0x90;  // Modulator RS/AM/FM: lower nibble set to 0x0

    // 15: Electric Guitar
    exp[14][4] = (ymfm[14][4] & 0xF0) | 0xF1;  // Modulator TL: brighten timbre
    exp[14][5] = (ymfm[14][5] & 0xF0) | 0xE4;  // Modulator RS/AM/FM: moderate modulation
    exp[14][6] = (ymfm[14][6] & 0xF0) | 0xC0;  // Carrier AR: fast attack, clear tone

    // 16: Rhythm1 (BD)
    // → No modification required; copied directly from YMFM data

    // 17: Rhythm2 (SD/HH)
    exp[16][7] = (ymfm[16][7] & 0xF0) | 0x68;  // RR/AM/FM: preserve upper nibble, fix lower bits for percussion tone

    // 18: Rhythm3 (TT/CYM)
    // → No modification required; copied directly from YMFM data
}

/*
 * YMFM_VRC7_VOICES[18][8] → EXPERIMENT_VRC7_PRESET[18][8]
 * Each voice is commented with its number and English instrument name.
 */
void convert_ymfm_vrc7_to_experiment(const uint8_t ymfm[18][8], uint8_t exp[18][8]) {
    for (int i = 0; i < 18; ++i)
        for (int j = 0; j < 8; ++j)
            exp[i][j] = ymfm[i][j];

    // 1: Bell
    exp[0][4] = ymfm[0][4] + 0x20; // Increase TL (modulator total level) to make the bell sound softer

    // 2: Guitar
    exp[1][4] = ymfm[1][4] - 0x20; // Decrease TL (modulator total level) to emphasize harmonic content
    exp[1][5] = ymfm[1][5] - 1;    // Slightly reduce feedback for a smoother tone

    // 3: Piano
    exp[2][0] = (ymfm[2][0] & 0xF0) | 0x11; // Adjust AR/DR for modulator to tighten attack
    exp[2][1] = (ymfm[2][1] & 0xF0) | 0x11; // Adjust AR/DR for carrier similarly
    exp[2][5] = ymfm[2][5] - 0x10;          // Lower sustain level for better dynamic contrast
    exp[2][6] = (ymfm[2][6] & 0xF0) | 0x20; // Modify waveform selection for brighter tone
    exp[2][7] = (ymfm[2][7] & 0xF0) | 0x12; // Adjust feedback and connection bits for balance

    // 4: Flute
    exp[3][4] = ymfm[3][4] - 0x50;          // Decrease TL to make tone gentler
    exp[3][6] = (ymfm[3][6] & 0xF0) | 0x61; // Modify waveform index for softer harmonic structure

    // 5: Clarinet
    exp[4][0] = (ymfm[4][0] & 0xF0) | 0x32; // Adjust AR/DR for better articulation
    exp[4][4] = ymfm[4][4] & 0xE1;          // Mask TL for consistent dynamic control
    exp[4][6] = ymfm[4][6] | 0x01;          // Enable subtle waveform variation

    // 6: Synth Brass
    exp[5][2] = ymfm[5][2] + 1;             // Slightly adjust DR to smoothen decay
    exp[5][4] = ymfm[5][4] - 0x09;          // Decrease TL for more punch
    exp[5][5] = ymfm[5][5] - 0x10;          // Reduce feedback for cleaner tone
    exp[5][6] = 0xF4;                       // Force waveform index to bright brass tone
    exp[5][7] = 0xF4;                       // Same for carrier

    // 7: Trumpet
    exp[6][5] = ymfm[6][5] & ~0x0E;         // Clear bits to control feedback and modulation depth
    exp[6][6] = (ymfm[6][6] & 0xF0) | 0x11; // Adjust waveform to slightly brighter brass timbre

    // 8: Organ
    exp[7][4] = (ymfm[7][4] == 0xFF) ? 0xA2 : ymfm[7][4]; // Correct invalid TL (FF → A2)
    exp[7][5] = (ymfm[7][5] & 0xF0) | 0x72; // Adjust sustain and release for smoother envelope
    exp[7][6] = (ymfm[7][6] & 0xF0) | 0x01; // Slight waveform tweak for realism

    // 9: Horn
    exp[8][0] = (ymfm[8][0] & 0xF0) | 0x35; // Adjust attack rate for stronger onset
    exp[8][1] = ymfm[8][1];                 // Keep DR as original
    exp[8][2] = ymfm[8][2];                 // Keep RR as original
    exp[8][4] = (ymfm[8][4] & 0xF0) | 0x40; // Reduce TL slightly for balance
    exp[8][5] = (ymfm[8][5] & 0xF0) | 0x73; // Adjust feedback to enhance body
    exp[8][6] = (ymfm[8][6] & 0xF0) | 0x72; // Add brighter harmonics
    exp[8][7] = (ymfm[8][7] & 0xF0) | 0x01; // Clean up waveform control bits

    // 10: Synth
    exp[9][0] = (ymfm[9][0] & 0xF0) | 0xB5; // Increase AR for sharper attack
    exp[9][2] = (ymfm[9][2] & 0xF0) | 0x0F; // Modify RR to sustain longer
    exp[9][3] = (ymfm[9][3] & 0xF0) | 0x0F; // Same adjustment for carrier
    exp[9][4] = (ymfm[9][4] & 0xF0) | 0xA8; // Slight TL correction
    exp[9][5] = (ymfm[9][5] & 0xF0) | 0xA5; // Balance modulator level
    exp[9][6] = (ymfm[9][6] & 0xF0) | 0x51; // Waveform tweak for clarity

    // 11: Harpsichord
    exp[10][2] = ymfm[10][2] & ~((1<<1)|(1<<2)|(1<<5)); // Clear specific bits to control decay and release

    // 12: Vibraphone
    exp[11][6] = (ymfm[11][6] & 0xF0) | 0x18; // Change waveform index for metallic tone color

    // 13: Acoustic Bass
    exp[12][4] = (ymfm[12][4] & ~0x3A) | 0xC9; // Modify TL and modulation for a rounder bass tone
    exp[12][5] = (ymfm[12][5] & 0xF0) | 0x95;  // Adjust sustain for natural decay
    exp[12][6] = ymfm[12][6] & 0x0F;           // Keep waveform clean
    exp[12][7] = ymfm[12][7] & 0x0F;           // Keep feedback low

    // 14: Electric Guitar
    exp[13][4] = ymfm[13][4] - 0x10;           // Decrease TL for more bite
    exp[13][5] = ymfm[13][5] & ~0x3F;          // Remove low bits to simplify modulation
    exp[13][6] = ymfm[13][6] | 0x03;           // Enable mild waveform variation
    exp[13][7] = ymfm[13][7] | 0xF0;           // Force waveform type to sustain harmonics

    // 15: Bass Synth
    exp[14][1] = (ymfm[14][1] & 0xF0) | 0x72;  // Adjust decay to a longer sustain
    exp[14][4] = ymfm[14][4] | 0x20;           // Slight TL boost
    exp[14][5] = ymfm[14][5] & 0xF5;           // Control feedback for smoother tone
    exp[14][6] = ymfm[14][6] | 0x06;           // Enable extra harmonic content
    exp[14][7] = ymfm[14][7] & 0xFE;           // Clean waveform bits

    // 16: Drum kit
    exp[15][2] = ymfm[15][2] | 0x18;           // Strengthen percussive envelope
    exp[15][3] = ymfm[15][3] | 0x0F;           // Adjust attack and decay for snappier hits
    exp[15][4] = ymfm[15][4] | 0x1F;           // TL correction for balanced loudness
    exp[15][5] = ymfm[15][5] | 0x18;           // Feedback adjustment
    exp[15][6] = ymfm[15][6] & 0x7A;           // Clean waveform mask
    exp[15][7] = ymfm[15][7] | 0x05;           // Add waveform variation

    // 17: Rhythm2
    exp[16][7] = (ymfm[16][7] & 0xF0) | 0x68;  // Adjust waveform index for proper noise balance
}


/** 
 * YMFM_YMF281B_VOICES[18][8] → EXPERIMENT_YMF281B_PRESET[18][8]
 * Convert YMFM YMF281B instrument data to experimental preset data
 *  Each instrument is adjusted individually by modifying operator or channel parameters.
 */
void convert_ymf281b_to_experiment(const uint8_t ymfm[18][8], uint8_t exp[18][8]) {
    // Copy original YMFM data as base
    for (int i = 0; i < 18; ++i)
        for (int j = 0; j < 8; ++j)
            exp[i][j] = ymfm[i][j];

    // 1: Bell
    exp[0][0] = (ymfm[0][0] & 0xF0) | 0x03;  // Modulator: adjust multiple to emphasize high overtones
    exp[0][4] = (ymfm[0][4] & 0x0F) | 0xE8;  // Carrier: higher TL (total level) for metallic tone
    exp[0][5] = (ymfm[0][5] & 0x0F) | 0x81;  // Modulator: strong attack, quick decay
    exp[0][6] = (ymfm[0][6] & 0xF0) | 0x42;  // Feedback & waveform adjustment

    // 2: Guitar
    exp[1][0] = (ymfm[1][0] & 0xF0) | 0x13;  // Slightly increase multiple for string-like harmonics
    exp[1][4] = (ymfm[1][4] & 0x0F) | 0xD8;  // Carrier TL tuned for brighter attack
    exp[1][5] = (ymfm[1][5] & 0x0F) | 0xF6;  // Fast attack & moderate sustain
    exp[1][6] = (ymfm[1][6] & 0xF0) | 0x23;  // Feedback shaping
    exp[1][7] = (ymfm[1][7] & 0xF0) | 0x12;  // Carrier waveform tweak

    // 3: Piano
    exp[2][0] = (ymfm[2][0] & 0xF0) | 0x11;  // Modulator: balanced multiple
    exp[2][1] = (ymfm[2][1] & 0xF0) | 0x11;  // Carrier: same harmonic range
    exp[2][4] = (ymfm[2][4] & 0xF0) | 0xFA;  // Strong attack for percussive tone
    exp[2][5] = (ymfm[2][5] & 0xF0) | 0xB2;  // Faster decay for crispness
    exp[2][6] = (ymfm[2][6] & 0xF0) | 0x20;  // Mild feedback
    exp[2][7] = (ymfm[2][7] & 0xF0) | 0x12;  // Waveform for clean overtone

    // 4: Flute
    exp[3][4] = (ymfm[3][4] & 0xF0) | 0xA8;  // Smooth attack, airy tone
    exp[3][6] = (ymfm[3][6] & 0xF0) | 0x61;  // Low feedback, gentle waveform

    // 5: Clarinet
    exp[4][0] = (ymfm[4][0] & 0xF0) | 0x32;  // Adjust multiple for reed-like tone
    exp[4][4] = (ymfm[4][4] & 0xE0) | 0xE1;  // Higher TL and slower decay
    exp[4][6] = (ymfm[4][6] & 0xF0) | 0x01;  // Slight feedback for subtle harmonics

    // 6: Synth Brass
    exp[5][0] = (ymfm[5][0] & 0xF0) | 0x02;  // Low multiple for brass body
    exp[5][1] = (ymfm[5][1] & 0xF0) | 0x01;  // Carrier in similar range
    exp[5][2] = (ymfm[5][2] & 0xF0) | 0x06;  // Envelope shaping
    exp[5][4] = (ymfm[5][4] & 0xF0) | 0xA3;  // Sharp attack
    exp[5][5] = (ymfm[5][5] & 0xF0) | 0xE2;  // Controlled sustain
    exp[5][6] = (ymfm[5][6] & 0xF0) | 0xF4;  // Feedback to enrich brightness
    exp[5][7] = (ymfm[5][7] & 0xF0) | 0xF4;  // Same for carrier

    // 7: Trumpet
    exp[6][2] = (ymfm[6][2] & 0xF0) | 0x1D;  // Envelope shape emphasizing attack
    exp[6][4] = (ymfm[6][4] & 0xF0) | 0x82;  // Higher TL for brassy edge
    exp[6][5] = (ymfm[6][5] & 0xF0) | 0x81;  // Slightly faster attack
    exp[6][6] = (ymfm[6][6] & 0xF0) | 0x11;  // Feedback for resonance

    // 8: Organ
    exp[7][0] = (ymfm[7][0] & 0xF0) | 0x23;  // Balanced multiple for organ-like harmonics
    exp[7][2] = (ymfm[7][2] & 0xF0) | 0x22;
    exp[7][4] = (ymfm[7][4] & 0xF0) | 0xA2;  // Long sustain
    exp[7][5] = (ymfm[7][5] & 0xF0) | 0x72;  // Softer decay
    exp[7][6] = (ymfm[7][6] & 0xF0) | 0x01;  // Minimal feedback

    // 9: Horn
    exp[8][0] = (ymfm[8][0] & 0xF0) | 0x35;  // Modulator multiple=5 for bright brass harmonics
    exp[8][1] = (ymfm[8][1] & 0xF0) | 0x11;  // Modulator AR/DR gentle attack for breathy start
    exp[8][2] = (ymfm[8][2] & 0xF0) | 0x25;  // Carrier AR/DR for sharper attack and quick decay
    exp[8][4] = (ymfm[8][4] & 0xF0) | 0x40;  // Feedback moderate (adds brass edge without noise)
    exp[8][5] = (ymfm[8][5] & 0xF0) | 0x73;  // Modulator total level — high output for body
    exp[8][6] = (ymfm[8][6] & 0xF0) | 0x72;  // Carrier TL slightly lower, keeps brightness
    exp[8][7] = (ymfm[8][7] & 0xF0) | 0x01;  // Modulation type: simple 2-op connection

    // 10: Synth
    exp[9][0] = (ymfm[9][0] & 0xF0) | 0xB5;  // High multiple for synthetic tone
    exp[9][2] = (ymfm[9][2] & 0xF0) | 0x0F;  // Envelope mod
    exp[9][3] = (ymfm[9][3] & 0xF0) | 0x0F;
    exp[9][4] = (ymfm[9][4] & 0x0F) | 0xA0;  // Adjust total level
    exp[9][5] = (ymfm[9][5] & 0x0F) | 0xA0;
    exp[9][6] = (ymfm[9][6] & 0xF0) | 0x51;  // Feedback shaping

    // 11: Harpsichord
    exp[10][2] = ymfm[10][2] & ~((1<<1)|(1<<2)|(1<<5)); // Clear bits to reduce decay → 0x5E→0x24
    exp[10][4] = ymfm[10][4] | 0x08; // Keep strong attack
    exp[10][5] = ymfm[10][5] | 0x08; // Same adjustment for carrier

    // 12: Vibraphone
    exp[11][6] = ymfm[11][6] | 0x08; // Slight feedback to add shimmer (0x10→0x18)

    // 13: Acoustic Bass
    exp[12][4] = (ymfm[12][4] & ~0x3A) | 0xC9; // Adjust attack/decay
    exp[12][5] = (ymfm[12][5] | 0x03) & 0x95;  // Modify sustain and rate
    exp[12][6] = ymfm[12][6] & 0x0F;           // Reduce feedback
    exp[12][7] = ymfm[12][7] & 0x0F;           // Lower waveform emphasis

    // 14: Electric Guitar
    exp[13][4] = ymfm[13][4] - 0x10; // Reduce total level (brighter tone)
    exp[13][5] = ymfm[13][5] & ~0x3F; // Shorter decay
    exp[13][6] = ymfm[13][6] | 0x03;  // Slight feedback increase
    exp[13][7] = ymfm[13][7] | 0xF0;  // Waveform emphasis

    // 15: Bass Synth
    exp[14][1] = (ymfm[14][1] & 0xF0) | 0x72; // Envelope tuning for deep tone
    exp[14][4] = ymfm[14][4] | 0x20;          // Boost sustain
    exp[14][5] = ymfm[14][5] & 0xF5;          // Control release
    exp[14][6] = ymfm[14][6] | 0x06;          // Feedback mod
    exp[14][7] = ymfm[14][7] & 0xFE;          // Simplify waveform

    // 16: Drum kit
    exp[15][2] = ymfm[15][2] | 0x18; // Emphasize attack
    exp[15][3] = ymfm[15][3] | 0x0F; // Stronger envelope
    exp[15][4] = ymfm[15][4] | 0x1F; // Bright transient
    exp[15][5] = ymfm[15][5] | 0x18; // Enhanced punch
    exp[15][6] = ymfm[15][6] & 0x7A; // Control feedback
    exp[15][7] = ymfm[15][7] | 0x05; // Adjust waveform

    // 17: Rhythm2 (Snare / Hi-Hat)
    exp[16][7] = (ymfm[16][7] & 0xF0) | 0x68; // Envelope retune

    // 18: Rhythm3 (Tom / Cymbal)
    // exp[17][k] = ymfm[17][k]; // No modification
}

/*
 * YMFM_YM2423_VOICES[18][8] → EXPERIMENT_YM2423_PRESET[18][8]
 * Each voice is commented with its number and English instrument name.
 */
void convert_ymfm_2423_to_experiment(const uint8_t ymfm[18][8], uint8_t exp[18][8]) {
    // 1. Violin (ch=0)
    // Smooth attack, natural sustain, and moderate modulation depth.
    exp[0][0] = (ymfm[0][0] & 0xF0) | 0x11; // Lower AR for softer onset
    exp[0][1] = (ymfm[0][1] & 0xF0) | 0x61; // DR tuned for smooth fade
    exp[0][2] = (ymfm[0][2] & 0xF0) | 0x1E; // SR for natural decay
    exp[0][3] = (ymfm[0][3] & 0xF0) | 0x17; // RR moderate
    exp[0][4] = (ymfm[0][4] & 0x0F) | 0xD0; // TL controls tone brightness
    exp[0][5] = (ymfm[0][5] & 0xF0) | 0x78; // AM/PM/EG bias
    exp[0][6] = (ymfm[0][6] & 0xF0) | 0x00; // No modulation depth change
    exp[0][7] = (ymfm[0][7] & 0xF0) | 0x17; // Carrier envelope fine-tune

    // 2. Guitar (ch=1)
    // Sharper transient, shorter sustain; adds plucked-string edge.
    exp[1][0] = (ymfm[1][0] & 0xF0) | 0x13; // Fast AR for percussive attack
    exp[1][1] = (ymfm[1][1] & 0xF0) | 0x41; // Mild decay
    exp[1][2] = (ymfm[1][2] & 0xF0) | 0x1A; // Slightly reduced sustain
    exp[1][3] = (ymfm[1][3] & 0xF0) | 0x0D; // Quicker release
    exp[1][4] = (ymfm[1][4] & 0x0F) | 0xD0 | 0x08; // D8 = brighter tone via TL
    exp[1][5] = (ymfm[1][5] & 0xF0) | 0xF7; // FM modulation depth up
    exp[1][6] = (ymfm[1][6] & 0xF0) | 0x23; // Slight feedback
    exp[1][7] = (ymfm[1][7] & 0xF0) | 0x13; // Mild sustain curve

    // 3. Piano (ch=2)
    // Strong attack and fast decay, emphasizing transient and resonance.
    exp[2][0] = (ymfm[2][0] & 0xF0) | 0x13; // Aggressive AR
    exp[2][1] = (ymfm[2][1] & 0xF0) | 0x01; // Rapid decay
    exp[2][2] = (ymfm[2][2] & 0xF0) | 0x99; // Longer sustain tail
    exp[2][3] = (ymfm[2][3] & 0xF0) | 0x00; // Tight RR
    exp[2][4] = (ymfm[2][4] & 0xF0) | 0xF2; // Bright tone
    exp[2][5] = (ymfm[2][5] & 0xF0) | 0xD4; // Controlled harmonic content
    exp[2][6] = (ymfm[2][6] & 0xF0) | 0x21; // Feedback slight
    exp[2][7] = (ymfm[2][7] & 0xF0) | 0x23; // Envelope fine-tune

    // 4. Flute (ch=3)
    // Soft and breathy, with moderate sustain and slow release.
    exp[3][0] = (ymfm[3][0] & 0xF0) | 0x11; // Slow AR for gentle onset
    exp[3][1] = (ymfm[3][1] & 0xF0) | 0x61; // Smooth DR
    exp[3][2] = (ymfm[3][2] & 0xF0) | 0x0E; // Sustain curve
    exp[3][3] = (ymfm[3][3] & 0xF0) | 0x07; // Long release
    exp[3][4] = (ymfm[3][4] & 0x0F) | 0x8D; // Soft TL = airy tone
    exp[3][5] = (ymfm[3][5] & 0xF0) | 0x64; // Moderate AM depth
    exp[3][6] = (ymfm[3][6] & 0xF0) | 0x70; // Light vibrato
    exp[3][7] = (ymfm[3][7] & 0xF0) | 0x27; // Smooth RR

    // 5. Clarinet (ch=4)
    // Gentle reed tone, focused midrange resonance.
    exp[4][0] = (ymfm[4][0] & 0xF0) | 0x32; // Mid-speed AR
    exp[4][1] = (ymfm[4][1] & 0xF0) | 0x21; // Controlled DR
    exp[4][2] = (ymfm[4][2] & 0xF0) | 0x1E; // Slight sustain
    exp[4][3] = (ymfm[4][3] & 0xF0) | 0x06; // Moderate release
    exp[4][4] = (ymfm[4][4] & 0x0F) | 0xE0 | 0x01; // E1 = warm TL curve
    exp[4][5] = (ymfm[4][5] & 0xF0) | 0x76; // AM mod slightly stronger
    exp[4][6] = (ymfm[4][6] & 0xF0) | 0x01; // Low feedback
    exp[4][7] = (ymfm[4][7] & 0xF0) | 0x28; // Slight envelope bias

    // 6. Oboe (ch=5)
    // Reed-like, narrow resonance, slower release for realism.
    exp[5][0] = (ymfm[5][0] & 0xF0) | 0x31; // Modulator: balanced attack, gentle bite
    exp[5][1] = (ymfm[5][1] & 0xF0) | 0x22; // Carrier: slower decay for breathy sustain
    exp[5][2] = (ymfm[5][2] & 0xF0) | 0x16; // Modulator TL=0x16 (adds slight nasal color)
    exp[5][3] = (ymfm[5][3] & 0xF0) | 0x05; // Carrier TL=0x05 (near full level)
    exp[5][4] = (ymfm[5][4] & 0x0F) | 0xE0; // Feedback: strong, narrow resonance typical of reeds
    exp[5][5] = (ymfm[5][5] & 0xF0) | 0x71; // Moderate sustain and slower release for realism
    exp[5][6] = (ymfm[5][6] & 0xF0) | 0x00; // Waveform: pure sine, natural tone
    exp[5][7] = (ymfm[5][7] & 0xF0) | 0x18; // Subtle LFO depth adds expressive vibrato

    // 7. Trumpet (ch=6)
    // Bright, brassy with quick attack and shallow sustain.
    exp[6][0] = (ymfm[6][0] & 0xF0) | 0x21; // Modulator: fast attack for brass bite
    exp[6][1] = (ymfm[6][1] & 0xF0) | 0x61; // Carrier: strong attack and shorter decay
    exp[6][2] = (ymfm[6][2] & 0xF0) | 0x1D; // Modulator TL=0x1D (high harmonic brightness)
    exp[6][3] = (ymfm[6][3] & 0xF0) | 0x07; // Carrier TL=0x07 (loud presence)
    exp[6][4] = (ymfm[6][4] & 0x0F) | 0x82; // Feedback high, bright tone core
    exp[6][5] = (ymfm[6][5] & 0xF0) | 0x81; // Fast decay, shallow sustain
    exp[6][6] = (ymfm[6][6] & 0xF0) | 0x11; // Waveform: adds harmonic shaping
    exp[6][7] = (ymfm[6][7] & 0xF0) | 0x07; // Slight pitch modulation for natural brass instability

    // 8. Organ (ch=7)
    // Steady amplitude, mild harmonic enhancement.
    exp[7][0] = (ymfm[7][0] & 0xF0) | 0x33; // Modulator: moderate attack, smooth decay
    exp[7][1] = (ymfm[7][1] & 0xF0) | 0x21; // Carrier: long sustain, constant amplitude
    exp[7][2] = (ymfm[7][2] & 0xF0) | 0x2D; // Modulator TL=0x2D (adds light harmonic content)
    exp[7][3] = (ymfm[7][3] & 0xF0) | 0x13; // Carrier TL=0x13 (steady loudness)
    exp[7][4] = (ymfm[7][4] & 0x0F) | 0xB0; // Feedback medium, organ-like stability
    exp[7][5] = (ymfm[7][5] & 0xF0) | 0x70; // Long sustain and gentle release
    exp[7][6] = (ymfm[7][6] & 0xF0) | 0x00; // Waveform: sine for clean tone
    exp[7][7] = (ymfm[7][7] & 0xF0) | 0x07; // Mild vibrato for rotary effect

    // 9. Horn (ch=8)
    // Round tone, mellow mid frequencies.
    exp[8][0] = (ymfm[8][0] & 0xF0) | 0x61; // Modulator: strong attack for brass edge
    exp[8][1] = (ymfm[8][1] & 0xF0) | 0x61; // Carrier: same contour for unified tone
    exp[8][2] = (ymfm[8][2] & 0xF0) | 0x1B; // Modulator TL=0x1B (warmer harmonic content)
    exp[8][3] = (ymfm[8][3] & 0xF0) | 0x06; // Carrier TL=0x06 (solid output)
    exp[8][4] = (ymfm[8][4] & 0x0F) | 0x64; // Feedback moderate, darker tone
    exp[8][5] = (ymfm[8][5] & 0xF0) | 0x65; // Envelope sustain moderate for body
    exp[8][6] = (ymfm[8][6] & 0xF0) | 0x10; // Waveform with light shaping, not pure sine
    exp[8][7] = (ymfm[8][7] & 0xF0) | 0x17; // Mild vibrato for expressive brass quality

    // 10. Synthesizer (ch=9)
    // Modern synthetic lead; stronger modulation and sustain.
    exp[9][0] = (ymfm[9][0] & 0xF0) | 0x41; // Modulator: moderate attack, controlled brightness
    exp[9][1] = (ymfm[9][1] & 0xF0) | 0x61; // Carrier: fast attack and full sustain
    exp[9][2] = (ymfm[9][2] & 0xF0) | 0x0B; // Modulator TL=0x0B (bright and cutting)
    exp[9][3] = (ymfm[9][3] & 0xF0) | 0x18; // Carrier TL=0x18 (balanced loudness)
    exp[9][4] = (ymfm[9][4] & 0x0F) | 0x85; // Feedback: high for crisp lead tone
    exp[9][5] = (ymfm[9][5] & 0xF0) | 0xF0; // Long sustain for legato synth style
    exp[9][6] = (ymfm[9][6] & 0xF0) | 0x81; // Alternate waveform for modern edge
    exp[9][7] = (ymfm[9][7] & 0xF0) | 0x07; // Subtle LFO for pitch expressiveness

    // 11. Harpsichord (ch=10)
    // Metallic transient and fast release for plucked key sound.
    exp[10][0] = (ymfm[10][0] & 0xF0) | 0x33; // Modulator: fast attack, short sustain
    exp[10][1] = (ymfm[10][1] & 0xF0) | 0x01; // Carrier: very quick decay
    exp[10][2] = (ymfm[10][2] & 0xF0) | 0x83; // Modulator TL=0x83 (adds metallic overtone)
    exp[10][3] = (ymfm[10][3] & 0xF0) | 0x11; // Carrier TL=0x11 (bright tone level)
    exp[10][4] = (ymfm[10][4] & 0x0F) | 0xEA; // Feedback high, strong brightness emphasis
    exp[10][5] = (ymfm[10][5] & 0xF0) | 0xEF; // Very fast decay, percussive feel
    exp[10][6] = (ymfm[10][6] & 0xF0) | 0x10; // Waveform slightly shaped for metallic transient
    exp[10][7] = (ymfm[10][7] & 0xF0) | 0x04; // Fine tuning for sharper pluck definition


    // 12. Vibraphone (ch=11)
    // Warm metallic tone with moderate vibrato depth.
    exp[11][0] = (ymfm[11][0] & 0xF0) | 0x17; // Modulator: moderate attack, slight tremolo
    exp[11][1] = (ymfm[11][1] & 0xF0) | 0xC1; // Carrier: long decay, smooth release
    exp[11][2] = (ymfm[11][2] & 0xF0) | 0x24; // Modulator TL=0x24 (brightness control)
    exp[11][3] = (ymfm[11][3] & 0xF0) | 0x07; // Carrier TL=0x07 (almost full level)
    exp[11][4] = (ymfm[11][4] & 0x0F) | 0xF8; // Connection/feedback: strong feedback adds metallic shimmer
    exp[11][5] = (ymfm[11][5] & 0xF0) | 0xF8; // Modulator/Carrier: max sustain, short release
    exp[11][6] = (ymfm[11][6] & 0xF0) | 0x22; // Waveform: sine with slight shaping for metallic tone
    exp[11][7] = (ymfm[11][7] & 0xF0) | 0x12; // Fine-tune / amplitude LFO depth moderate

    // 13. Synth Bass (ch=12)
    // Deep bass with strong envelope, fast attack.
    exp[12][0] = (ymfm[12][0] & 0xF0) | 0x61; // Modulator: fast attack, low sustain
    exp[12][1] = (ymfm[12][1] & 0xF0) | 0x50; // Carrier: quick decay for percussive edge
    exp[12][2] = (ymfm[12][2] & 0xF0) | 0x0C; // Modulator TL=0x0C (bright tone)
    exp[12][3] = (ymfm[12][3] & 0xF0) | 0x05; // Carrier TL=0x05 (full volume)
    exp[12][4] = (ymfm[12][4] & 0x0F) | 0xD2; // Feedback moderate, connection mode 0
    exp[12][5] = (ymfm[12][5] & 0xF0) | 0xF5; // Envelope: short sustain, tight bass control
    exp[12][6] = (ymfm[12][6] & 0xF0) | 0x40; // Waveform: saw-like for synth bass texture
    exp[12][7] = (ymfm[12][7] & 0xF0) | 0x42; // Pitch LFO moderate for “growl” movement

    // 14. Acoustic Bass (ch=13)
    // Natural upright tone; slower attack, more resonance.
    exp[13][0] = (ymfm[13][0] & 0xF0) | 0x01; // Modulator: slow attack
    exp[13][1] = (ymfm[13][1] & 0xF0) | 0x01; // Carrier: same slow response
    exp[13][2] = (ymfm[13][2] & 0xF0) | 0x55; // Modulator TL=0x55 (soft, woody tone)
    exp[13][3] = (ymfm[13][3] & 0xF0) | 0x03; // Carrier TL=0x03 (moderate volume)
    exp[13][4] = (ymfm[13][4] & 0x0F) | 0xE4; // Feedback moderate, natural resonance
    exp[13][5] = (ymfm[13][5] & 0xF0) | 0x90; // Long sustain, slower decay for acoustic body
    exp[13][6] = (ymfm[13][6] & 0xF0) | 0x03; // Waveform: near-sine for round bass tone

    // 15. Electric Guitar (ch=14)
    // Bright, plucked tone with mild distortion edge.
    exp[14][0] = (ymfm[14][0] & 0xF0) | 0x41; // Modulator: medium attack, edgy response
    exp[14][1] = (ymfm[14][1] & 0xF0) | 0x41; // Carrier: same contour, synchronized envelope
    exp[14][2] = (ymfm[14][2] & 0xF0) | 0x89; // Modulator TL=0x89 (adds harmonic drive)
    exp[14][3] = (ymfm[14][3] & 0xF0) | 0x03; // Carrier TL=0x03 (strong output)
    exp[14][4] = (ymfm[14][4] & 0x0F) | 0xF1; // Feedback high, adds slight distortion
    exp[14][5] = (ymfm[14][5] & 0xF0) | 0xE4; // Moderate sustain, short release for pluck
    exp[14][6] = (ymfm[14][6] & 0xF0) | 0xC0; // Waveform: saw-like for bright timbre
    exp[14][7] = (ymfm[14][7] & 0xF0) | 0x13; // Fine detune for realism in chorus texture

    // 16. Rhythm1 (BD) ch=15
    for(int j=0; j<8; ++j) exp[15][j] = ymfm[15][j];
    
    // 17. Rhythm2 (SD/HH) ch=16
    for(int j=0; j<8; ++j) exp[16][j] = ymfm[16][j];
    exp[16][7] = (ymfm[16][7] & 0xF0) | 0x68;
    
    // 18. Rhythm3 (TT/CYM) ch=17
    for(int j=0; j<8; ++j) exp[17][j] = ymfm[17][j];
}

// vgm-conv style YM2413→OPL(3) conversion, with canonical lookup tables
// AR/DR/RR (Attack/Decay/Release Rate) conversion tables
static const uint8_t opll2opl_ar[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };
static const uint8_t opll2opl_dr[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };
static const uint8_t opll2opl_rr[16] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15 };

// Key scaling level
static const uint8_t opll2opl_ksl[4] = { 0, 2, 1, 3 };

// Multiplier
static const uint8_t opll2opl_mult[16] = { 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

// Total Level (TL) 4bit YM2413 to 6bit OPL (matches vgm-conv: (TL_nibble << 1))
static inline uint8_t opll2opl_tl(uint8_t tl) {
    return (tl & 0x0F) << 1;
}

/** Write to OPL3 register mirror and VGM output */
static inline void write_opl3_reg(VGMContext *p_vgmctx, uint8_t addr, uint8_t data) {
    forward_write(p_vgmctx, 0, addr, data);
    p_vgmctx->opll_state.reg[addr] = data;
}

static inline int toTL(int vol, int off) {
    int t = (vol << 2) - off;
    return (t > 0) ? t : 0;
}


static const unsigned char (*select_opll_preset_table(
    OPLL_PresetType preset, OPLL_PresetSource preset_source
))[8]
{
    switch (preset) {
        case OPLL_PresetType_YM2413:
            switch (preset_source) {
                case OPLL_PresetSource_YMVOICE:
                    return YMVOICE_YM2413_VOICES;
                case OPLL_PresetSource_YMFM:
                    return YMFM_YM2413_VOICES;
                case OPLL_PresetSource_EXPERIMENT:
                    return EXPERIMENT_YM2413_PRESET;
                default:
                    return YMFM_YM2413_VOICES;
            }
        case OPLL_PresetType_VRC7:
            switch (preset_source) {
                case OPLL_PresetSource_YMVOICE:
                    return YMVOICE_VRC7_VOICES;
                case OPLL_PresetSource_YMFM:
                    return YMFM_VRC7_VOICES;
                case OPLL_PresetSource_EXPERIMENT:
                    return EXPERIMENT_VRC7_PRESET;
                default:
                    return YMFM_VRC7_VOICES;
            }
        case OPLL_PresetType_YMF281B:
            switch (preset_source) {
                case OPLL_PresetSource_YMVOICE:
                    return YMVOICE_YMF281B_VOICES;
                case OPLL_PresetSource_YMFM:
                    return YMFM_YMF281B_VOICES;
                case OPLL_PresetSource_EXPERIMENT:
                    return EXPERIMENT_YMF281B_PRESET;
                default:
                    return YMFM_YMF281B_VOICES;
            }
        case OPLL_PresetType_YM2423:
            switch (preset_source) {
                case OPLL_PresetSource_YMFM:
                    return YMFM_YM2423_VOICES;
                case OPLL_PresetSource_EXPERIMENT:
                    return EXPERIMENT_YM2423_PRESET;
                default:
                    return YMFM_YM2423_VOICES;
            }
        // 他のpresetも同様に追加
        default:
            return YMVOICE_YM2413_VOICES;
    }
}


void opll_load_voice(VGMContext *p_vgmctx, int inst, int ch, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp) return;
    memset(p_vp, 0, sizeof(*p_vp));

    uint8_t user_patch[8];
    // Select the preset table
    const unsigned char (*source_preset)[8] = select_opll_preset_table(p_opts->preset, p_opts->preset_source);
    if (p_opts->preset_source == OPLL_PresetSource_EXPERIMENT) {
        switch (p_opts->preset) {
            case OPLL_PresetType_YM2413:
                convert_ymfm_2413_to_experiment(YMFM_YM2413_VOICES, EXPERIMENT_YM2413_PRESET);
                source_preset = EXPERIMENT_YM2413_PRESET;
                break;
            case OPLL_PresetType_VRC7:
                convert_ymfm_vrc7_to_experiment(YMFM_VRC7_VOICES, EXPERIMENT_VRC7_PRESET);
                source_preset = EXPERIMENT_VRC7_PRESET;
                break;
            case OPLL_PresetType_YMF281B:
                convert_ymf281b_to_experiment(YMFM_YMF281B_VOICES, EXPERIMENT_YMF281B_PRESET);
                source_preset = EXPERIMENT_YMF281B_PRESET;
                break;
            case OPLL_PresetType_YM2423:
                convert_ymfm_2423_to_experiment(YMFM_YM2423_VOICES, EXPERIMENT_YM2423_PRESET);
                source_preset = EXPERIMENT_YM2423_PRESET;
                break;
            default:
                break;
        }
    }
    // Select patch
    const unsigned char *src = NULL;
    if (inst == 0) {
        // User patch (from registers)
        for (int i = 0; i < 8; ++i)
            user_patch[i] = p_vgmctx->opll_state.reg[i];
        src = user_patch;
    } else if (inst >= 1 && inst <= 19) {
        src = source_preset[inst - 1]; // [1]..[19] are preset patches
    } else if (source_preset >= 20) {
        src = source_preset[19]; // Fallback: last preset
    } else {
        src = source_preset[0];  // Fallback: first preset
    }

    // Defensive: if src is NULL, abort
    if (!src) {
        fprintf(stderr, "[ERROR] src is NULL in opll_load_voice (inst=%d)\n", inst);
        return;
    }

    // --- Modulator ---
    uint8_t m_am   = (src[0] >> 7) & 1;
    uint8_t m_vib  = (src[0] >> 6) & 1;
    uint8_t m_egt  = (src[0] >> 5) & 1;
    uint8_t m_ksr  = (src[0] >> 4) & 1;
    uint8_t m_mult = src[0] & 0x0F;
    uint8_t m_kl   = (src[2] >> 6) & 0x03;
    uint8_t m_tl   = src[2] & 0x3F;
    uint8_t m_ar   = (src[4] >> 4) & 0x0F;
    uint8_t m_dr   = src[4] & 0x0F;
    uint8_t m_sl   = (src[6] >> 4) & 0x0F;
    uint8_t m_rr   = src[6] & 0x0F;
    uint8_t m_ws   = (src[3] >> 3) & 0x01;

    // --- Carrier ---
    uint8_t c_am   = (src[1] >> 7) & 1;
    uint8_t c_vib  = (src[1] >> 6) & 1;
    uint8_t c_egt  = (src[1] >> 5) & 1;
    uint8_t c_ksr  = (src[1] >> 4) & 1;
    uint8_t c_mult = src[1] & 0x0F;
    uint8_t c_kl   = (src[3] >> 6) & 0x03;
    uint8_t c_tl   = src[3] & 0x3F;
    uint8_t c_ar   = (src[5] >> 4) & 0x0F;
    uint8_t c_dr   = src[5] & 0x0F;
    uint8_t c_sl   = (src[7] >> 4) & 0x0F;
    uint8_t c_rr   = src[7] & 0x0F;
    uint8_t c_ws   = (src[3] >> 4) & 0x01;

    uint8_t fb = src[3] & 0x07;

    if (p_opts->preset_source != OPLL_PresetSource_YMVOICE) {
        // Modulator
        p_vp->op[0].am   = m_am;
        p_vp->op[0].vib  = m_vib;
        p_vp->op[0].egt  = m_egt;
        p_vp->op[0].ksr  = m_ksr;
        p_vp->op[0].mult = opll2opl_mult[m_mult];
        p_vp->op[0].ksl  = opll2opl_ksl[m_kl];
        p_vp->op[0].tl   = opll2opl_tl(m_tl);
        p_vp->op[0].ar   = opll2opl_ar[m_ar];
        p_vp->op[0].dr   = opll2opl_dr[m_dr];
        p_vp->op[0].sl   = m_sl;
        p_vp->op[0].rr   = opll2opl_rr[m_rr];
        p_vp->op[0].ws   = m_ws;

        // Carrier
        p_vp->op[1].am   = c_am;
        p_vp->op[1].vib  = c_vib;
        p_vp->op[1].egt  = c_egt;
        p_vp->op[1].ksr  = c_ksr;
        p_vp->op[1].mult = opll2opl_mult[c_mult];
        p_vp->op[1].ksl  = opll2opl_ksl[c_kl];
        p_vp->op[1].tl   = opll2opl_tl(c_tl);
        p_vp->op[1].ar   = opll2opl_ar[c_ar];
        p_vp->op[1].dr   = opll2opl_dr[c_dr];
        p_vp->op[1].sl   = c_sl;
        p_vp->op[1].rr   = opll2opl_rr[c_rr];
        p_vp->op[1].ws   = c_ws;

        p_vp->fb[0] = fb;
        p_vp->cnt[0] = 0;
        p_vp->is_4op = 0;
        p_vp->voice_no = inst;
        p_vp->source_fmchip = 0x01; // YM2413
    } else {
        // Modulator
        p_vp->op[0].am   = m_am;
        p_vp->op[0].vib  = m_vib;
        p_vp->op[0].egt  = m_egt;
        p_vp->op[0].ksr  = m_ksr;
        p_vp->op[0].mult = m_mult;
        p_vp->op[0].ksl  = m_kl;
        p_vp->op[0].tl   = m_tl;
        p_vp->op[0].ar   = m_ar;
        p_vp->op[0].dr   = m_dr;
        p_vp->op[0].sl   = m_sl;
        p_vp->op[0].rr   = m_rr;
        p_vp->op[0].ws   = m_ws;

        // Carrier
        p_vp->op[1].am   = c_am;
        p_vp->op[1].vib  = c_vib;
        p_vp->op[1].egt  = c_egt;
        p_vp->op[1].ksr  = c_ksr;
        p_vp->op[1].mult = c_mult;
        p_vp->op[1].ksl  = c_kl;
        p_vp->op[1].tl   = c_tl;
        p_vp->op[1].ar   = c_ar;
        p_vp->op[1].dr   = c_dr;
        p_vp->op[1].sl   = c_sl;
        p_vp->op[1].rr   = c_rr;
        p_vp->op[1].ws   = c_ws;

        p_vp->fb[0] = fb;
        p_vp->cnt[0] = 0;
        p_vp->is_4op = 0;
        p_vp->voice_no = inst;
        p_vp->source_fmchip = 0x01; // YM2413
    }

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr, "[YM2413->OPL3] inst=%d RAW: %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            inst, src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
        fprintf(stderr, "[YM2413->OPL3] MOD: AM=%u VIB=%u EGT=%u KSR=%u MULT=%u KSL=%u TL=%u AR=%u DR=%u SL=%u RR=%u WS=%u\n",
            p_vp->op[0].am, p_vp->op[0].vib, p_vp->op[0].egt, p_vp->op[0].ksr, p_vp->op[0].mult,
            p_vp->op[0].ksl, p_vp->op[0].tl, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[0].sl, p_vp->op[0].rr, p_vp->op[0].ws
        );
        fprintf(stderr, "[YM2413->OPL3] CAR: AM=%u VIB=%u EGT=%u KSR=%u MULT=%u KSL=%u TL=%u AR=%u DR=%u SL=%u RR=%u WS=%u\n",
            p_vp->op[1].am, p_vp->op[1].vib, p_vp->op[1].egt, p_vp->op[1].ksr, p_vp->op[1].mult,
            p_vp->op[1].ksl, p_vp->op[1].tl, p_vp->op[1].ar, p_vp->op[1].dr, p_vp->op[1].sl, p_vp->op[1].rr, p_vp->op[1].ws
        );
        fprintf(stderr, "[YM2413->OPL3] FB=%u\n", p_vp->fb[0]);
    }
}

/**
 * Apply OPL3VoiceParam to a channel.
 */
int opll2opl3_apply_voice(VGMContext *p_vgmctx, int ch, int mod_volume, int car_volume, bool key, OPL3VoiceParam *p_vp, const CommandOptions *p_opts)
{
    if (!p_vp || ch < 0 || ch >= 9) return 0;
    int wrote_bytes = 0;
    int slot_mod = opl3_opreg_addr(0, ch, 0);
    int slot_car = opl3_opreg_addr(0, ch, 1);
    uint8_t tl = p_vgmctx->opll_state.reg[0x30 + ch] & 0xF;

    // AM/VIB/EGT/KSR/MULT
    uint8_t opl3_2n_mod = (uint8_t)((p_vp->op[0].am << 7) | (p_vp->op[0].vib << 6) | (p_vp->op[0].egt << 5) | (p_vp->op[0].ksr << 4) | (p_vp->op[0].mult & 0x0F));
    uint8_t opl3_2n_car = (uint8_t)((p_vp->op[1].am << 7) | (p_vp->op[1].vib << 6) | (p_vp->op[1].egt << 5) | (p_vp->op[1].ksr << 4) | (p_vp->op[1].mult & 0x0F));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + slot_mod, opl3_2n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x20 + slot_car, opl3_2n_car, p_opts);

    // KSL/TL 
    uint8_t opl3_4n_mod = (uint8_t)((opll2opl_ksl[(p_vp->op[0].ksl & 0x03)] << 6) | ((mod_volume >= 0) ? mod_volume : (p_vp->op[0].tl & 0x3F)));
    uint8_t opl3_4n_car = (uint8_t)((opll2opl_ksl[(p_vp->op[1].ksl & 0x03)] << 6) | ((car_volume >= 0) ? car_volume : (p_vp->op[1].tl & 0x3F)));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + slot_mod, opl3_4n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x40 + slot_car, opl3_4n_car, p_opts);

    // AR/DR
    uint8_t opl3_6n_mod = (uint8_t)((p_vp->op[0].ar << 4) | (p_vp->op[0].dr & 0x0F));
    uint8_t opl3_6n_car = (uint8_t)((p_vp->op[1].ar << 4) | (p_vp->op[1].dr & 0x0F));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + slot_mod, opl3_6n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x60 + slot_car, opl3_6n_car, p_opts);

    // SL/RR
    uint8_t opl3_8n_mod = (uint8_t)((p_vp->op[0].sl << 4) | ((!p_vp->op[0].egt) ? (p_vp->op[0].rr & 0x0F) : 0));
    uint8_t opl3_8n_car = (uint8_t)((p_vp->op[1].sl << 4) | ((p_vp->op[1].egt || key) ? (p_vp->op[1].rr & 0x0F) : 6));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + slot_mod, opl3_8n_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0x80 + slot_car, opl3_8n_car, p_opts);

    // FB/CNT
    uint8_t c0_val = (uint8_t)(0xC0 | ((p_vp->fb[0] & 0x07) << 1) | (p_vp->cnt[0] & 0x01));
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xC0 + ch, c0_val, p_opts);

    // WS
    uint8_t opl3_en_mod = (uint8_t)((p_vp->op[0].ws)? 1 : 0);
    uint8_t opl3_en_car = (uint8_t)((p_vp->op[1].ws)? 1 : 0);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + slot_mod, opl3_en_mod, p_opts);
    wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xE0 + slot_car, opl3_en_car, p_opts);

    if (p_opts->debug.verbose ) {
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 AM: %s, Vibrato: %s, KSR: %s, EG Type: %d, Freq Multipler: %d\n",ch,opl3_2n_mod,(p_vp->op[0].am) ? "On":"Off",(p_vp->op[0].vib << 6) ? "On":"Off",(p_vp->op[0].ksr << 4) ? "On":"Off",(p_vp->op[0].egt << 5),(p_vp->op[0].mult & 0x0F));
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 AM: %s, Vibrato: %s, KSR: %s, EG Type: %d, Freq Multipler: %d\n",ch,opl3_2n_car,(p_vp->op[1].am) ? "On":"Off",(p_vp->op[1].vib << 6) ? "On":"Off",(p_vp->op[1].ksr << 4) ? "On":"Off",(p_vp->op[1].egt << 5),(p_vp->op[1].mult & 0x0F));
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Key Scaling: %d, Total Level: 0x%02x\n",ch,opl3_4n_mod,(p_vp->op[0].ksl & 0x03), (p_vp->op[0].tl & 0x3F));
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Key Scaling: %d, Total Level: 0x%02x\n",ch,opl3_4n_car,(p_vp->op[1].ksl & 0x03), (p_vp->op[1].tl & 0x3F));
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Attack Rate: %X, Decay Rate: %X\n",ch,opl3_6n_mod,p_vp->op[0].ar,p_vp->op[0].dr);
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Attack Rate: %X, Decay Rate: %X\n",ch,opl3_6n_car,p_vp->op[1].ar,p_vp->op[1].dr);
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 0 Sustain Level: %X, Release Rate: %X\n",ch,opl3_8n_mod,p_vp->op[0].sl,p_vp->op[0].rr);
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Op 1 Sustain Level: %X, Release Rate: %X\n",ch,opl3_8n_car,p_vp->op[1].sl,p_vp->op[1].rr);
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Waveform Select: %X\n",ch,opl3_en_mod,(p_vp->op[0].ws & 0x07));
        fprintf(stderr,"[DEBUG][Apply Voice] Ch %d Reg0x%02X : Waveform Select: %X\n",ch,opl3_en_car,(p_vp->op[0].ws & 0x07));
    }
    return wrote_bytes;
}

/**
 * Update OPL3 voice for the specified channel.
 * This function loads the voice and applies it to the OPL3 channel.
 */
int opll2opl3_update_voice(VGMContext *p_vgmctx, int ch, const CommandOptions *p_opts)
{
    OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
    uint8_t *regs = p_vgmctx->opll_state.reg;
    bool rflag = (p_vgmctx->opll_state.is_rhythm_mode != 0);
    uint8_t d = regs[0x30 + ch];
    int inst = (d & 0xF0) >> 4;
    int volume = d & 0x0F;
    bool key = (regs[0x20 + ch] & 0x10) ? true : false;
    int wrote_bytes = 0;
    
    OPLL2OPL3_PendingChannel *p = &p_s->ch[ch];
    OPL3VoiceParam vp;

    if (rflag && ch >= 6) {
        // Rhythm mode: use rhythm voice presets
        switch (ch) {
            case 6:
                opll_load_voice(p_vgmctx, 16, ch, &vp, p_opts); // BD
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, -1 ,toTL(volume, 0), key,&vp, p_opts);
                break;
            case 7:
                opll_load_voice(p_vgmctx, 17, ch, &vp, p_opts); // SD/TOM
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
                break;
            case 8:
                opll_load_voice(p_vgmctx, 18, ch, &vp, p_opts); // CYM/HH
                wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
                break;
        }
    } else if (!rflag && ch >= 6) {
        // Rhythm mode off: restore normal voice for ch6/7/8
        int inst_normal = (regs[0x30 + ch] >> 4) & 0x0F;
        if (inst_normal == 0) {
            // User patch
            opll_load_voice(p_vgmctx, 0, ch, &vp, p_opts);
            wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
        } else {
            opll_load_voice(p_vgmctx, inst_normal, ch, &vp, p_opts);
            wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, toTL(inst, 0) ,toTL(volume, 0), key,&vp, p_opts);
        }
    } else {
        // Normal melodic channel
        int inst_normal = (regs[0x30 + ch] >> 4) & 0x0F;
        if (inst_normal == 0) {
            opll_load_voice(p_vgmctx, 0, ch, &vp, p_opts);
        } else {
            opll_load_voice(p_vgmctx, inst_normal, ch, &vp, p_opts);
        }
        wrote_bytes += opll2opl3_apply_voice(p_vgmctx, ch, -1, toTL(volume, 0), key,&vp, p_opts);
    }

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] ch=%d inst=%d vol=%d key=%d rhythm=%d\n",
            ch, inst, volume, key, rflag);
    }
    return wrote_bytes;
}

int opll2opl3_handle_opll_command (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, const CommandOptions *p_opts) 
{
    int wrote_bytes = 0;
    if (reg == 0x0e) {
        OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
        int lfoDepth = OPLL_LFO_DEPTH; 

        bool prev_rhythm_mode = p_vgmctx->opll_state.is_rhythm_mode;
        bool now_rhythm_mode  = (val & 0x20) != 0;

        int mod_slots[3] = { opl3_opreg_addr(0, 6, 0), opl3_opreg_addr(0, 7, 0), opl3_opreg_addr(0, 8, 0) };
        int car_slots[3] = { opl3_opreg_addr(0, 6, 1), opl3_opreg_addr(0, 7, 1), opl3_opreg_addr(0, 8, 1) };

        if (now_rhythm_mode && !prev_rhythm_mode) {
            // Rhythm mode enabled （posedge）
            p_vgmctx->opll_state.is_rhythm_mode = true;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts); // BD
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts); // SD/TOM
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts); // CYM/HH
        } else if (!now_rhythm_mode && prev_rhythm_mode) {
            // Rhythm mode disabled（negedge）
            p_vgmctx->opll_state.is_rhythm_mode = false;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts);
            // 1. Write LFO Depth = 0 once（0xc0 | (val & 0x3f)）
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, 0xc0 | (val & 0x3f), p_opts);
        } else {
                // Only update the flag (no edge)
                p_vgmctx->opll_state.is_rhythm_mode = (val & 0x20) ? true : false;
        }
         // 2. Write LFO Depth at least once for all cases
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, (lfoDepth << 6) | (val & 0x3f), p_opts);
        return wrote_bytes;
    }
        /* --- FNUM Low (0x10..0x18) --- */
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        p->fnum_comb = (uint16_t)((p->fnum_comb & 0x300) | val);
        p->has_fnum_low = true;
        p->last_reg_10 = val;

        opll2opl3_debug_log(p_vgmctx, "HANDLE", "FNUM Low",ch, p_opts);
        bool keybit   = (val & 0x80) != 0;
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3 Before conversion: block=%u, fnum=0x%03X (dec %u) src_clock=%.1f dst_clock=%.1f\n",
                p->block, p->fnum_comb & 0x3FF, p->fnum_comb & 0x3FF,
                p_vgmctx->source_fm_clock, p_vgmctx->target_fm_clock);
        }

        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;
            (void)convert_fnum_block_from_opll_to_opl3(
                p_vgmctx->source_fm_clock,
                p_vgmctx->target_fm_clock,
                p->block,
                p->fnum_comb,
            &dst_fnum,
            &dst_block);
        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] OPLL→OPL3 After conversion: dst_block=%u, dst_fnum=0x%03X (dec %u)\n",
                dst_block, dst_fnum, dst_fnum);
        }

        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr, "[DEBUG][0x10] ch=%d val=0x%02X (should be FNUM-LSB)\n", ch, val);
        }
        uint8_t reg_bn = ((p_vgmctx->opll_state.reg[0x20 + ch]  & 0x1F) << 1) | ((val & 0x80) >> 7) ;
        uint8_t reg_an = (val & 0x7f) << 1;

        if (p_opts && p_opts->debug.verbose) {
            fprintf(stderr,
                "[DEBUG] reg_bn:0x%02x reg_an:0x%02x\n",reg_bn,reg_an);
        }
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts);
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts);
        return wrote_bytes;
    }

    /* --- FNUM High + Block + Key (0x20..0x28) --- */
    if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);

        uint8_t fhi   = val & 0x03;
        uint8_t block = (val >> 2) & 0x07;
        bool keybit   = (val & 0x10) != 0;
        bool prev_key = p->key_state;

        // FNUM/Block/Key update
        p->fnum_high = fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->last_reg_10 & 0xFF));
        p->block = block;
        p->last_reg_20 = val;

        wrote_bytes += opll2opl3_update_voice(p_vgmctx, ch, p_opts);

        // KeyOn/KeyOff含むA0/B0レジスタ書き込み（OPLL的なNoteOn/Off反映）
        uint8_t reg_bn = ((val  & 0x1F) << 1) | ((p_vgmctx->opll_state.reg[0x10 + ch] & 0x80) >> 7);
        uint8_t reg_an = (p->last_reg_10 & 0x7f) << 1;
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + ch, reg_bn, p_opts);
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + ch, reg_an, p_opts);
        p->key_state = keybit;
        p->has_fnum_high = true;
        return wrote_bytes;
    }

    /* --- Instrument/Volume (0x30..0x38) --- */
    // Instrument/Volume (0x30..0x38)
    if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;

        p->last_reg_30 = val;
        p->voice_id = (val >> 4) & 0x0F;
        p->tl = val & 0x0F;

        opll2opl3_debug_log(p_vgmctx, "HANDLE", "Instrument/Volume", ch, p_opts);
        wrote_bytes += opll2opl3_update_voice(p_vgmctx, ch, p_opts);

        return wrote_bytes;
    }
    return wrote_bytes;
}

int opll2opl3_flush_command_buffer (VGMContext *p_vgmctx, const CommandOptions *p_opts) {
    int wrote_bytes = 0;
    OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);
    // Extract reg0x2n
    bool keybit   = (p_command_buffer->reg0x2n & 0x10) != 0;
    uint8_t reg_an = (p_command_buffer->reg0x1n & 0x7f) << 1;
    uint8_t reg_bn = ((p_command_buffer->reg0x2n  & 0x1F) << 1) | ((p_command_buffer->reg0x1n & 0x80) >> 7);

    if (p_command_buffer->prev_keybit && !keybit) {
        // KeyBit 1 -> 0 : 0x20 (KeyOff) > 0x10(FnumL) > 0x30 (INST/Vol)
        if (p_opts->debug.verbose) {
        fprintf(stderr,
            "[OPLL2OPL3][FLUSH] KeyBit 1 -> 0 :0x20:0x%02x (KeyOff) > 0x10(FnumL):0x%02x > 0x30 (INST/Vol):0x%02x \n"
            ,p_command_buffer->reg0x2n, p_command_buffer->reg0x1n,p_command_buffer->reg0x3n
        );
        }
        // reg1n
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + p_command_buffer->ch, reg_bn, p_opts);

        // reg2n
        if (p_command_buffer->wait1 > 0) {
            wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait1, p_opts, &(p_vgmctx->opll_state.sch));
        }
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + p_command_buffer->ch, reg_an, p_opts);

        // reg3n
        if (p_command_buffer->wait2 > 0) {
            wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait2, p_opts, &(p_vgmctx->opll_state.sch));
        }

        
        wrote_bytes += opll2opl3_update_voice(p_vgmctx, p_command_buffer->ch, p_opts);
    }
    else if (!p_command_buffer->prev_keybit && keybit) {
        // KeyBit 0 -> 1 : 0x30 (INST/Vol) > 0x10(FnumL) > 0x20 (KeyOn)
        fprintf(stderr,
            "[OPLL2OPL3][FLUSH] KeyBit 0 -> 1 :0x30:0x%02x (Inst/Val)) > wait1:%d > 0x10(FnumL):0x%02x > wait2:%d > 0x20 (KeyOn):0x%02x \n"
            ,p_command_buffer->reg0x3n, p_command_buffer->wait1, p_command_buffer->reg0x1n,  p_command_buffer->wait2, p_command_buffer->reg0x2n
        );

        // reg3n
        wrote_bytes += opll2opl3_update_voice(p_vgmctx, p_command_buffer->ch, p_opts);

        // reg 1n
        if (p_command_buffer->wait1 > 0) {
            wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait1, p_opts, &(p_vgmctx->opll_state.sch));
        }
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + p_command_buffer->ch, reg_an, p_opts);

        // reg2n
        if (p_command_buffer->wait2 > 0) {
            wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait2, p_opts, &(p_vgmctx->opll_state.sch));
        }
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + p_command_buffer->ch, reg_bn, p_opts);
    }
    else {
        // No Edge of KeyOn
            if (!keybit) {
            if (p_opts->debug.verbose) {
                fprintf(stderr,
                    "[OPLL2OPL3][FLUSH] KeyBit 0 -> 0 :0x20:0x%02x (KeyOff) > wait1:%d > 0x10(FnumL):0x%02x >wait2:%d> 0x30 (INST/Vol):0x%02x \n"
                    ,p_command_buffer->reg0x2n,p_command_buffer->wait1, p_command_buffer->reg0x1n,p_command_buffer->wait2, p_command_buffer->reg0x3n
                );
            }
            // reg1n
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xB0 + p_command_buffer->ch, reg_bn, p_opts);

            // reg2n
            if (p_command_buffer->wait1 > 0) {
                wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait1, p_opts, &(p_vgmctx->opll_state.sch));
            }
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xA0 + p_command_buffer->ch, reg_an, p_opts);

            // reg3n
            if (p_command_buffer->wait2 > 0) {
                wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, p_command_buffer->wait2, p_opts, &(p_vgmctx->opll_state.sch));
            }
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, p_command_buffer->ch, p_opts);
            }
    }
    p_command_buffer->prev_keybit  = keybit;

    // Reset Command Buffer
    p_command_buffer->has_reg0x1n = false;
    p_command_buffer->has_reg0x2n = false;
    p_command_buffer->has_reg0x3n = false;
    p_command_buffer->wait_count  = 0;
    p_command_buffer->wait1  = 0;
    p_command_buffer->wait2  = 0;
    
    return wrote_bytes;
}


int opll2opl3_handle_opll_command2 (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, const CommandOptions *p_opts) 
{
    int wrote_bytes = 0;
    if (reg == 0x0e) {
        OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
        OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);

        int lfoDepth = OPLL_LFO_DEPTH; 

        bool prev_rhythm_mode = p_vgmctx->opll_state.is_rhythm_mode;
        bool now_rhythm_mode  = (val & 0x20) != 0;

        int mod_slots[3] = { opl3_opreg_addr(0, 6, 0), opl3_opreg_addr(0, 7, 0), opl3_opreg_addr(0, 8, 0) };
        int car_slots[3] = { opl3_opreg_addr(0, 6, 1), opl3_opreg_addr(0, 7, 1), opl3_opreg_addr(0, 8, 1) };

        if (now_rhythm_mode && !prev_rhythm_mode) {
            // Rhythm mode enabled （posedge）
            p_vgmctx->opll_state.is_rhythm_mode = true;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts); // BD
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts); // SD/TOM
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts); // CYM/HH
        } else if (!now_rhythm_mode && prev_rhythm_mode) {
            // Rhythm mode disabled（negedge）
            p_vgmctx->opll_state.is_rhythm_mode = false;
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 6, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 7, p_opts);
            wrote_bytes += opll2opl3_update_voice(p_vgmctx, 8, p_opts);
            // 1. Write LFO Depth = 0 once（0xc0 | (val & 0x3f)）
            wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, 0xc0 | (val & 0x3f), p_opts);
        } else {
                // Only update the flag (no edge)
                p_vgmctx->opll_state.is_rhythm_mode = (val & 0x20) ? true : false;
        }
         // 2. Write LFO Depth at least once for all cases
        wrote_bytes += opll2opl3_emit_reg_write(p_vgmctx, 0xbd, (lfoDepth << 6) | (val & 0x3f), p_opts);
        return wrote_bytes;
    }
    
    OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);
    /* --- FNUM Low (0x10..0x18) --- */
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        p->fnum_comb = (uint16_t)((p->fnum_comb & 0x300) | val);
        p->has_fnum_low = true;
        p->last_reg_10 = val;

        opll2opl3_debug_log(p_vgmctx, "HANDLE", "FNUM Low",ch, p_opts);
        bool keybit   = (val & 0x80) != 0;

        // Update Command Buffer
        p_command_buffer->has_reg0x1n = true;
        p_command_buffer->reg0x1n = val;
        p_command_buffer->ch = ch;
    }

    /* --- FNUM High + Block + Key (0x20..0x28) --- */
    if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);

        uint8_t fhi   = val & 0x03;
        uint8_t block = (val >> 2) & 0x07;
        bool keybit   = (val & 0x10) != 0;
        bool prev_key = p->key_state;

        // FNUM/Block/Key update
        p->fnum_high = fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->last_reg_10 & 0xFF));
        p->block = block;
        p->last_reg_20 = val;

        wrote_bytes += opll2opl3_update_voice(p_vgmctx, ch, p_opts);

        // KeyOn/KeyOff含むA0/B0レジスタ書き込み（OPLL的なNoteOn/Off反映）
        p->key_state = keybit;
        p->has_fnum_high = true;
    
        // Update Command Buffer
        p_command_buffer->has_reg0x2n = true;
        p_command_buffer->reg0x2n = val;
        p_command_buffer->ch = ch;
    }

    /* --- Instrument/Volume (0x30..0x38) --- */
    // Instrument/Volume (0x30..0x38)
    if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg & 0x0F;
        OPLL2OPL3_PendingChannel *p = &(p_vgmctx->opll_state.sch.ch[ch]);
        uint16_t dst_fnum = (uint16_t)(p->fnum_comb & 0x3FF);
        uint8_t dst_block = p->block & 0x07;

        p->last_reg_30 = val;
        p->voice_id = (val >> 4) & 0x0F;
        p->tl = val & 0x0F;

        // Update Command Buffer
        p_command_buffer->has_reg0x3n = true;
        p_command_buffer->reg0x3n = val;
        p_command_buffer->ch = ch;
    }

    if (p_command_buffer->has_reg0x1n && p_command_buffer->has_reg0x2n) {
        // Flush 0x10, 0x20, 0x30
        if (p_opts->debug.verbose) {
            fprintf(stderr,
                "[OPLL2OPL3][COMMAND2] call flush reg0x1n:0x%02x reg0x2n:0x%02x reg0x2n:0x%02x\n",
               p_command_buffer->reg0x1n, p_command_buffer->reg0x2n, p_command_buffer->reg0x3n
            );
        }

        opll2opl3_flush_command_buffer(p_vgmctx, p_opts);
    }

    return wrote_bytes;
}


int opll2opl3_schedule_wait(VGMContext *p_vgmctx, uint32_t wait_samples, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    int wrote_bytes = 0;
    OPLL2OPL3_Scheduler *p_s = &(p_vgmctx->opll_state.sch);
    p_s->wait_count++;
    if (p_opts->debug.verbose) {
        fprintf(stderr,
            "[OPLL2OPL3][WAIT] wait_count: %d wait_samples:%d \n",p_s->wait_count, wait_samples
        );
    }

    wrote_bytes += emit_wait(p_vgmctx, wait_samples, s, p_opts);
    return wrote_bytes;
}


int opll2opl3_schedule_wait2(VGMContext *p_vgmctx, uint32_t wait_samples, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
    OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);

    switch ( p_command_buffer->wait_count) {
        case 0 : p_command_buffer->wait1 = wait_samples;
            break;
        case 1 : p_command_buffer->wait2 = wait_samples;
            break;
        default :
            fprintf(stderr,
            "[OPLL2OPL3][WAIT2] Too much wait !!! wait_count:%d\n",
            p_command_buffer->wait_count
            );
    }

    p_command_buffer->wait_count++;
    if (p_opts->debug.verbose) {
        fprintf(stderr,     
            "[OPLL2OPL3][WAIT2] wait_count:%d wait_samples:%d wait1:0x%d wait2:0x%d\n",
            p_command_buffer->wait_count, wait_samples, p_command_buffer->wait1, p_command_buffer->wait2
        );
    }
}

/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll2opl3_command_handler (VGMContext *p_vgmctx, uint8_t reg, uint8_t val, uint16_t wait_samples, const CommandOptions *p_opts)
 {
    int wrote_bytes = 0;
    // Update timestamp
    p_vgmctx->opll_state.sch.virtual_time = p_vgmctx->timestamp.current_sample;
    
    if (p_opts->debug.verbose) {
        fprintf(stderr,
        "[OPLL2OPL3][HANDLER][%s] virtual_time:%llu emit_time:%llu --- reg:0x%02x val:0x%02x Sample:%d\n",
        (p_vgmctx->cmd_type == VGMCommandType_RegWrite) ? "RegWrite" : "Wait",
        (unsigned long long)p_vgmctx->opll_state.sch.virtual_time,
        (unsigned long long)p_vgmctx->opll_state.sch.emit_time,reg,val,wait_samples
        );
    }

    if (p_opts->opll_convert_method == OPLL_ConvertMethod_VGMCONV) {
        if (p_vgmctx->cmd_type == VGMCommandType_RegWrite) {
            p_vgmctx->opll_state.reg_stamp[reg] = p_vgmctx->opll_state.reg[reg];
            p_vgmctx->opll_state.reg[reg] = val;
            wrote_bytes += opll2opl3_handle_opll_command(p_vgmctx, reg, val, p_opts);
        } else if (p_vgmctx->cmd_type == VGMCommandType_Wait) {
            p_vgmctx->opll_state.sch.virtual_time += wait_samples;
            wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, wait_samples, p_opts, &(p_vgmctx->opll_state.sch));
        } else {
            // Should not be occured here
        }
    } else {
        if (p_vgmctx->cmd_type == VGMCommandType_RegWrite) {
            p_vgmctx->opll_state.reg_stamp[reg] = p_vgmctx->opll_state.reg[reg];
            p_vgmctx->opll_state.reg[reg] = val;
            wrote_bytes += opll2opl3_handle_opll_command2(p_vgmctx, reg, val, p_opts);
        } else if (p_vgmctx->cmd_type == VGMCommandType_Wait) {
            OPLL_CommandBuffer *p_command_buffer = &(p_vgmctx->opll_state.sch.command_buffer);
            if (p_command_buffer->has_reg0x1n || p_command_buffer->has_reg0x2n || p_command_buffer->has_reg0x3n) {
                wrote_bytes += opll2opl3_schedule_wait2(p_vgmctx, wait_samples, p_opts, &(p_vgmctx->opll_state.sch));
            } else {
                p_vgmctx->opll_state.sch.virtual_time += wait_samples;
                wrote_bytes += opll2opl3_schedule_wait(p_vgmctx, wait_samples, p_opts, &(p_vgmctx->opll_state.sch));
            }
        } else {
            // Should not be occured here
        }
    }
    
    return wrote_bytes;
}

