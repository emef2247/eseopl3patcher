#ifndef OPLL_STATE_H
#define OPLL_STATE_H

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t sample_t; // sample 単位の時間

// timing (samples) - adjust to target sample rate (e.g., 44100)
#define OPLL_SAMPLE_RATE            44100
#define OPLL_MIN_GATE_MS            2        // 最短ゲート補償 (ms)
#define OPLL_MIN_GATE_SAMPLES       ((OPLL_SAMPLE_RATE * OPLL_MIN_GATE_MS) / 1000) // ≒88@44.1kHz
#define OPLL_MAX_PENDING_MS         50       // 保留上限 (ms)
#define OPLL_MAX_PENDING_SAMPLES    ((OPLL_SAMPLE_RATE * OPLL_MAX_PENDING_MS) / 1000) // ≒2205@44.1kHz
#define OPLL_NUM_CHANNELS 9
#define YM2413_REGS_SIZE 0x40
#define OPLL_LFO_DEPTH 3

typedef enum {
    OPLL_CH_TYPE_INVALID = 0,
    OPLL_CH_TYPE_MELODIC,
    OPLL_CH_TYPE_RHYTHM
} OPLL_ChannelType;

typedef struct {
    int ch_index;       // 0〜8 (or -1)
    OPLL_ChannelType type;   // melodic / rhythm / invalid
} OPLL_ChannelInfo;

typedef struct {
    /* register-arrival flags */
    bool has_fnum_low;   // 1n (0x10..0x18)
    bool has_fnum_high;  // 2n (0x20..0x28 includes key bit and block)
    bool has_tl;         // 0x30..0x38
    bool has_voice;      // 0x30..0x38 upper bits or 0x00..0x07 user patch

    // Key/edge state
    bool has_keybit_stamp;    // last observed register key bit (register state)
    bool has_keybit;         // whether key bit was seen (1)
    bool is_pending;        // we delayed keyon because not all pieces arrived
    bool is_pending_keyoff; // KeyOff waiting to be applied (handled in flush)
    bool is_active;         // currently logically KeyOn (after flush active=true)
    bool is_keyoff_forced;  // internal marker for forced (retrigger) off
    bool ignore_first_tl;
    
    // Register cache (for edge detect + diagnostics)
    uint8_t last_reg_10;
    uint8_t last_reg_20;
    uint8_t last_reg_30;

    // Freq/voice cache
    uint8_t  fnum_low;
    uint8_t  fnum_high;   // lower 2 bits used
    uint16_t fnum_comb;  // assembled 10-bit fnum (maintain)
    uint16_t last_fnum_comb;
    uint8_t  block;
    uint8_t  key_state;
    uint8_t  prev_keybit;
    uint8_t  last_block;
    uint8_t  tl;
    uint8_t  voice_id;

    // Timing
    sample_t keyon_time;  // when key-on was detected (used for gate length)
    sample_t last_emit_time; 
} OPLL2OPL3_PendingChannel;

typedef struct {
    int  ch;
    bool has_reg0x1n;
    bool has_reg0x2n;
    bool has_reg0x3n;
    bool prev_keybit;
    uint8_t reg0x1n;
    uint8_t reg0x2n;
    uint8_t reg0x3n;
    int     wait_count;
    sample_t    wait1;
    sample_t    wait2;
} OPLL_CommandBuffer;

typedef struct {
    sample_t    virtual_time; // 入力（解析）側の進行時間（samples）
    sample_t    emit_time;    // 出力済みVGMの進行時間（samples）
    bool        accessed[0x200];
    uint8_t     last_emitted_reg_val[0x200];
    int         wait_count;
    OPLL2OPL3_PendingChannel ch[OPLL_NUM_CHANNELS];
    OPLL_CommandBuffer command_buffer;
} OPLL2OPL3_Scheduler;

typedef struct {
    uint8_t  reg[0x200];
    uint8_t  reg_stamp[0x200];
    uint8_t  patches[19][8];
    bool     is_rhythm_mode;
    bool     is_initialized;
    uint8_t  lfo_depth;  // Staged A0 values per channel
    OPLL2OPL3_Scheduler sch;
} OPLLState;

#endif /* OPLL_STATE_H */