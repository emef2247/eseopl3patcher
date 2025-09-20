#include "ratemap_integrated.h"
#include <stdio.h>

/* ==== 内部テーブル ==== */
static const unsigned char MAP_SIMPLE[16] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const unsigned char MAP_CALIBV2_AR[16] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15
};
static const unsigned char MAP_CALIBV2_DR[16] = {
    1,2,3,4,5,6,7,8,9,10,12,13,14,15,15,15
};
static const unsigned char MAP_CALIBV3_AR[16] = {
    4,5,6,7,8,9,10,11,12,13,14,15,15,15,15,15
};
static const unsigned char MAP_CALIBV3_DR[16] = {
    2,3,4,5,6,7,8,9,10,11,12,13,14,14,15,15
};

/* ==== DYNAMIC SHAPEFIX 統計 ==== */
static double g_gap_avg = 0.0;
static int    g_gap_count = 0;
static int    g_gap_max = 0;

/* ==== 環境変数ユーティリティ ==== */
static bool env_flag(const char *name) {
    const char *e = getenv(name);
    if(!e) return false;
    return (e[0] == '1' || e[0] == 't' || e[0] == 'T' || e[0] == 'y' || e[0] == 'Y');
}

static ratemap_profile_t detect_profile(const char *s) {
    if(!s) return RMAP_PROF_SIMPLE;
    if(strcasecmp(s,"calibv2")==0) return RMAP_PROF_CALIBV2;
    if(strcasecmp(s,"calibv3")==0) return RMAP_PROF_CALIBV3;
    return RMAP_PROF_SIMPLE;
}

static shapefix_mode_t detect_shapefix_mode(const char *s) {
    if(!s) return SHAPEFIX_STATIC;
    if(strcasecmp(s,"off")==0) return SHAPEFIX_OFF;
    if(strcasecmp(s,"dynamic")==0) return SHAPEFIX_DYNAMIC;
    return SHAPEFIX_STATIC;
}

/* ==== 公開関数 ==== */

/**
 * ratemap_init_from_env
 * 環境変数を読み設定を初期化。
 */
void ratemap_init_from_env(ratemap_config_t *out_cfg) {
    if(!out_cfg) return;
    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->profile = detect_profile(getenv("RMAP_PROFILE"));
    out_cfg->shapefix_mode = detect_shapefix_mode(getenv("SHAPEFIX_MODE"));
    out_cfg->shapefix_base_threshold = 11;
    {
        const char *t = getenv("SHAPEFIX_BASE_THRESHOLD");
        if(t && *t) {
            int v = atoi(t);
            if(v >= 4 && v <= 14) out_cfg->shapefix_base_threshold = v;
        }
    }
    out_cfg->verbose = env_flag("VERBOSE_RATEMAP");
    if(out_cfg->verbose) {
        fprintf(stderr,"[RATEMAP] profile=%d shapefix_mode=%d base_th=%d\n",
                (int)out_cfg->profile,
                (int)out_cfg->shapefix_mode,
                out_cfg->shapefix_base_threshold);
    }
}

/**
 * 0..15 補正マクロ (安全側)
 */
#define CLAMP4(v) ((v)<0?0:((v)>15?15:(v)))

unsigned char ratemap_map_ar(const ratemap_config_t *cfg, unsigned char raw_ar) {
    unsigned idx = raw_ar & 0x0F;
    switch(cfg->profile) {
        case RMAP_PROF_CALIBV2: return MAP_CALIBV2_AR[idx];
        case RMAP_PROF_CALIBV3: return MAP_CALIBV3_AR[idx];
        default: return MAP_SIMPLE[idx];
    }
}
unsigned char ratemap_map_dr(const ratemap_config_t *cfg, unsigned char raw_dr) {
    unsigned idx = raw_dr & 0x0F;
    switch(cfg->profile) {
        case RMAP_PROF_CALIBV2: return MAP_CALIBV2_DR[idx];
        case RMAP_PROF_CALIBV3: return MAP_CALIBV3_DR[idx];
        default: return MAP_SIMPLE[idx];
    }
}

/**
 * SHAPEFIX 動的閾値計算（簡易）。
 * gap 分布の平均/最大を参照し base_threshold を微調整。
 */
static int dynamic_threshold(const ratemap_config_t *cfg, int ar) {
    double avg = g_gap_avg;
    int mx = g_gap_max;
    int th = cfg->shapefix_base_threshold;
    if(avg > 8.0) th -= 1;
    if(mx  > 13 ) th -= 1;
    if(ar <= 1)  th -= 1;          // 低AR の場合は早めに fix
    if(th < 6) th = 6;
    return th;
}

/**
 * ratemap_apply_shapefix
 * gap = DR - AR が閾値を超えたら DR を (AR+8) 付近まで縮める。
 * 戻り値: fixしたら true
 */
bool ratemap_apply_shapefix(const ratemap_config_t *cfg, int inst_no, int is_mod,
                            int *ar, int *dr) {
    if(!cfg || !ar || !dr) return false;
    if(cfg->shapefix_mode == SHAPEFIX_OFF) return false;

    int a = CLAMP4(*ar);
    int d = CLAMP4(*dr);
    int gap = d - a;
    int th = cfg->shapefix_mode == SHAPEFIX_DYNAMIC
             ? dynamic_threshold(cfg, a)
             : cfg->shapefix_base_threshold;

    if(gap > th) {
        int target = a + 8;
        if(target > 14) target = 14;
        if(target < d) {
            if(cfg->verbose) {
                fprintf(stderr,"[SHAPEFIX] inst=%d %s AR=%d DR=%d gap=%d th=%d -> DR'=%d\n",
                        inst_no, is_mod?"Mod":"Car", a, d, gap, th, target);
            }
            *dr = target;
            return true;
        }
    } else {
        if(cfg->verbose) {
            fprintf(stderr,"[SHAPEFIX] inst=%d %s no-fix AR=%d DR=%d gap=%d th=%d\n",
                    inst_no, is_mod?"Mod":"Car", a, d, gap, th);
        }
    }
    return false;
}

/**
 * ratemap_feed_gap_stats
 * DYNAMIC 用に gap を投入
 */
void ratemap_feed_gap_stats(int gap) {
    if(gap < 0) return;
    if(gap > g_gap_max) g_gap_max = gap;
    g_gap_count++;
    // 簡易移動平均 (重み 1/gap_count)
    g_gap_avg += (gap - g_gap_avg) / (double)g_gap_count;
}

/**
 * ratemap_get_gap_stats
 */
void ratemap_get_gap_stats(double *avg_gap, int *max_gap) {
    if(avg_gap) *avg_gap = g_gap_avg;
    if(max_gap) *max_gap = g_gap_max;
}