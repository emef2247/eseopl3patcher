# make_batch_from_vgm.py の使い方

## 概要

`make_batch_from_vgm.py` は、指定ディレクトリ内の `*OPLL.vgm` ファイルを一括で様々なプリセット・モードのOPL3用VGMファイルに変換する **バッチファイル（eseopl3patcher_batch.bat）** を自動生成するPythonスクリプトです。

## 使い方

```sh
python make_batch_from_vgm.py <input_dir>
```

- `<input_dir>` … 変換元の `*OPLL.vgm` ファイル群があるディレクトリ

実行すると、`<input_dir>` 配下に `eseopl3patcher_batch.bat` が生成されます。このバッチファイルを実行することで、様々なパターンの変換VGMファイルが自動的に生成されます。

---

## 入力ファイルのネーミングルール

- **変換元VGM**:  
  - `*OPLL.vgm`  
    - 例: `testOPLL.vgm`, `music1OPLL.vgm` など

---

## 変換後ファイルのネーミングルール

| ファイル名        | 意味・内容                                                                                          |
|-------------------|----------------------------------------------------------------------------------------------------|
| `*YVS.vgm`        | Ym-Voice Single: ym-voice プリセットでOPLL→OPL3変換（OPL3コマンドのみ）                            |
| `*YVM.vgm`        | Ym-Voice Multiple: ym-voice プリセットでOPLL→OPL3変換（OPL3＋元OPLLコマンド両方含む）               |
| `*YFS.vgm`        | YmFm Single: ymfm プリセットでOPLL→OPL3変換（OPL3コマンドのみ）                                    |
| `*YFM.vgm`        | YmFm Multiple: ymfm プリセットでOPLL→OPL3変換（OPL3＋元OPLLコマンド両方含む）                       |
| `*EXPS.vgm`       | Experiment Single: emu由来（実験系）プリセットでOPLL→OPL3変換（OPL3コマンドのみ）                   |
| `*EXPM.vgm`       | Experiment Multiple: emu由来（実験系）プリセットでOPLL→OPL3変換（OPL3＋元OPLLコマンド両方含む）      |

> ※ 例えば `testOPLL.vgm` から `testYVS.vgm` や `testEXPM.vgm` などが生成されます。

---

## 変換後ファイルの出力先フォルダ

| フォルダ名   | 使用されるプリセット                           |
|--------------|----------------------------------------------|
| YM2413       | OPL3はYM2413プリセットを使用                  |
| YM2423       | OPL3はYM2423 (OPLL-X) プリセットを使用        |
| YMF281B      | OPL3はYMF281B (OPLLP) プリセットを使用        |
| VRC7         | OPL3はVRC7 (DS1001) プリセットを使用          |

- 例:  
  - `YM2413/testYVS.vgm`
  - `YMF281B/music1EXPS.vgm`
  - `VRC7/abcYFM.vgm`
  - など

---

## 生成されるバッチファイル

- ファイル名: `eseopl3patcher_batch.bat`
- 内容: 各変換コマンド（`./build/eseopl3patcher`）が1行ずつ記載されています
- 例:
  ```
  ./build/eseopl3patcher "input/abcOPLL.vgm" 100 -o "YM2413/abcYVS.vgm" -ch_panning 1 -detune_limit 4 -preset_source YMVOICE -preset YM2413
  ./build/eseopl3patcher "input/abcOPLL.vgm" 100 -o "YMF281B/abcEXPS.vgm" -ch_panning 1 -detune_limit 4 -preset_source EXPERIMENT -preset YMF281B
  ...
  ```

---

## まとめ

- `make_batch_from_vgm.py` を使うことで、多数のOPLL用VGMを一括で様