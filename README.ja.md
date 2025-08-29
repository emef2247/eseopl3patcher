# eseopl3patcher

YM3812 (OPL2) VGMファイルをYMF262 (OPL3) VGMファイルに変換するツール

## 概要

eseopl3patcherは、YM3812 (OPL2) 用に作成されたVGMファイルをOPL3 (YMF262) 形式に変換するコマンドラインツールです。OPL3の拡張チャンネルを活用し、デチューンした音声を追加することでコーラス効果を付与します。さらに、柔軟なステレオ出力や音量バランスの調整が可能です。

## 特徴

- YM3812 (OPL2) コマンドをOPL3 (YMF262) コマンドへ変換
- OPL3の拡張チャンネル（ch9～17）にデチューンを適用
- チャンネルパンモード（`-ch_panning`）でチャンネルごとのL/R振り分けが可能
- ポートごとに独立した音量比率（`-vr0`, `-vr1`）を指定可能
- 変換情報や操作者情報を自動的にGD3タグへ追記
  
## 入力パラメータ

- VGMファイル（YM3812/OPL2形式）
- デチューン値（パーセント、例: `2.5` は+2.5%、`-1` は-1%デチューン）
    - デチューン値はパーセントで指定してください。例: `2.5` = 2.5%、`-1` = -1%。
- KeyOnウェイト（整数、例: `1`）。省略可（デフォルト: `0`）
- 作成者名（ここで指定した文字列がGD3 Creator欄に追加されます）。省略可（デフォルト: `eseopl3patcher`）
- 出力ファイル名（`-o output.vgm`）。省略可（デフォルト: `<input>OPL3.vgm`）
- チャンネルパンモード（`-ch_panning 0|1`）。省略可（デフォルト: `0`）
    - `0`：ポート0（ch0–ch8）はL、ポート1（ch9–ch17）はR出力
    - `1`：偶数チャンネル・奇数チャンネルを交互にL/Rに振り分けて出力（ステレオ感UP）
- ポート0の音量比率（`-vr0 <float>`）。省略可（デフォルト: `1.0`）
    - ポート0の音量比率を指定（1.0 = 100%、0.6 = 60%など）
- ポート1の音量比率（`-vr1 <float>`）。省略可（デフォルト: `0.6`）
    - ポート1の音量比率を指定（1.0 = 100%、0.6 = 60%など）

### デチューン値（±指定）について

- デチューン値は正負どちらでも指定可能です。
    - 正の値（例: `2.5`）はデチューン音のピッチを上げます。
    - 負の値（例: `-2.5`）はデチューン音のピッチを下げます。
- 例:
    - `2.5` … FNumberに+2.5%加算（高くなる）
    - `-2.5` … FNumberから-2.5%減算（低くなる）

> **デチューン値の符号によって、変換後のピッチが上がるか下がるかが決まります。用途に応じて指定してください。**

### チャンネルパンモード・音量バランスについて

- `-ch_panning 0`（デフォルト）：ポート0はL、ポート1はR出力。デチューン音（ポート1）がRだけから出るため、効果を明確に確認できます。
- `-ch_panning 1`：偶数・奇数チャンネルを交互にL/Rに振り分けてステレオ効果を得られます。
- `-vr0` および `-vr1` でポートごとの音量バランスも自由に調整できます。
    - 例：デチューン音（ポート1）を目立たせたくない場合は `-vr1 0.6` などを指定。
    - コーラス効果を明確にしたい場合は `-ch_panning 0` にして `-vr1` の値を調整してください。

## 使い方

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>]
```

- `<input.vgm>` : 変換対象のVGMファイル（YM3812/OPL2形式）
- `<detune>` : デチューン値（パーセント、例: `2.5`は+2.5%、`-1`は-1%）
- `[keyon_wait]` : KeyOnウェイト（整数、省略可、デフォルト: 0）
- `[creator]` : 作成者名（省略可、デフォルト: "eseopl3patcher"）
- `[-o output.vgm]` : 出力ファイル名（省略可、省略時は自動生成）
- `[-ch_panning 0|1]` : チャンネルパンモード　0:OFF 1:ON（省略可、デフォルト: 0）
- `[-vr0 <float>]` : ポート0音量比率 1.0以下の小数（1.0で100%)（省略可、デフォルト: 1.0）
- `[-vr1 <float>]` : ポート1音量比率 1.0以下の小数（1.0で100%)（省略可、デフォルト: 0.6）

**引数の順序は柔軟です。**  
`[keyon_wait]`、`[creator]`、`-o output.vgm`、`-ch_panning`、`-vr0`、`-vr1` は `<input.vgm>` と `<detune>` の後ならどの順でも指定可能です。

**使用例:**
```sh
eseopl3patcher song.vgm -1
eseopl3patcher song.vgm 2.5 1
eseopl3patcher song.vgm -1 2 YourName
eseopl3patcher song.vgm -1 YourName
eseopl3patcher song.vgm -1 -o output.vgm
eseopl3patcher song.vgm 1.5 0 YourName -o output.vgm
eseopl3patcher song.vgm -1 -o output.vgm YourName
eseopl3patcher song.vgm 1.5 -ch_panning 1 -vr0 1.0 -vr1 0.5
eseopl3patcher song.vgm 2.5 -vr1 0.8
```

英語版READMEは[こちら](https://github.com/emef2247/eseopl3patcher/blob/main/README.md#usage) をご覧ください。

## ダウンロード

[![Download for Linux and Windows](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

**Linux** および **Windows** 用のビルド済みバイナリは [最新リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest) からダウンロードできます。

## ビルド

```sh
make win
# または
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```

## 作者

[@emef2247](https://github.com/emef2247) 

## ライセンス

MIT License
