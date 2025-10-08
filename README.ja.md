# eseopl3patcher

OPL2/OPLファミリー（YM3812, YM3526, Y8950）用VGMファイルをYMF262（OPL3）用VGMファイルに変換するツール

---

## 概要

eseopl3patcherは、OPL2/OPLファミリーサウンドチップ（YM3812, YM3526, Y8950）向けのVGMファイルをOPL3（YMF262）フォーマットに変換するコマンドラインツールです。OPL3の追加チャンネルなどを活かしつつ、複数の変換オプションを提供します。

---

## サポート入力チップ

- **YM3812 (OPL2)**
- **YM3526 (OPL)**
- **Y8950 (OPL, ADPCMは未サポート)**

これらのチップ向けVGMファイルを変換可能です。

---

## 主な機能

- YM3812（OPL2）、YM3526（OPL）、Y8950コマンドをOPL3（YMF262）コマンドへ変換
- OPL3追加チャンネル（ch9～ch17）へのデチューン適用
- 柔軟なチャンネルパンニング（`-ch_panning`）
- ポート0/1ごとの独立したボリューム比制御（`-vr0`, `-vr1`）
- 変換情報/オペレータ情報のGD3タグ自動付与
- デチューン最大値（detune_limit）による上限指定

---

## 入力パラメータ

- VGMファイル（YM3812/OPL2/YM3526/Y8950形式）
- デチューン値（パーセント例: `2.5` = +2.5%、`-1` = -1%）
- **デチューンリミット値（`-detune_limit <float>`、省略可、デフォルト: 制限なし）**
    - detune値の絶対値がこの値を超えないように上限を設けます。たとえば `-detune_limit 3.0` で±3.0%以内に制限されます。
    - detune値が大きすぎることによる音程の崩壊を防ぐのに有効です。
- KeyOnウェイト（整数、省略可、デフォルト: 0）
- クリエイター名（省略可、デフォルト: eseopl3patcher）
- 出力ファイル名（`-o output.vgm`、省略可）
- チャンネルパンニング（`-ch_panning 0|1`、省略可）
- ポート0/1ボリューム比（`-vr0`, `-vr1`、省略可）
- 詳細デバッグ（`-verbose`、省略可）

---

### OPLチップの明示的選択

複数のOPLファミリーが含まれる場合、デフォルトで最初に現れるチップを変換します。  
特定のチップのみ変換したい場合は下記を指定：

- `--convert-ym3812` : YM3812を変換
- `--convert-ym3526` : YM3526を変換
- `--convert-y8950`  : Y8950を変換

---

### デチューン値とdetune_limitについて

- デチューン値は正負どちらも指定可能（例: `2.5`で上昇, `-2.5`で下降）
- detune_limit（例: `-detune_limit 3.0`）を指定すると、detune値が±3.0%以内に制限されます。
    - 例: detune値を`5.0`とした場合でも、detune_limitが`3.0`なら実際には±3.0%で適用されます。
    - detune_limitを省略した場合は制限なしとなります（大きすぎる値は不自然な音になるため推奨しません）。
- 例:  
    - `2.5` … FNumberを+2.5%補正（高くなる）
    - `-2.5` … FNumberを-2.5%補正（低くなる）
    - `-detune_limit 1.5` … detune値の絶対値が1.5%を超えないよう制限

---

### チャンネルパンニング・ボリューム比・詳細モード

- `-ch_panning 0`（デフォルト）: ポート0→左, ポート1→右
- `-ch_panning 1`: 偶数/奇数チャンネル交互にL/R
- `-vr0`, `-vr1`でポートごとの音量バランスを調整可能
- `-verbose`で詳細デバッグ出力

---

## 使い方

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] [-detune_limit <float>] [-verbose] [--convert-ym3812] [--convert-ym3526] [--convert-y8950]
```

- `<input.vgm>` : 変換対象VGMファイル
- `<detune>` : デチューン値（%表記、例: `2.5`, `-1`）
- `[keyon_wait]` : KeyOnウェイト（整数、省略可、デフォルト: 0）
- `[creator]` : クリエイター名（省略可）
- `[-o output.vgm]` : 出力ファイル名（省略可）
- `[-ch_panning 0|1]` : パンニングモード
- `[-vr0 <float>]` : ポート0音量比（デフォルト: 1.0）
- `[-vr1 <float>]` : ポート1音量比（デフォルト: 0.6）
- `[-detune_limit <float>]` : detune値の絶対値の上限（省略可、デフォルト: 制限なし）
- `[-verbose]` : 詳細デバッグ
- `[--convert-ymXXXX]` : チップ選択

`[keyon_wait]`, `[creator]`, `-o`, `-ch_panning`, `-vr0`, `-vr1`, `-detune_limit`, `-verbose`, `--convert-ymXXXX`は順不同で指定可能。

**使用例:**
```sh
eseopl3patcher song.vgm -1
eseopl3patcher song.vgm 2.5 1
eseopl3patcher song.vgm -1 2 YourName
eseopl3patcher song.vgm -1 -o output.vgm
eseopl3patcher song.vgm 1.5 -ch_panning 1 -vr0 1.0 -vr1 0.5
eseopl3patcher song.vgm 2.5 --convert-ym3526
eseopl3patcher song.vgm 2.5 --convert-y8950 --convert-ym3526
eseopl3patcher song.vgm 5.0 -detune_limit 3.0
```

---

## ダウンロード

[![Download for Linux and Windows](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows用のバイナリは[リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest)からダウンロードできます。

---

## ビルド

```sh
make win
# または
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```
## 作者

[@emef2247](https://github.com/emef2247)

---

## ライセンス

MIT License
