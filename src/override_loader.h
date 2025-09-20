#ifndef ESEOPL3PATCHER_OVERRIDE_LOADER_H
#define ESEOPL3PATCHER_OVERRIDE_LOADER_H

/**
 * override_loader.h
 * overrides_pass2.json のような軽量 JSON をパースし override_apply に登録。
 *
 * 制限:
 *  - ネストは "patch_overrides" 直下の { variant: { ... } } のみ対応
 *  - 文字列キーと int 値 (mod_tl_delta, car_tl_delta, fb_delta) のみ抽出
 *  - 空白/改行/タブは自由
 *  - 数値は -?[0-9]+
 *  - 余計なキーがあっても無視
 *  - コメント不可
 */

#ifdef __cplusplus
extern "C" {
#endif

/* 成功 0, 失敗 <0 */
int override_loader_load_json(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_OVERRIDE_LOADER_H */