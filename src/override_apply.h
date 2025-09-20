#ifndef ESEOPL3PATCHER_OVERRIDE_APPLY_H
#define ESEOPL3PATCHER_OVERRIDE_APPLY_H

/**
 * override_apply.h
 * Variant 単位の TL / FB 差分適用 API。
 *  - JSON ロード自体は override_loader に委譲
 *  - ここは保持 + 検索 + 適用のみ責務
 *
 * 使用手順:
 *   1. override_init()
 *   2. override_loader_load_json(path) でテーブル投入
 *   3. 変換中に override_apply_fb / override_apply_tl を呼ぶ
 */

#include "compat_bool.h"

#ifdef __cplusplus
extern "C" {
#endif

int  override_init(void);
void override_reset(void);
int  override_add(const char *variant, int mod_tl_delta, int car_tl_delta, int fb_delta);

/* 変換処理で使用 (FB は演算前, TL は最終直前) */
int  override_apply_fb(const char *variant, int fb_value);
int  override_apply_tl(const char *variant, int tl_value, int is_modulator);

/* デバッグ用ダンプ (stderr) */
void override_dump_table(void);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_OVERRIDE_APPLY_H */