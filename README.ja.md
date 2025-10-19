# eseopl3patcher

<<<<<<< HEAD
YM2413, YM3812, YM3526, Y8950, VRC7, YMF281Bの VGMファイルを YMF262 (OPL3) VGMファイルに変換するツール
=======
OPL2/OPLファミリー（YM3812, YM3526, Y8950）用VGMファイルをYMF262（OPL3）用VGMファイルに変換するツール
>>>>>>> origin/main

---

## 概要

<<<<<<< HEAD
`eseopl3patcher` は、OPL系音源（YM2413, YM3812, YM3526, Y8950）対応のVGMファイルを、OPL3 (YMF262) フォーマットに変換するコマンドラインツールです。  
OPL3の拡張チャンネルによるコーラス（デチューン）効果や、柔軟なステレオ/ボリューム設定を特徴とします。  
YM2413/OPLL変換時は、VRC7やYMF281B音色ROMも選択可能です。

---
=======
eseopl3patcherは、OPL2/OPLファミリーサウンドチップ（YM3812, YM3526, Y8950）向けのVGMファイルをOPL3（YMF262）フォーマットに変換するコマンドラインツールです。OPL3の追加チャンネルなどを活かしつつ、複数の変換オプションを提供します。

---

## サポート入力チップ
>>>>>>> origin/main

## サポートチップ

- **YM2413  (OPLL)**
- **VRC7    (OPLL VRC7プリセット)**
- **YMF281B (OPLLP YMF281Bプリセット)**
- **YM3812 (OPL2)**
- **YM3526 (OPL)**
<<<<<<< HEAD
- **Y8950 (OPL, ADPCM部は未変換)**
- VGM 1.50以降のデータにも対応

=======
- **Y8950 (OPL, ADPCMは未サポート)**

これらのチップ向けVGMファイルを変換可能です。

>>>>>>> origin/main
---

## 主な特徴

<<<<<<< HEAD
- OPL2/OPLL系コマンドをOPL3(YMF262)コマンドへ変換
- OPL3の拡張チャンネル（ch9～17）でデチューン・コーラス効果
- パンニング・ボリューム調整 (`-ch_panning`, `-vr0`, `-vr1`)
- YM2413変換時に音色ROM（YM2413, VRC7, YMF281B）選択可能（`--preset`）
- GD3タグ自動生成（変換情報・パラメータ記録）
- 詳細デバッグモード（`-verbose`）
- 複数OPL系チップ混在時の自動判定または明示選択
- コマンドラインオプションによる柔軟な変換制御
- **YM2413とOPL3の同時演奏を可能にする `--keep_source_vgm` オプション**
=======
- YM3812（OPL2）、YM3526（OPL）、Y8950コマンドをOPL3（YMF262）コマンドへ変換
- OPL3追加チャンネル（ch9～ch17）へのデチューン適用
- 柔軟なチャンネルパンニング（`-ch_panning`）
- ポート0/1ごとの独立したボリューム比制御（`-vr0`, `-vr1`）
- 変換情報/オペレータ情報のGD3タグ自動付与
- デチューン最大値（detune_limit）による上限指定

---
>>>>>>> origin/main

---

<<<<<<< HEAD
## デチューン・補正について
=======
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
>>>>>>> origin/main

- `-detune <値>` : 全体にかかるデチューン量（パーセント指定、例: `20` → +20%）
    - 変換時は音域ごとに補正カーブが入り、高音・低音では自動的にデチューンが弱まります
    - 極端な値（例: `100`）を指定しても、実際の変化量は`detune_limit`で制限されます
- `-detune_limit <値>` : デチューン変化量の最大値（絶対値、例: `4` → ±4以内に抑制）
    - デチューンによるピッチずれが不自然にならないよう、上限で安全に制御します

<<<<<<< HEAD
---

## YM2413/OPLL プリセット選択

- `--preset <YM2413|VRC7|YMF281B>`  
    YM2413(OPLL)変換時の楽器ROMを選択できます。コナミVRC7互換やYMF281Bも指定可能。
    - `YM2413`: オリジナルOPLL音色（デフォルト）
    - `VRC7`: VRC7音色ROM
    - `YMF281B`: YMF281B互換ROM
    - 例: `--preset VRC7`
    - 他チップ変換には効果なし

---

## YM2413とOPL3の同時演奏 (`--keep_source_vgm`)

