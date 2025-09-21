# eseopl3patcher

YM3812, YM3526, Y8950 の VGM ファイルを YMF262 (OPL3) VGM ファイルに変換するツール

## 概要

eseopl3patcher は、OPL2/OPL ファミリー（YM3812, YM3526, Y8950）用の VGM ファイルを OPL3 (YMF262) フォーマットに変換するコマンドラインツールです。OPL3 の拡張チャンネルを利用してデチューン（Chorus効果）を追加し、柔軟なステレオ・ボリューム設定が可能です。

## サポートされている入力チップ

- **YM3812 (OPL2)**
- **YM3526 (OPL)**
- **Y8950 (OPL, ADPCM部は未変換)**

これらのチップ用VGMファイルが変換対象として利用できます。

## 主な機能

- YM3812, YM3526, Y8950 コマンドを OPL3 (YMF262) コマンドに変換
- OPL3 の拡張チャンネル (ch9–17) にデチューンを適用してコーラス効果を追加
- チャンネルごとのパンニング設定（`-ch_panning`オプション参照）
- ポートごとの独立したボリューム比設定（`-vr0`, `-vr1`）
- 詳細デバッグ出力（`-verbose`）
- GD3タグに自動的に変換情報やオペレータ情報を追加
- コマンドラインから柔軟に各種オプションを指定可能
- **複数OPLチップが混在している場合は自動判定で一つだけ変換します。明示的選択も可**

## 入力パラメータ

- VGMファイル (YM3812/OPL2/YM3526/Y8950 フォーマット)
- デチューン値（パーセント指定, 例: `2.5` → +2.5%、`-1` → -1%）
    - デチューン値はパーセントで指定します。例: `2.5`=+2.5%、`-1`=-1%
- KeyOnウェイト（整数, 例: `1`）※省略可（初期値: `0`）
- クリエイター名（GD3タグに追記, 初期値: `eseopl3patcher`）
- 出力ファイル名 (`-o output.vgm`) ※省略可（初期値: `<input>OPL3.vgm`）
- チャンネルパンニングモード (`-ch_panning 0|1`) ※省略可（初期値: `0`）
    - `0`: Port 0 (ch0–ch8) を左、Port 1 (ch9–ch17) を右に出力
    - `1`: 偶数/奇数チャンネルをL/R交互にパンニング
- Port 0 ボリューム比 (`-vr0 <float>`) ※省略可（初期値: `1.0`）
- Port 1 ボリューム比 (`-vr1 <float>`) ※省略可（初期値: `0.6`）
- 詳細デバッグ (`-verbose`) ※省略可（初期値: OFF）

### OPLチップの明示的選択

複数OPLチップが混在している場合、デフォルトでは最初に現れたチップのみ変換されます。  
複数チップを同時に変換したい場合は、下記オプションを明示的に指定してください。

- `--convert-ym3812` : YM3812 を変換
- `--convert-ym3526` : YM3526 を変換
- `--convert-y8950`  : Y8950 を変換

複数指定も可能です。

### デチューン値（+/-の指定について）

- デチューン値は正・負どちらでも指定可能です。
    - 正の値（例: `2.5`）はピッチが上がります
    - 負の値（例: `-2.5`）はピッチが下がります
- 例:
    - `2.5` … FNumber に +2.5% 加算（高音化）
    - `-2.5` … FNumber から -2.5% 減算（低音化）

> **デチューン値の符号で変換後のピッチ方向が決まります。用途に合わせて指定してください。**

### チャンネルパンニング・ボリューム比・デバッグについて

- `-ch_panning 0`（初期値）: Port 0→左, Port 1→右。デチューン音を右チャンネルで分離して聴きやすいです
- `-ch_panning 1`: 偶数/奇数チャンネルを交互にL/Rパンニングでステレオ効果
- `-vr0`, `-vr1`でPortごとのバランス調整が可能です
    - 例: デチューン音を小さくする場合 `-vr1 0.6`
    - コーラス効果を強調したい場合は `-ch_panning 0`推奨

## 使い方

```sh
eseopl3patcher <input.vgm> <detune> [keyon_wait] [creator] [-o output.vgm] [-ch_panning 0|1] [-vr0 <float>] [-vr1 <float>] [-verbose] [--convert-ym3812] [--convert-ym3526] [--convert-y8950]
```

