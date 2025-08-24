# eseopl3patcher

YM3812(OPL2) VGMファイルをYMF262(OPL3) VGMファイルへ変換するツール

## 概要

eseopl3patcherは、YM3812（OPL2）用に作られたVGMファイルを、OPL3（YMF262）用に変換するコマンドラインツールです。OPL3の拡張チャンネルを活用し、元の9chに対してデチューンした9chを追加し、合計18chによるコーラス効果を実現します。

## 特徴

- YM3812(OPL2)コマンドをOPL3(YMF262)コマンドに変換
- OPL3拡張チャンネル（ch9～ch17）側にデチューンを適用
- GD3タグに変換情報・実行者情報を自動追記
- コマンドラインから簡単操作

## 入力パラメータ

- VGMファイル（YM3812/OPL2形式）
- デチューン値（パーセンテージ指定。例：2.5は+2.5%デチューン）
    - デチューン値は常にパーセント（%）で指定してください。例：`2.5` = 2.5%、`0.5` = 0.5%、`10` = 10%
- KeyOn後のウェイト（整数, 例: 1）
- このプログラムの実行者（ここで指定した文字列がGD3のCreator欄に追記されます）

### Detuneの比率について（＋／－の指定）

- デチューン値は「+」または「-」の値を指定できます。
    - プラス値（例：2.5）はFNumberに加算され、音程が高くなります。
    - マイナス値（例：-2.5）はFNumberから減算され、音程が低くなります。
- 指定できる値の例：
    - `2.5` … FNumberに+2.5%相当を加算
    - `-2.5` … FNumberから-2.5%相当を減算

> **デチューン値の正負で変換後の音程が上下します。用途に合わせて指定してください。**

## 使い方

```sh
eseopl3patcher <input.vgm> <detune_ratio> <keyon_wait> <operator> [-o output.vgm]
```

例:
```sh
eseopl3patcher sample.vgm 2.5 1 "YourName"
```

- `<input.vgm>` : 変換するVGMファイル（YM3812/OPL2形式）
- `<detune_ratio>` : デチューン比率（パーセンテージ。例: 2.5なら2.5%。1.0は100%として扱われます。±の指定で加算／減算を選択）
- `<keyon_wait>` : KeyOn後のウェイト（整数, 例: 1）
- `<operator>` : このプログラムの実行者（ここで指定した文字列がGD3のCreator欄に追記されます）
- `-o output.vgm` : 出力ファイル名（省略時は自動生成）

## ダウンロード

[![Download for Linux and Windows](https://img.shields.io/github/v/release/emef2247/eseopl3patcher?label=Download%20latest%20release)](https://github.com/emef2247/eseopl3patcher/releases/latest)

Linux用・Windows用のビルド済みバイナリは [最新リリースページ](https://github.com/emef2247/eseopl3patcher/releases/latest) からダウンロードできます。

## ライセンス

MIT License