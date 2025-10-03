# OPLL(YM2413) → OPL3(YMF262) パラメータ変換ガイド

本ドキュメントは、YMFM ベースの内部動作を前提に、OPLL(YM2413) の音色・EG・周波数パラメータを OPL3 へ写像する際の考え方と実装指針をまとめたものです。開発・解析向けの情報であり、最終ユーザ向け UI/機能からは露出しません。

- 対象ソース: YMFM (third_party/ymfm)
- 解析支援: YMFM_ANALYSIS アクセス（ビルド前パッチ）＋ CSV トレース
- 用語: 「モジュレータ(op0)」「キャリア(op1)」は2オペの慣例に従う

---

## 1. 概要

- 目的
  - YM2413 のノート・音色パラメータを OPL3 レジスタに落とし込み、元音色に近い応答（アタック、減衰、サステイン、リリース、音量/倍音バランス）を得る。
- アプローチ
  - YMFMコアの「実際に使われる有効値」（KSL 加算後 TL、KSR 適用後のレート等）を解析用 CSV に出力。
  - それを OPL3 側のレジスタ語彙にマップし、必要に応じて±1ステップ程度の微調整で差を吸収。

---

## 2. YM2413（OPLL）内部の要点（YMFM準拠）

- 周波数/キーコード
  - ch_block_freq = B(ブロック3bit) + FNUM(下位ビット)
  - キーコードは block_freq 上位ビットから算出（OPLL: 上位4bit）
  - detune は無し（OPLL は detune=0）
  - phase_step は OPL 方程式（OPLL は block_freq を1bit増幅して使用）

- オペレータ基本量
  - multiple: 0..15 を OPL規定の丸め（0 → 0.5、11 → 10 など）で x.1 倍化
  - total_level:
    - 非リズム ch の op0（モジュレータ）は「楽器データの TL」
    - それ以外（キャリア/リズム等）は「音量 nibble × 4」
    - 最後に <<3（×8）して内部固定小数スケールへ
  - KSL: テーブル opl_key_scale_atten(block, fnumMSB) を ksl シフトで total_level に事前加算

- EG（ADSR）
  - sustain level(SL): 4bit（15→31 に拡張）後 <<5（内部単位）
  - レート: OPLL 固有の係数（DP=12*4, RR=7*4, RS=5*4）をベースに、KSR で keycode に応じ増速
  - EG type（op_eg_sustain）: パーカッシブ/サステイン動作切替（サステインOFFで減衰→リリース系の固定係数利用）

- フィードバック/アルゴリズム
  - ch_feedback: 3bit
  - アルゴリズムは2オペ想定で「モジュレータ→キャリア」が基本

- 波形
  - waveform[0]=サイン、waveform[1]=負半周期の整流（rectified）
  - 楽器データの Rectified ビットで per-op 選択

---

## 3. OPLL → OPL3 パラメータ対応（推奨マッピング）

下記は OPL3（2オペ・標準接続）を前提とした対応です。

- 周波数（A0/B0/C0 系）
  - FNUM/ブロックはそのまま対応（±セント差は後述の微調整で）
  - LFO PM が絡む場合は OPL3 側の PM 深度を OPLL 相当へ固定

- multiple（0..15）
  - 同一丸め規則のため、値をそのまま OPL3 multiple へ

- KSR（0/1）
  - 同義なのでそのまま

- KSL（0..3）
  - 値そのまま OPL3 の KSL へ
  - OPLL では TL 合成前に KSL を加算するが、OPL3 も per-op KSL を持つため等価に扱える

- TL（0..63）
  - モジュレータ: OPLL 楽器データの TL(0..63) を OPL3 TL にそのまま
  - キャリア: OPLL は「音量 nibble(0..15)」を「4×」して TL に寄与
    - 変換例: TL_car_OPL3 ≈ round((volume_nibble / 15.0) × 63)
  - 実際の出力差は KSL やミキシング比で変動するため、後述の微調整を推奨

- EG（AR/DR/SL/RR/EGT）
  - AR/DR/SL/RR: 楽器データの nibble（0..15）を OPL3 の各フィールドへマップ
  - EG Type（サステイン/パーカッシブ）: OPLL op_eg_sustain を OPL3 の EGT へ
  - 体感整合のコツ:
    - OPLL 固有の RR/RS 挙動（常数利用）によりリリース末尾の見え方が変わる
    - OPL3 側 RR±1、SL±1、キャリアTL±1 で追い込むと整合しやすい

- フィードバック
  - ch_feedback(0..7) をそのまま OPL3 フィードバックへ

- 波形
  - OPLL Rectified → OPL3 波形1（half-sine）を推奨
  - サイン → OPL3 波形0

- LFO（AM/PM）
  - per-op AM/VIB enable は OPL3 に同等ビットがあるためそのまま
  - 深度は OPLL 固定、OPL3 は可変（原曲追従なら OPL3 を OPLL 相当に固定）

---

## 4. 解析用 CSV と活用方法

- 出力有効化（例）
  ```bash
  ESEOPL3_YMFM_TRACE=1 \
  ESEOPL3_YMFM_TRACE_MIN_WAIT=2048 \
  ESEOPL3_YMFM_TRACE_CSV=ymfm_trace.csv \
  make USE_YMFM=1 USER_DEFINES="-DYMFM_ANALYSIS" -j
  ```