- `<input.vgm>` : VGMファイル（YM3812/OPL2/YM3526/Y8950対応）
- `<detune>` : デチューン値（パーセント, 例 `2.5`, `-1`）
- `[keyon_wait]` : KeyOnウェイト（整数, 省略可, 初期値: 0）
- `[creator]` : GD3タグのクリエイター（省略可, 初期値: "eseopl3patcher"）
- `[-o output.vgm]` : 出力ファイル名（省略可, 自動生成）
- `[-ch_panning 0|1]` : パンニングモード（省略可, 初期値: 0）
- `[-vr0 <float>]` : Port 0ボリューム比（省略可, 初期値: 1.0）
- `[-vr1 <float>]` : Port 1ボリューム比（省略可, 初期値: 0.6）
- `[-verbose]` : 詳細デバッグ出力（省略可）
- `[--convert-ym3812] [--convert-ym3526] [--convert-y8950]` : 変換するOPLチップを明示的に選択（複数可）

パラメータは `<input.vgm>`, `<detune>` の後なら順番自由です。

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
eseopl3patcher song.vgm 2.5 -verbose
eseopl3patcher song.vgm 2.5 -ch_panning 1 -verbose -vr1 0.7
eseopl3patcher song.vgm 2.5 --convert-ym3526
eseopl3patcher song.vgm 2.5 --convert-y8950 --convert-ym3526
```

## ダウンロード

[![Linux/Windows版ダウンロード](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux/Windows用のバイナリは [最新リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest) からダウンロードできます。

## ビルド

```sh
make win
# または
gcc -O2 -Wall -Iinclude -o eseopl3patcher.exe src/*.c
```

# VGM ツール取得・ビルド手順

このディレクトリはテスト・解析用に外部ツール (vgm2txt, VGMPlay) をローカル取得・ビルドするための補助スクリプトを提供します。  
バイナリを直接リポジトリにコミットしないことで、サイズとライセンス管理の負担を下げつつ再現性を確保します。

## 取得対象

| ツール | リポジトリ | 用途 | 生成バイナリ例 |
|--------|-----------|------|----------------|
| vgm2txt | https://github.com/vgmrips/vgmtools | VGM → テキスト（レジスタ列解析） | tools/bin/vgm2txt |
| VGMPlay | https://github.com/vgmrips/vgmplay  | 変換後 VGM の実再生確認 | tools/bin/VGMPlay (または vgmplay) |

## 使い方

```bash
# 初回または更新
bash tools/fetch_vgm_tools.sh
# 既存 clone を更新せずに再コピーだけしたい場合は --no-update などを参照
```

完了後:
```
tools/
  bin/
    vgm2txt
    VGMPlay         (環境により vgmplay になる場合あり)
  version/
    vgm2txt_commit.txt
    vgmplay_commit.txt
    vgm_tools_meta.json
  _src/
    vgmtools/       (git clone ソース)
    vgmplay/        (git clone ソース)
```

`version/*.txt` には使用したコミットハッシュが記録され、再現ビルドに利用できます。

## 任意コミットを固定したい場合

環境変数でコミット（またはタグ）を指定:

```bash
VGMTOOLS_COMMIT=3f1ab2c VGMPLAY_COMMIT=v0.40 bash tools/fetch_vgm_tools.sh
```

指定が無い場合は各リポジトリのデフォルトブランチ (main / master) の最新を取得します。

## オプション

| オプション | 説明 |
|-----------|------|
| `--force` | 既存ビルドを無条件で再ビルド |
| `--no-update` | 既存 clone があれば `git fetch` を行わずそのまま再ビルド |
| `--skip-vgmplay` | VGMPlay をスキップ（vgm2txt のみ） |
| `--skip-vgm2txt` | vgm2txt をスキップ（VGMPlay のみ） |
| `--quiet` | 最小限のログ |

## CI 組み込み例 (GitHub Actions)

```yaml
- name: Fetch VGM tools
  run: |
    bash tools/fetch_vgm_tools.sh --quiet
- name: Verify vgm2txt
  run: tools/bin/vgm2txt --help | head -n 1
```

## PATH 追加 (ローカル)

```bash
export PATH="$PWD/tools/bin:$PATH"
```

## ライセンス

各プロジェクトのライセンス (vgmtools / vgmplay) はそれぞれのリポジトリに従います。  
再配布する場合はオリジナルの README / LICENSE への参照を保ってください。

## トラブルシュート

| 症状 | 対処 |
|------|------|
| `make: g++: command not found` | `sudo apt install build-essential` |
| `fatal: unable to access` | ネットワーク / Proxy / GitHub 一時障害 |
| VGMPlay バイナリが見つからない | `make` ログを確認。OS 毎に生成ファイル名差異 (大文字/小文字) |
| 既存バイナリを更新したくない | スクリプト未実行、または判定部を書き換え |

## 将来拡張予定

- 特定コミットの SHA256 検証キャッシュ
- vgm_cmp 等の追加ビルド
- Windows (MSYS2) / macOS 自動分岐

## 作者

[@emef2247](https://github.com/emef2247) 作

## ライセンス

MIT License