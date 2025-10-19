#ifndef ESEOPL3PATCHER_OPL3_STATE_H
#define ESEOPL3PATCHER_OPL3_STATE_H

#include <stdint.h>
#include <stdbool.h>

#define OPL3_DB_INITIAL_SIZE 64
#define OPL3_NUM_CHANNELS    18   // 9 (port0) + 9 (port1)
#define OPL3_MODE_2OP        0
#define OPL3_MODE_4OP        1

typedef struct OPL3OpParam {
    uint8_t am, vib, egt, ksr, mult;
    uint8_t ksl, tl;
    uint8_t ar, dr, sl, rr;
    uint8_t ws;
} OPL3OpParam;

typedef OPL3OpParam OPL3OperatorParam;

typedef struct OPL3VoiceParam {
    OPL3OpParam op[4];     // [0]=mod, [1]=car, [2-3]=future 4op
    uint8_t fb[2];
    uint8_t cnt[2];
    uint8_t is_4op;
    int     voice_no;
    int     source_fmchip;
} OPL3VoiceParam;

typedef struct OPL3VoiceDB {
    int count;
    int capacity;
    OPL3VoiceParam *p_voices;
} OPL3VoiceDB;

/** Main OPL3 register/state mirror */
typedef struct {
    uint8_t  reg[0x200];
    uint8_t  reg_stamp[0x200];
    uint8_t     last_key[OPL3_NUM_CHANNELS];     // true=KeyOn, false=KeyOff
    uint32_t post_keyon_sample[OPL3_NUM_CHANNELS];
    uint32_t post_keyon_valid[OPL3_NUM_CHANNELS];
    int      rhythm_mode;
    int      opl3_mode_initialized;
    int      source_fmchip;
    OPL3VoiceDB voice_db;
    // Channel-local staging for A0 (FNUM LSB) to ensure atomic A0+B0 writes
    uint8_t staged_fnum_lsb[OPL3_NUM_CHANNELS];  // Staged A0 values per channel
    bool staged_fnum_valid[OPL3_NUM_CHANNELS];   // Whether staged A0 value is valid
    int pair_an_bn_enabled; // 1: Pearing enabled, 0: Pearing disabled
} OPL3State;

#endif /* ESEOPL3PATCHER_OPL3_STATE_H */