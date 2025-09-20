#ifndef ESEOPL3PATCHER_OPL3_STATE_H
#define ESEOPL3PATCHER_OPL3_STATE_H

#include <stdint.h>
#include "../compat_bool.h"

/* Basic macros */
#define OPL3_DB_INITIAL_SIZE 64
#define OPL3_NUM_CHANNELS    18   /* 9 (port0) + 9 (port1) */
#define OPL3_MODE_2OP        0
#define OPL3_MODE_4OP        1

/* Operator parameter */
typedef struct OPL3OpParam {
    uint8_t am, vib, egt, ksr, mult;
    uint8_t ksl, tl;
    uint8_t ar, dr, sl, rr;
    uint8_t ws;
} OPL3OpParam;
/* Backward compatibility alias */
typedef OPL3OpParam OPL3OperatorParam;

/* Voice parameter (2-op or 4-op; current conversion uses 2-op) */
typedef struct OPL3VoiceParam {
    OPL3OpParam op[4];   /* allocate room for future 4-op (op[0..1] used now) */
    uint8_t fb[2];
    uint8_t cnt[2];
    uint8_t is_4op;
    int     voice_no;
    int     source_fmchip;
} OPL3VoiceParam;

/* Voice database */
typedef struct OPL3VoiceDB {
    int count;
    int capacity;
    OPL3VoiceParam *p_voices;
} OPL3VoiceDB;

/* Main OPL3 register/state mirror */
typedef struct {
    uint8_t  reg[0x200];
    uint8_t  reg_stamp[0x200];
    int      rhythm_mode;
    int      opl3_mode_initialized;
    int      source_fmchip;
    OPL3VoiceDB voice_db;
} OPL3State;

#endif /* ESEOPL3PATCHER_OPL3_STATE_H */