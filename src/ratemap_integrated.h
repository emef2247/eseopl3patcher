#ifndef ESEOPL3PATCHER_RATEMAP_INTEGRATED_H
#define ESEOPL3PATCHER_RATEMAP_INTEGRATED_H

#include <stdlib.h>
#include <string.h>
#include "compat_bool.h"

/**
 * ratemap_integrated.h
 *  - AR/DR マッピングプロファイル
 *  - SHAPEFIX 動的閾値計算（オプション）
 *  - 環境変数による挙動制御
 *
 *  方針:
 *    - メイン変換ループは本ヘッダの公開 API のみ呼び出す
 *    - プロファイル: simple / calibv2 / calibv3
 *    - 環境変数:
 *        RMAP_PROFILE            (simple|calibv2|calibv3)
 *        SHAPEFIX_MODE           (off|static|dynamic)  未指定=static(従来)
 *        SHAPEFIX_BASE_THRESHOLD (整数, 既定=11)
 *        VERBOSE_RATEMAP         (1でdebug)
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RMAP_PROF_SIMPLE  = 0,
    RMAP_PROF_CALIBV2 = 1,
    RMAP_PROF_CALIBV3 = 2
} ratemap_profile_t;

typedef enum {
    SHAPEFIX_OFF = 0,
    SHAPEFIX_STATIC,
    SHAPEFIX_DYNAMIC
} shapefix_mode_t;

/* 公開設定構造体（現在値） */
typedef struct {
    ratemap_profile_t profile;
    shapefix_mode_t   shapefix_mode;
    int               shapefix_base_threshold; /* 例: 従来 11 */
    bool              verbose;
} ratemap_config_t;

/* 初期化 & 設定読み込み */
void ratemap_init_from_env(ratemap_config_t *out_cfg);

/* 生 rawAR/rawDR (0..15) をプロファイルに適用した値を返す */
unsigned char ratemap_map_ar(const ratemap_config_t *cfg, unsigned char raw_ar);
unsigned char ratemap_map_dr(const ratemap_config_t *cfg, unsigned char raw_dr);

/* SHAPEFIX:
 *  引数 ar,dr は 0..15 範囲想定。必要に応じて内部で clamp。
 *  返り値: fix 適用したか (true/false)
 */
bool ratemap_apply_shapefix(const ratemap_config_t *cfg, int inst_no, int is_mod,
                            int *ar, int *dr);

/* DYNAMIC モード用統計フィード (単純実装: gap の移動平均) */
void ratemap_feed_gap_stats(int gap);

/* 統計情報の取得（ログ用） */
void ratemap_get_gap_stats(double *avg_gap, int *max_gap);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_RATEMAP_INTEGRATED_H */