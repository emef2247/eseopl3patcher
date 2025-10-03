#ifndef ESEOPL3PATCHER_OPLL_DURATION_FOLLOW_H
#define ESEOPL3PATCHER_OPLL_DURATION_FOLLOW_H

#include <stdint.h>
#include <stdbool.h>
#include "../../include/ymfm_c_api.h"

#define DURA_ACT_NONE     0u
#define DURA_ACT_KEYOFF   1u
#define DURA_ACT_TL_MUTE  2u

typedef struct {
    int      enabled;           /* 0/1: 有効化フラグ（ESEOPL3_DURATION_ENABLE） */
    float    end_db;            /* 例: -80.0 */
    uint32_t min_gate_samples;  /* 最小ゲート長 */
    uint32_t end_hold_samples;  /* しきい値下回り連続サンプル数 */
    uint32_t start_grace_samples; /* KO_ON後の猶予サンプル（終了判定しない期間） */
    int      use_tl_mute;       /* 0/1: KeyOff時にTL=63のハードミュートを併用 */
} OpllDurationCfg;

typedef struct {
    int      active;
    uint32_t since_on;
    uint32_t below_cnt;
    uint32_t grace_left;        /* KO_ON直後の猶予残りサンプル */
    int      saw_ko_off;
    int      recommended_off;
} OpllDurationCh;

typedef struct {
    ymfm_ctx_t* ymfm;
    OpllDurationCfg cfg;
    OpllDurationCh  ch[9];
} OpllDurationFollow;

void opll_duration_follow_init(OpllDurationFollow* f, ymfm_ctx_t* ymfm, const OpllDurationCfg* cfg);
void opll_duration_follow_on_ko_on(OpllDurationFollow* f, int ch);
void opll_duration_follow_on_ko_off(OpllDurationFollow* f, int ch);
uint32_t opll_duration_follow_on_wait(OpllDurationFollow* f, int ch, uint32_t wait_samples);
int opll_duration_follow_finished(const OpllDurationFollow* f, int ch);
void opll_duration_follow_get_state(const OpllDurationFollow* f, int ch,
                                    int* saw_ko_off, uint32_t* since_on, uint32_t* below_cnt);

#endif /* ESEOPL3PATCHER_OPLL_DURATION_FOLLOW_H */