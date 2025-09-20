#ifndef RATEMAP_PROFILES_H
#define RATEMAP_PROFILES_H

#include <stdlib.h>
#include <string.h>

/** RateMapProfile
 *  SIMPLE: 元の素直なマッピング
 *  CALIBV2: 既存調整 (0押し上げなど)
 *  CALIBV3: Attack 改善強化（raw0/1 をさらに高く）
 */
typedef enum {
    RMAP_SIMPLE = 0,
    RMAP_CALIBV2 = 1,
    RMAP_CALIBV3 = 2
} RateMapProfile;

// 各 0..15 → 0..15
static const unsigned char RATEMAP_SIMPLE[16] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const unsigned char RATEMAP_CALIBV2_AR[16] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15
};
static const unsigned char RATEMAP_CALIBV2_DR[16] = {
    1,2,3,4,5,6,7,8,9,10,12,13,14,15,15,15
};
static const unsigned char RATEMAP_CALIBV3_AR[16] = {
    4,5,6,7,8,9,10,11,12,13,14,15,15,15,15,15
};
static const unsigned char RATEMAP_CALIBV3_DR[16] = {
    2,3,4,5,6,7,8,9,10,11,12,13,14,14,15,15
};

/** detect_profile_from_env
 *  環境変数 RMAP_PROFILE を見てプロファイル選択
 *  値: simple / calibv2 / calibv3 （大文字小文字無視）
 */
static inline RateMapProfile detect_profile_from_env(void) {
    const char *e = getenv("RMAP_PROFILE");
    if (!e) return RMAP_SIMPLE;
    if (strcasecmp(e, "calibv2") == 0) return RMAP_CALIBV2;
    if (strcasecmp(e, "calibv3") == 0) return RMAP_CALIBV3;
    return RMAP_SIMPLE;
}

/** get_ar_map
 *  Attack Rate 用テーブルを返す
 */
static inline const unsigned char* get_ar_map(RateMapProfile p) {
    switch (p) {
        case RMAP_CALIBV2: return RATEMAP_CALIBV2_AR;
        case RMAP_CALIBV3: return RATEMAP_CALIBV3_AR;
        default: return RATEMAP_SIMPLE;
    }
}

/** get_dr_map
 *  Decay Rate 用テーブルを返す
 */
static inline const unsigned char* get_dr_map(RateMapProfile p) {
    switch (p) {
        case RMAP_CALIBV2: return RATEMAP_CALIBV2_DR;
        case RMAP_CALIBV3: return RATEMAP_CALIBV3_DR;
        default: return RATEMAP_SIMPLE;
    }
}

#endif /* RATEMAP_PROFILES_H */