- `--keep_source_vgm`  
    YM2413入力時のみ有効なオプションです。  
    **変換後VGMの先頭に、元のYM2413コマンド（レジスタ書き込み）をそのまま残します。**
    - これにより「YM2413（MSX-MUSIC等）とOPL3（YMF262）」が同時に鳴らせるVGMデータを生成可能です
    - MSXのMSX Music + 似非OPL3-RAM、SoundCoreSLOT EX|EXG + 似非OPL3-RAM、NanoDrive7など、  
      YM2413とOPL3を同時に鳴らせるシステム環境で再生する用途を想定しています
    - 通常変換ではYM2413コマンドは削除されますが、このオプションで「両方の演奏」を実現できます

    **用途例:**
    - MSXでMSX-MUSIC（YM2413）とOPL3拡張RAMの両方に同じVGMデータを同時出力する
    - NanoDrive7やSoundCoreSLOT EX/EXG拡張カードでデュアルFM再生
    - VGMPlayer等で両方のFM音源をミックス再生したい場合

---

## 主なコマンドラインオプション

| オプション | 説明 | デフォルト |
|------------|------|------------|
| `<input.vgm>` | 入力VGMファイル | 必須 |
| `<detune>` | デチューン値（%指定、例: 20, -8） | 必須 |
| `[keyon_wait]` | KeyOn/Off待ち時間（サンプル数） | 0 |
| `[creator]` | クリエイター名（GD3タグ用） | eseopl3patcher |
| `-o <output.vgm>` | 出力ファイル名 | `<input>OPL3.vgm` |
| `-ch_panning <0|1>` | パンニングモード | 0 |
| `-vr0 <float>` | Port0ボリューム比 | 1.0 |
| `-vr1 <float>` | Port1ボリューム比 | 0.8 |
| `-detune_limit <float>` | デチューン量の上限 | 4.0 |
| `--preset <YM2413|VRC7|YMF281B>` | YM2413変換時の音色ROM | YM2413 |
| `--keep_source_vgm` | YM2413コマンドを残し、OPL3と同時演奏 | 無効 |
| `--convert-ym2413` | YM2413のみ変換 | (自動判定) |
| `--convert-ym3812` | YM3812のみ変換 | (自動判定) |
| `--convert-ym3526` | YM3526のみ変換 | (自動判定) |
| `--convert-y8950` | Y8950のみ変換 | (自動判定) |
| `-verbose` | 詳細デバッグ出力 | オフ |
=======
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
>>>>>>> origin/main

---

## 使い方

```sh
<<<<<<< HEAD
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] \
    [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] \
    [-detune_limit <float>] [--preset <YM2413|VRC7|YMF281B>] \
    [--keep_source_vgm] [--convert-ym2413] [--convert-ym3812] [--convert-ym3526] [--convert-y8950] \
    [-verbose]
```

**オプションは順不同・省略可。必須は`<input.vgm>`と`<detune>`のみ。**

---

### 使用例
=======
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
>>>>>>> origin/main

```sh
<<<<<<< HEAD
eseopl3patcher song.vgm 20
eseopl3patcher song.vgm 100 -o song_OPL3.vgm -ch_panning 1 -detune_limit 4
eseopl3patcher song.vgm 25 --preset VRC7
eseopl3patcher song.vgm 15 --convert-ym3526 --preset YMF281B
eseopl3patcher song.vgm 2.5 -vr1 0.8
eseopl3patcher song.vgm -8 -ch_panning 1 -verbose
eseopl3patcher song.vgm 30 --keep_source_vgm
=======
eseopl3patcher song.vgm -1
eseopl3patcher song.vgm 2.5 1
eseopl3patcher song.vgm -1 2 YourName
eseopl3patcher song.vgm -1 -o output.vgm
eseopl3patcher song.vgm 1.5 -ch_panning 1 -vr0 1.0 -vr1 0.5
eseopl3patcher song.vgm 2.5 --convert-ym3526
eseopl3patcher song.vgm 2.5 --convert-y8950 --convert-ym3526
eseopl3patcher song.vgm 5.0 -detune_limit 3.0
>>>>>>> origin/main
```

---

## ダウンロード

<<<<<<< HEAD
[![Download latest release](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows用バイナリは [最新リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest) からダウンロードしてください。
=======
[![Download for Linux and Windows](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows用のバイナリは[リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest)からダウンロードできます。

---
>>>>>>> origin/main

---

## ビルド方法

```sh
make win
# または
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```
<<<<<<< HEAD

---

## VGMツールの取得と連携

テスト・解析用に、`vgm2txt`・`VGMPlay` など公式VGMツールを `tools/` 以下へ自動取得・ビルド可能です。

```bash
bash tools/fetch_vgm_tools.sh
```

詳細は `tools/` ディレクトリのREADMEを参照。

---

## ライセンス

MIT License  
各外部ツールはそれぞれのライセンスに従ってください。

---

## 作者

[@emef2247](https://github.com/emef2247)
=======
## 作者

[@emef2247](https://github.com/emef2247)

---

## ライセンス

MIT License
>>>>>>> origin/main
