# eseopl3patcher

YM3812(OPL2) VGMファイルをYMF262(OPL3) VGMファイルへ変換するツール

## 概要

eseopl3patcherは、YM3812（OPL2）用に作られたVGMファイルを、OPL3（YMF262）用に変換するコマンドラインツールです。OPL3の拡張チャンネルを活用し、元の9chに対してデチューンした9chを追加し、合計18chによるコーラス効果を実現します。

## 特徴

- YM3812(OPL2)コマンドをOPL3(YMF262)コマンドに変換
- OPL3拡張チャンネル（ch9～ch17）側にデチューンを適用
- GD3タグに変換情報・実行者情報を自動追記
- コマンドラインから簡単操作

## 特徴の入力:

- VGMファイル（YM3812/OPL2形式）
- デチューン比率（小数, 例: 2.5）  
- KeyOn後のウェイト（整数, 例: 1）  
- このプログラムの実行者（ここで指定した文字列がGD3のCreator欄に追記されます）

## 使い方

```sh
eseopl3patcher <input.vgm> <detune_ratio> <keyon_wait> <operator> [-o output.vgm]
```

例:
```sh
eseopl3patcher sample.vgm 2.5 1 "YourName"
```

- `<input.vgm>` : 変換するVGMファイル（YM3812/OPL2形式）
- `<detune_ratio>` : デチューン比率（小数, 例: 2.5）
- `<keyon_wait>` : KeyOn後のウェイト（整数, 例: 1）
- `<operator>` : このプログラムの実行者（ここで指定した文字列がGD3のCreator欄に追記されます）
- `-o output.vgm` : 出力ファイル名（省略時は自動生成）

## ライセンス

MIT License