#ifndef ESEOPL3PATCHER_YMFM_C_API_H
#define ESEOPL3PATCHER_YMFM_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

// 不透明ハンドル
typedef struct ymfm_ctx ymfm_ctx_t;

// 作成/破棄（OPLL/YM2413）
ymfm_ctx_t* ymfm_opll_create(uint32_t clock_hz, uint32_t sample_rate);
void        ymfm_destroy(ymfm_ctx_t* ctx);

// レジスタ書き込み（OPLLはアドレス/データポート方式）
void        ymfm_opll_write(ymfm_ctx_t* ctx, uint32_t addr, uint8_t data);

// Nサンプル進めて、簡単な応答メトリクスを返す
// 戻り値: 平均絶対振幅（0..1目安、内部正規化係数は実装依存）
float       ymfm_step_and_measure(ymfm_ctx_t* ctx, uint32_t n_samples);

// 追加: Nサンプル進めて、RMS→dBFSを返す（0dBFSがフルスケール、無音は -inf に近い値）
// 戻り値: RMS[dBFS]（例: -60.0f など）
float       ymfm_step_and_measure_db(ymfm_ctx_t* ctx, uint32_t n_samples);

// 追加: 直近測定の非ゼロサンプル数（0なら完全無音だったことを示す）
uint32_t    ymfm_get_last_nonzero(const ymfm_ctx_t* ctx);

// デバッグ出力（直近の指標などをSTDOUTへ）
void        ymfm_debug_print(const ymfm_ctx_t* ctx, const char* tag);

#ifdef __cplusplus
}
#endif

#endif // ESEOPL3PATCHER_YMFM_C_API_H