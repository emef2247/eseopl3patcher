# eseopl3patcher

YM2413, YM3812, YM3526, Y8950, VRC7, YMF281B, YM2423 の VGMファイルを YMF262 (OPL3) VGMファイルに変換するツール

---

## 概要

`eseopl3patcher` は、OPL系音源（YM2413, YM3812, YM3526, Y8950）対応のVGMファイルを、OPL3 (YMF262) フォーマットに変換するコマンドラインツールです。  
OPL3の拡張チャンネルによるコーラス（デチューン）効果や、柔軟なステレオ/ボリューム設定を特徴とします。  
YM2413/OPLL変換時は、VRC7やYMF281B, YM2423の音色プリセットも選択可能です。

---

## サポートチップ

- **YM2413  (OPLL)**
- **VRC7    (OPLL VRC7プリセット)**
- **YMF281B (OPLLP YMF281Bプリセット)**
- **YM2423   (OPLL-X) [v2.1.0より追加]**
- **YM3812 (OPL2)**
- **YM3526 (OPL)**
- **Y8950 (OPL, ADPCM部は未変換)**
- VGM 1.50以降のデータにも対応

---

## 主な特徴

- OPL2/OPLL系コマンドをOPL3(YMF262)コマンドへ変換
- OPL3の拡張チャンネル（ch9～17）でデチューン・コーラス効果
- パンニング・ボリューム調整 (`-ch_panning`, `-vr0`, `-vr1`)
- YM2413変換時に音色ROM（YM2413, VRC7, YMF281B, YM2423）選択可能（`--preset`）【v2.1.0より】
- GD3タグ自動生成（変換情報・パラメータ記録）
- 詳細デバッグモード（`-verbose`）
- 複数OPL系チップ混在時の自動判定または明示選択
- コマンドラインオプションによる柔軟な変換制御
- **YM2413とOPL3の同時演奏を可能にする `--keep_source_vgm` オプション**

---

## デチューン・補正について

- `-detune <値>` : 全体にかかるデチューン量（パーセント指定、例: `20` → +20%）
    - 変換時は音域ごとに補正カーブが入り、高音・低音では自動的にデチューンが弱まります
    - 極端な値（例: `100`）を指定しても、実際の変化量は`detune_limit`で制限されます
- `-detune_limit <値>` : デチューン変化量の最大値（絶対値、例: `4` → ±4以内に抑制）
    - デチューンによるピッチずれが不自然にならないよう、上限で安全に制御します

---

## YM2413/OPLL プリセット＆音色ソース選択【v2.1.0より拡充】

- `--preset <YM2413|VRC7|YMF281B|YM2423>`
    - YM2413(OPLL)変換時のプリセットを選択できます。
    - `YM2413`: OPLL互換音色プリセット（デフォルト）
    - `VRC7`: VRC7互換音色プリセット
    - `YMF281B`: YMF281B互換音色プリセット
    - `YM2423`: OPLL-X音色プリセット
    - 例: `--preset VRC7`
    - 他チップ変換には効果なし

- `--preset_source <YMVOICE|YMFM|EXPERIMENT>`
    - プリセット音色の生成元を選択できます。
        - `YMVOICE`: [ym-voice](https://github.com/digital-sound-antiques/ym-voice) プロジェクト音色
        - `YMFM`: [Copyright-free-OPLL(x)-ROM-patches](https://github.com/plgDavid/misc/wiki/Copyright-free-OPLL(x)-ROM-patches)
        - `EXPERIMENT`: YMFM音色をベースに独自の調整や実験的修正を加えたバージョン
    - **YM2423の場合は `YMFM` または `EXPERIMENT` のみ有効**（`YMVOICE`を選んだ場合もYMFM相当が利用されます）

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
| `--preset <YM2413|VRC7|YMF281B|YM2423>` | YM2413変換時の音色ROM | YM2413 |
| `--preset_source <YMVOICE|YMFM|EXPERIMENT>` | プリセット音色の生成元 | YMFM |
| `--keep_source_vgm` | YM2413コマンドを残し、OPL3と同時演奏 | 無効 |
| `--convert-ym2413` | YM2413のみ変換 | (自動判定) |
| `--convert-ym3812` | YM3812のみ変換 | (自動判定) |
| `--convert-ym3526` | YM3526のみ変換 | (自動判定) |
| `--convert-y8950` | Y8950のみ変換 | (自動判定) |
| `-verbose` | 詳細デバッグ出力 | オフ |

---

## 使い方

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] \
    [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] \
    [-detune_limit <float>] [--preset <YM2413|VRC7|YMF281B|YM2423>] \
    [--preset_source <YMVOICE|YMFM|EXPERIMENT>] \
    [--keep_source_vgm] [--convert-ym2413] [--convert-ym3812] [--convert-ym3526] [--convert-y8950] \
    [-verbose]
```

**オプションは順不同・省略可。必須は `<input.vgm>` と `<detune>` のみ。**

---

### 使用例

```sh
eseopl3patcher song.vgm 20
eseopl3patcher song.vgm 100 -o song_OPL3.vgm -ch_panning 1 -detune_limit 4
eseopl3patcher song.vgm 25 --preset VRC7
eseopl3patcher song.vgm 15 --convert-ym3526 --preset YMF281B
eseopl3patcher song.vgm 2.5 -vr1 0.8
eseopl3patcher song.vgm -8 -ch_panning 1 -verbose
eseopl3patcher song.vgm 30 --keep_source_vgm
eseopl3patcher song.vgm 10 --preset YM2423 --preset_source EXPERIMENT
```

---

## ダウンロード

[![Download latest release](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows用バイナリは [最新リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest) からダウンロードしてください。

---

## ビルド方法

```sh
make win
# または
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```

## ライセンス

MIT License  

---

## 作者

[@emef2247](https://github.com/emef2247)