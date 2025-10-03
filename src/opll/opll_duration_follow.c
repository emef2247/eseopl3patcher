#include "opll_duration_follow.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static inline float eg_att_to_db_approx_int(int att_raw) {
    if (att_raw < 0) return -240.0f;
    if (att_raw > 1023) att_raw = 1023;
    return -(float)att_raw * (96.0f / 1023.0f);
}

static inline int parse_bool_env(const char* v) {
    if (!v) return 0;
    return (v[0]=='1'||v[0]=='y'||v[0]=='Y'||v[0]=='t'||v[0]=='T');
}

void opll_duration_follow_init(OpllDurationFollow* f, ymfm_ctx_t* ymfm, const OpllDurationCfg* cfg) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->ymfm = ymfm;
    /* デフォルト */
    f->cfg.enabled = 1;
    f->cfg.end_db = -80.0f;
    f->cfg.min_gate_samples = 1024;
    f->cfg.end_hold_samples = 1024;
    f->cfg.start_grace_samples = 256;
    f->cfg.use_tl_mute = 0;
    if (cfg) f->cfg = *cfg;

    const char* v;
    v = getenv("ESEOPL3_DURATION_ENABLE");
    if (v && *v) f->cfg.enabled = parse_bool_env(v) ? 1 : 0;

    v = getenv("ESEOPL3_DURATION_DB");        if (v && *v) f->cfg.end_db = (float)atof(v);
    v = getenv("ESEOPL3_DURATION_MIN_GATE");  if (v && *v) { int t=atoi(v); if (t>=0) f->cfg.min_gate_samples=(uint32_t)t; }
    v = getenv("ESEOPL3_DURATION_HOLD");      if (v && *v) { int t=atoi(v); if (t>=0) f->cfg.end_hold_samples=(uint32_t)t; }
    v = getenv("ESEOPL3_DURATION_START_GRACE"); if (v && *v) { int t=atoi(v); if (t>=0) f->cfg.start_grace_samples=(uint32_t)t; }
    f->cfg.use_tl_mute = parse_bool_env(getenv("ESEOPL3_DURATION_TL_MUTE"));

}

void opll_duration_follow_on_ko_on(OpllDurationFollow* f, int ch) {
    if (!f || ch < 0 || ch >= 9) return;
    memset(&f->ch[ch], 0, sizeof(f->ch[ch]));
    f->ch[ch].active = 1;
    f->ch[ch].grace_left = f->cfg.start_grace_samples;
}

void opll_duration_follow_on_ko_off(OpllDurationFollow* f, int ch) {
    if (!f || ch < 0 || ch >= 9) return;
    if (!f->ch[ch].active) return;
    f->ch[ch].saw_ko_off = 1;
}

uint32_t opll_duration_follow_on_wait(OpllDurationFollow* f, int ch, uint32_t wait_samples) {
    if (!f || ch < 0 || ch >= 9 || wait_samples == 0) return DURA_ACT_NONE;
    if (!f->cfg.enabled) return DURA_ACT_NONE;

    OpllDurationCh* s = &f->ch[ch];
    if (!s->active) return DURA_ACT_NONE;

    /* YMFM進行（emit_waitぶん） */
    (void)ymfm_step_and_measure_db(f->ymfm, wait_samples);

    /* KO_ON直後の猶予期間は終了判定しない */
    uint32_t step = wait_samples;
    if (s->grace_left > 0) {
        if (s->grace_left >= step) {
            s->grace_left -= step;
            s->since_on   += step;
            return DURA_ACT_NONE;
        } else {
            step -= s->grace_left;
            s->since_on += s->grace_left;
            s->grace_left = 0;
            /* 以降の step については通常評価 */
        }
    }

    int att_car = ymfm_get_op_env_att(f->ymfm, ch, 1);
    float att_db = eg_att_to_db_approx_int(att_car);

    if (att_db <= f->cfg.end_db) s->below_cnt += step;
    else                         s->below_cnt  = 0;
    s->since_on += step;

    if (!s->recommended_off) {
        int gate_ok = (s->since_on >= f->cfg.min_gate_samples);
        int settled = (s->below_cnt >= f->cfg.end_hold_samples);
        if (gate_ok && settled) {
            s->recommended_off = 1;
            uint32_t act = DURA_ACT_KEYOFF;
            if (f->cfg.use_tl_mute) act |= DURA_ACT_TL_MUTE;
            return act;
        }
    }
    return DURA_ACT_NONE;
}

int opll_duration_follow_finished(const OpllDurationFollow* f, int ch) {
    if (!f || ch < 0 || ch >= 9) return 1;
    return f->ch[ch].active ? 0 : 1;
}

void opll_duration_follow_get_state(const OpllDurationFollow* f, int ch,
                                    int* saw_ko_off, uint32_t* since_on,
                                    uint32_t* below_cnt) {
    if (!f || ch < 0 || ch >= 9) return;
    const OpllDurationCh* s = &f->ch[ch];
    if (saw_ko_off) *saw_ko_off = s->saw_ko_off;
    if (since_on)   *since_on   = s->since_on;
    if (below_cnt)  *below_cnt  = s->below_cnt;
}