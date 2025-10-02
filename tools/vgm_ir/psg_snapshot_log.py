from __future__ import annotations

import math
from pathlib import Path
from typing import List, Optional

from .ir_types import IR, Event
from .decoders_psg_scc import psg_mode_from_reg7


# 期待ヘッダ行（Beyond Compare での突合せを想定して厳密一致）
CSV_HEADER = "#type,time,ch,ticks,l,fL,v,fV,f,fF,o,scale,en,fEn,vDiff,vCnt,oDiff,envlp,envlpIndex,nE,nF,offset,data,wtbIndex,fCtrlA,fCtrlB,wNCtrl,vVCtrl,aVCtrl,envPCtrlL,envPCtrlM,envShape,ioParallel1,ioParallel2"


def _time_from_samples(samples: int) -> float:
    # 指定に従い 44100.0 固定
    return samples / 44100.0


def _ticks_from_time(time_s: float) -> int:
    # 指定に従い ceil(time*60)、ただし 1 => 0
    t = int(math.ceil(time_s * 60.0))
    if t == 1:
        return 0
    return t


def _type_from_reg(reg: int) -> str:
    if reg in (0, 2, 4):
        return "fCA"
    if reg in (1, 3, 5):
        return "fCB"
    if reg == 6:
        return "wNC"
    if reg == 7:
        return "mode"
    if reg in (8, 9, 10):
        return "aVC"
    if reg == 11:
        return "envL"
    if reg == 12:
        return "envM"
    if reg == 13:
        return "envS"
    if reg == 14:
        return "ioP1"
    if reg == 15:
        return "ioP2"
    return f"reg{reg}"  # フォールバック（通常到達しない）


def _channel_from_reg(reg: int) -> Optional[int]:
    if reg in (0, 1, 8):
        return 0
    if reg in (2, 3, 9):
        return 1
    if reg in (4, 5, 10):
        return 2
    return None  # Reg6/7/11-15 は None（直近の ch を使う）


def write_psg_snapshot_log_csv(path: Path, ir: IR) -> None:
    """
    VGM中のPSGレジスタアクセスを1行ずつ、期待ヘッダに揃えてCSV出力する。
    - 全レジスタのスナップショットを毎行末尾に列挙
    - time=累積サンプル/44100.0, ticks=ceil(time*60) with special-case
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    # PSGレジスタ 0..15 の現在値（スナップショット）
    regs = [0] * 16
    last_ch: int = 0  # Reg6/7/11-15 の行で使う ch

    with path.open("w", encoding="utf-8", newline="") as f:
        f.write(CSV_HEADER + "\n")

        # IR内のイベント順序はVGMの出現順のはず
        for e in ir.events:
            if e.chip != "PSG" or e.kind != "reg-write":
                continue

            # 必須情報
            reg = int(e.extras.get("reg", -1))
            if reg < 0 or reg > 15:
                continue
            val = int(e.value or 0) & 0xFF
            regs[reg] = val

            # 行属性
            t_abs = _time_from_samples(e.samples)
            ticks = _ticks_from_time(t_abs)
            ch = _channel_from_reg(reg)
            if ch is None:
                ch = last_ch
            else:
                last_ch = ch

            # en（tone/noiseの有効状態 0..3）
            en_val = psg_mode_from_reg7(regs[7], ch)

            # スナップショット値（チャンネル依存＋グローバル）
            fCtrlA = regs[ch * 2 + 0]  # reg0/2/4
            fCtrlB = regs[ch * 2 + 1]  # reg1/3/5
            wNCtrl = regs[6]
            vVCtrl = regs[7]
            aVCtrl = regs[8 + ch] & 0x1F  # 5bit (VA含む)
            envPCtrlL = regs[11]
            envPCtrlM = regs[12]
            envShape = regs[13]
            ioParallel1 = regs[14]
            ioParallel2 = regs[15]

            # 出力行の構築（未使用カラムは空文字）
            # フロートは比較のしやすさを考慮し 16桁の有効桁で出力
            time_str = f"{t_abs:.16g}"
            row = [
                _type_from_reg(reg),         # type
                time_str,                    # time
                str(ch),                     # ch
                str(ticks),                  # ticks
                "", "", "", "", "", "", "", "",  # l,fL,v,fV,f,fF,o,scale
                str(en_val),                 # en
                "", "", "", "",              # fEn,vDiff,vCnt,oDiff
                "", "", "", "",              # envlp,envlpIndex,nE,nF
                "", "", "",                  # offset,data,wtbIndex
                str(fCtrlA),
                str(fCtrlB),
                str(wNCtrl),
                str(vVCtrl),
                str(aVCtrl),
                str(envPCtrlL),
                str(envPCtrlM),
                str(envShape),
                str(ioParallel1),
                str(ioParallel2),
            ]

            f.write(",".join(row) + "\n")