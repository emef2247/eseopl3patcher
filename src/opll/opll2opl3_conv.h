#ifndef ESEOPL3PATCHER_OPLL2OPL3_CONV_H
#define ESEOPL3PATCHER_OPLL2OPL3_CONV_H

#include "../opl3/opl3_convert.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include <string.h>

typedef uint64_t sample_t; // sample 単位の時間

// timing (samples) - adjust to target sample rate (e.g., 44100)
#define OPLL_SAMPLE_RATE            44100
#define OPLL_MIN_GATE_MS            2        // 最短ゲート補償 (ms)
#define OPLL_MIN_GATE_SAMPLES       ((OPLL_SAMPLE_RATE * OPLL_MIN_GATE_MS) / 1000) // ≒88@44.1kHz
#define OPLL_MAX_PENDING_MS         50       // 保留上限 (ms)
#define OPLL_MAX_PENDING_SAMPLES    ((OPLL_SAMPLE_RATE * OPLL_MAX_PENDING_MS) / 1000) // ≒2205@44.1kHz
#define OPLL_NUM_CHANNELS 9
#define YM2413_REGS_SIZE 0x40

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
    bool    accessed[0x200];
    uint8_t last_emitted_reg_val[0x200];
} OPLL2OPL3_PendingChannel;

typedef struct {
    sample_t  virtual_time; // 入力（解析）側の進行時間（samples）
    sample_t  emit_time;    // 出力済みVGMの進行時間（samples）
    OPLLState opll_state;
    uint8_t lfo_reg;
    OPLL2OPL3_PendingChannel ch[OPLL_NUM_CHANNELS];
} OPLL2OPL3_Scheduler;


void opll2opl3_init_scheduler() ;

int opll2opl_command_handler (
    VGMContext *p_vgmctx,
    uint8_t reg, uint8_t val, uint16_t wait_samples,
    const CommandOptions *p_opts);

#endif /* ESEOPL3PATCHER_OPLL2OPL3_CONV_H */