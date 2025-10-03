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
float       ymfm_step_and_measure(ymfm_ctx_t* ctx, uint32_t n_samples);
float       ymfm_step_and_measure_db(ymfm_ctx_t* ctx, uint32_t n_samples);
uint32_t    ymfm_get_last_nonzero(const ymfm_ctx_t* ctx);

// EG内部（分析ビルドで有効）
// ch: 0..8, op_index: 0=mod, 1=car
// 戻り値: phase は 0..5（enum envelope_state 準拠）。未提供時は -1。
//         att は 0..1023（小さいほど大）。未提供時は -1。
int         ymfm_get_op_env_phase(ymfm_ctx_t* ctx, int ch, int op_index);
int         ymfm_get_op_env_att(ymfm_ctx_t* ctx, int ch, int op_index);

// 追加のキャッシュ値（YMFM内部の有効値）
// total_level_x8: KSL合成後TL（×8スケール）
// multiple_x2   : multiple の x.1 値を整数化（×2）
// eg_rate       : KSR適用後のrate（state: 0..5 = DP/ATK/DEC/SUS/REL/REV）
// eg_sustain_x32: SLの内部単位（<<5 済み）
// block_freq    : cache の block_freq
int         ymfm_get_op_cache_total_level_x8(ymfm_ctx_t* ctx, int ch, int op_index);
int         ymfm_get_op_cache_multiple_x2(ymfm_ctx_t* ctx, int ch, int op_index);
int         ymfm_get_op_cache_eg_rate(ymfm_ctx_t* ctx, int ch, int op_index, int eg_state);
int         ymfm_get_op_cache_eg_sustain_x32(ymfm_ctx_t* ctx, int ch, int op_index);
int         ymfm_get_op_cache_block_freq(ymfm_ctx_t* ctx, int ch, int op_index);

// デバッグ出力
void        ymfm_debug_print(const ymfm_ctx_t* ctx, const char* tag);

#ifdef __cplusplus
}
#endif

#endif // ESEOPL3PATCHER_YMFM_C_API_H