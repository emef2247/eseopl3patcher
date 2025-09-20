# Calibration パイプライン手順 (pass1 / pass2)

本ディレクトリは OPLL→OPL3 変換の RateMap / Harmonic バランス / SHAPEFIX 調整を行うための
“校正専用” ワークスペースです。  
本体の通常利用 (ユーザ向け変換) とは分離して運用します。

## 1. 前提

- ルートにビルド済みパッチャーバイナリ  
  - `./eseopl3patcher_m1s0t0` など (variants)
- `VGMPlay.ini` で `LogSound = 2` を有効化 → `vgmplay` 実行時自動 WAV 出力
- Python 3 (分析スクリプト用)
- (オプション) 環境変数で RateMap / SHAPEFIX モードを切替

## 2. ディレクトリ構成 (抜粋)

```
calibration/
  Makefile
  config.yaml
  scripts/
    calib_pass1.sh
    calib_pass2.sh
    harmonic_balance.py
    analyze_wav.py
    summarize_enhanced.py
  overrides/
    overrides_pass2.json (自動生成)
  reports/
    report_XX_pass1.json
    summarize_pass1.csv
```

## 3. 環境変数 (主なもの)

| 変数 | 値例 | 説明 |
|------|------|------|
| RMAP_PROFILE | simple / calibv2 / calibv3 | RateMap プロファイル選択 |
| SHAPEFIX_MODE | off / static / dynamic | エンベロープ DR 調整方式 |
| SHAPEFIX_BASE_THRESHOLD | 11 (既定) | static / dynamic の基準 gap 閾値 |
| VERBOSE_RATEMAP | 1 | 詳細ログ (stderr) |

例:
```bash
export RMAP_PROFILE=calibv3
export SHAPEFIX_MODE=dynamic
export VERBOSE_RATEMAP=1
```

## 4. PASS1 実行

初回変換 + 分析:
```bash
cd calibration
RMAP_PROFILE=calibv3 make pass1
```

生成物:
- `reports/report_<base>_pass1.json` : 個別分析 (attack90_ms, rel_h2 等)
- `reports/summarize_pass1.csv` : 集計 CSV
- `reports/warn_pass1.txt` : 閾値超過警告

## 5. Harmonic バランス (オーバーライド生成)

PASS1 の結果から 2倍音 (rel_h2) の過不足を調整する TL / FB delta を計算:
```bash
make balance
```
結果: `overrides/overrides_pass2.json` (例)
```json
{
  "patch_overrides": {
    "m1s0t0": { "mod_tl_delta": 7, "car_tl_delta": -2, "fb_delta": -1 }
  },
  "meta": { "source": "harmonic_balance.py" }
}
```

## 6. PASS2 実行 (オーバーライド適用再変換)

```bash
make pass2
```

- PASS2 は内部で `--override overrides/overrides_pass2.json` (将来 patcher 実装) を想定
- 集計結果: `summarize_pass2.csv` / `warn_pass2.txt`

## 7. rel_h2 の確認・受容基準

| 指標 | 目標レンジ (暫定) |
|------|------------------|
| rel_h2 | -6dB ～ +6dB |
| attack90_ms | 120ms 以下推奨 |

PASS1→PASS2 で rel_h2 の極端な偏り (例: +18dB → +5dB) が改善されているかを確認。

## 8. 代表的なトラブルシュート

| 症状 | 原因候補 | 対処 |
|------|----------|------|
| attack90 が 300ms 付近で一定 | AR 下限押上不足 / DR=0 多発 | calibv3 プロファイル使用 / SHAPEFIX dynamic |
| rel_h2 が 15dB 超 | Mod 過強調 / FB 高 | PASS2 オーバーライド / FB delta |
| RMS200 ≪ RMS500 | 立ち上がり遅延エンベロープ | AR 再マップ (calibv3) / DR 下限強化 |

## 9. duplicate_write_opl3 との関係

- TL / FB 値の“差分補正” は `duplicate_write_opl3()` 呼び出し直前で完了させる
- `duplicate_write_opl3()` 内ではステレオ/コーラス処理のみを行い、音色アルゴリズムは変更しない

## 10. 典型的ワークフローまとめ

```bash
# 1. PASS1
RMAP_PROFILE=calibv3 SHAPEFIX_MODE=dynamic make pass1

# 2. オーバーライド生成
make balance

# 3. PASS2
RMAP_PROFILE=calibv3 SHAPEFIX_MODE=dynamic make pass2

# 4. 差分確認
diff -u reports/summarize_pass1.csv reports/summarize_pass2.csv
```

## 11. よくある質問

Q. overrides_pass2.json をコミットすべき?  
A. 調整履歴を追いたいならコミット推奨。巨大化する場合はタグ/リリースのみでも可。

Q. JSON パース失敗時は?  
A. 警告を表示し PASS2 でも補正無しで継続。`stderr` を確認。

---

(更新日: YYYY-MM-DD)