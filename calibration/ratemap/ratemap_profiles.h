#ifndef RATEMAP_PROFILES_H
#define RATEMAP_PROFILES_H

/*
  使用法:
    - ビルド時に -DRMAP_PROFILE=RMAP_CALIBV2 など指定
    - もしくは環境変数を読んで選択 (簡易APIサンプル付き)

  ここでは raw(0..15) → mapped(0..15) の例
*/

static const unsigned char RATEMAP_SIMPLE[16] = {
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};

static const unsigned char RATEMAP_CALIBV2_AR[16] = {
  /* rawAR 0→1引上げ等 (現行状態を再構成した仮) */
  1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15
};

static const unsigned char RATEMAP_CALIBV2_DR[16] = {
  1,2,3,4,5,6,7,8,9,10,12,13,14,15,15,15
};

static const unsigned char RATEMAP_CALIBV3_AR[16] = {
  /* 改良案: より速いAttack 確保 (0,1をさらに引き上げ) */
  4,5,6,7,8,9,10,11,12,13,14,15,15,15,15,15
};

static const unsigned char RATEMAP_CALIBV3_DR[16] = {
  /* DR=0 も下限 2 に */
  2,3,4,5,6,7,8,9,10,11,12,13,14,14,15,15
};

typedef enum {
  RMAP_SIMPLE = 0,
  RMAP_CALIBV2 = 1,
  RMAP_CALIBV3 = 2
} RateMapProfile;

static inline const unsigned char* get_ar_map(RateMapProfile p) {
  switch(p) {
    case RMAP_CALIBV2: return RATEMAP_CALIBV2_AR;
    case RMAP_CALIBV3: return RATEMAP_CALIBV3_AR;
    default: return RATEMAP_SIMPLE;
  }
}
static inline const unsigned char* get_dr_map(RateMapProfile p) {
  switch(p) {
    case RMAP_CALIBV2: return RATEMAP_CALIBV2_DR;
    case RMAP_CALIBV3: return RATEMAP_CALIBV3_DR;
    default: return RATEMAP_SIMPLE;
  }
}

/* 環境変数 RMAP_PROFILE=calibv3 などを読む簡易関数 */
static inline RateMapProfile detect_profile_from_env(void) {
  const char* e = getenv("RMAP_PROFILE");
  if(!e) return RMAP_SIMPLE;
  if(strcasecmp(e,"calibv2")==0) return RMAP_CALIBV2;
  if(strcasecmp(e,"calibv3")==0) return RMAP_CALIBV3;
  return RMAP_SIMPLE;
}

#endif