- 列の例（主要）
  - mean_abs, rms_db, nz: 実音出力の窓統計（RMS は dBFS 近似、無音→ -240.00）
  - phase_mod/phase_car: EG ステート（0..5=DP/ATK/DEC/SUS/REL/REV、OPLLは DP あり）
  - att_mod/att_car: 生のエンベロープ減衰（0..1023、0が最大）
  - att_mod_db/att_car_db: 近似 dB（簡易換算: −att_raw×96/1023）

- 推奨の見方
  - リリース末尾の減衰傾き（att_*_db 対 時間）を OPL3 側の出力と重ね、RR/SL/TL を微調整
  - サステイン到達点（att_* の平衡値）から SL/TL の見直し
  - モジュレータの効き具合（キャリア出力の倍音構成やレベル）に対して、mod TL/KSL/FB を調整

---

## 5. 変換アルゴリズム（初期版の手順）

1) 周波数設定
- OPLL の block/fnum を OPL3 の A0/B0/C0 群へ同値設定
- LFO PM の深度は OPLL 相当に固定（必要に応じ）

2) オペレータ単位のマッピング
- multiple, KSR, KSL: 同値設定
- TL:
  - mod: OPLL 楽器 TL → OPL3 TL
  - car: TL_car_OPL3 ≈ round((vol_nibble / 15) × 63)
- EG:
  - AR/DR/SL/RR/EGT: nibble 単位で同値
- feedback: ch_feedback を同値

3) 波形
- Rectified → 波形1、Sine → 波形0

4) リズムの扱い
- OPLL リズム（ch>=6）は OPL3 の専用マッピングとアルゴリズムが異なるため、個別対応が必要
  - 初期は非リズム曲/パートで運用、必要に応じ拡張

5) 微調整（誤差最小化）
- 比較対象: CSV の att_car_db/att_mod_db の傾き・平衡点と、OPL3 側実音の RMS 変化
- 推奨探索:
  - RR_car ∈ {base-1, base, base+1}
  - SL_car ∈ {base-1, base, base+1}
  - TL_car ∈ {base-1, base, base+1}
- 誤差関数: 時間窓ごとの dB 差の L1/L2 など

---

## 6. 厳密 dB への拡張（後日差し替え）

- 近似式: att_db ≈ −att_raw × (96/1023)
- 厳密化: YMFM の `attenuation_to_volume`（5.8対数→線形→dB）と同等処理で算出
  - OPLL の env は 4.6→4.8→5.8 相当に調整した上でテーブル変換
  - 必要時にブリッジ側へテーブル複製（研究目的）

---

## 7. YMFM_ANALYSIS（解析アクセサ）について

- 既存アクセサ（導入済み）
  - `analysis_get_op_env_phase(ch, op)` → EG ステート（int）
  - `analysis_get_op_env_att(ch, op)` → 生アッテネーション（0..1023）

- 追加提案（必要に応じて導入）
  - `analysis_get_op_cache_total_level(ch, op)` → KSL 合成後 TL（×8 スケール）
  - `analysis_get_op_cache_rates(ch, op)` → KSR 適用後レート（EG_ATTACK/DECAY/SUSTAIN/RELEASE/DEPRESS）
  - `analysis_get_op_cache_multiple(ch, op)` → multiple の x.1 値
  - これらを CSV に出せば、OPL3 で「書くべき値」の逆算が容易になる

---

## 8. 既知の差異と補正の勘所

- リリース末尾
  - OPLL は RS/RR の固定係数分で末尾の減衰が速/遅に見えることがある
  - OPL3 側 RR±1 で揃いやすい

- KSL・音量
  - 実周波数（ブロック/鍵）に依存した KSL により、音域でのレベル差が発生
  - OPL3 の KSL を同値にしても DAC スケール差で見かけが揃わない場合があるため、TL_car を±1でならす

- 波形
  - OPLL の rectified は OPL3 波形1 で近似可。ただし内部整流の位相/符号の癖で微差が乗る場合あり

- LFO
  - 深度/波形/周波数の違いで微妙な揺れ方が異なる。必要に応じ OPL3 の LFO 深さを固定し、楽器側 AM/VIB を最小限に

---

## 9. 参考コマンド・環境

- 解析 CSV 生成（例）
  ```bash
  ESEOPL3_YMFM_TRACE=1 \
  ESEOPL3_YMFM_TRACE_MIN_WAIT=2048 \
  ESEOPL3_YMFM_TRACE_CSV=ymfm_trace.csv \
  ./build/bin/eseopl3patcher tests/equiv/inputs/ym2413_scale_chromatic.vgm 0
  ```

- YMFM 解析パッチ適用
  ```bash
  bash scripts/patch_ymfm_analysis.sh
  make USE_YMFM=1 USER_DEFINES="-DYMFM_ANALYSIS" -j
  ```

- CSV 列（抜粋）
  - `phase_mod, att_mod, att_mod_db, phase_car, att_car, att_car_db`
  - `mean_abs, rms_db, nz, wait_samples, t_ms, event, reg2n_hex`

---

## 10. 今後の拡張

- cache 値（有効 TL・KSR 適用後レート・multiple 等）を CSV に追加
- OPLL→OPL3 自動マッピング/微調整ツール（CSV→OPL3 レジスタセット生成）
- リズムパートの専用対応
- 厳密 dB 変換の導入

---

更新履歴
- 2025-10-03: 初版（解析CSV＋基本マッピングの方針を記載）