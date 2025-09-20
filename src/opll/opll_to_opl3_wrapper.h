#ifndef OPLL_TO_OPL3_WRAPPER_H
#define OPLL_TO_OPL3_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>

#include "../vgm/vgm_helpers.h"
#include "../opl3/opl3_voice.h"
#include "../opl3/opl3_event.h"

/**
 * @file opll_to_opl3_wrapper.h
 * @brief YM2413 (OPLL) → OPL3 変換ラッパ (レジスタストリーミング変換エントリポイント)
 *
 * 設計メモ:
 *  - YM2413 の 0x10..0x38 (1n/2n/3n) 書き込みシーケンスを観察し、
 *    KeyOn エッジ近傍を一時保留して OPL3 側の適切なレジスタ書き込み順序 (A→40→B 等) を保証。
 *  - Attack/Decay Rate マッピングや最小 AR クランプ等はマクロで有効化。
 *  - Rhythm (inst 16..20) にも対応 (データ ROM を参照)。
 *  - デバッグ出力はマクロ (OPLL_DEBUG_*) により抑制/表示を切替可能。
 */

/**
 * @brief YM2413 パッチを全て OPL3VoiceDB に登録 (ROM ベース)
 * @param db 追加先データベース
 */
void register_all_ym2413_patches_to_opl3_voice_db(OPL3VoiceDB *db);

/**
 * @brief OPLL → OPL3 レジスタ変換エントリポイント
 *
 * @param p_music_data   出力 VGM バッファ
 * @param p_vgm_context  VGM 進行状態
 * @param p_state        OPL3 出力状態
 * @param reg            書き込まれた OPLL アドレス
 * @param val            書き込まれた値
 * @param next_wait_samples 次コマンドまでの待ちサンプル (0=直後)
 * @param opts           追加オプション
 * @return 追加で書き込まれた OPL3 バイト数 (現状 0 / 将来拡張用)
 *
 * 注意:
 *  - キャンセル条件や遅延書き込みは内部で処理。
 *  - KeyOn トランジション時、音色や FNUM 情報が揃うまで最大 KEYON_WAIT_FOR_INST_TIMEOUT_SAMPLES 待機。
 */
int opll_write_register(
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    uint16_t next_wait_samples,
    const CommandOptions *opts
);

/**
 * @brief 直近に観測した 1n/2n/3n のスタンプ (最終出力済み値)
 */
typedef struct {
    uint8_t last_1n; bool valid_1n;
    uint8_t last_2n; bool valid_2n;
    uint8_t last_3n; bool valid_3n;
    bool    ko;             // 最終 2n の KO(bit4)
} OpllStampCh;

/**
 * @brief これから出力しうる保留中レジスタ。flush 時にまとめて出力。
 */
typedef struct {
    bool has_1n; uint8_t reg1n;
    bool has_2n; uint8_t reg2n;
    bool has_3n; uint8_t reg3n;
} OpllPendingCh;

/**
 * @brief 2n (KeyOn/Off) トランジション判定結果
 */
typedef struct {
    bool has_2n;
    bool note_on_edge;   // ko:0→1
    bool note_off_edge;  // ko:1→0
    bool ko_next;
} PendingEdgeInfo;

/**
 * @brief KO ビットを強制的に落とす (KeyOff 用)
 */
static inline uint8_t opll_make_keyoff(uint8_t val) {
    return (uint8_t)(val & ~(1u << 4));
}

#define YM2413_NUM_CH 9

// グローバル保留・スタンプ (変換器内部状態)
extern OpllPendingCh g_pend[YM2413_NUM_CH];
extern OpllStampCh   g_stamp[YM2413_NUM_CH];


/**
 * Initialize OPL3 chip and voice database.
 */
void opll_init(OPL3State *p_state) ;

#endif /* OPLL_TO_OPL3_WRAPPER_H */