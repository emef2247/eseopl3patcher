from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple
import math

from .ir_types import IR, Event


@dataclass
class TLItem:
    chip: str
    ch: int
    tick: int
    time_s: float
    kind: str  # "f", "v", "fV", "mode", "en"
    # Values
    reg16: Optional[int] = None
    pitch_hz: Optional[float] = None
    vol4: Optional[int] = None
    # Durations in ticks (per-stream)
    dur_f_ticks: Optional[int] = None
    dur_v_ticks: Optional[int] = None
    # Raw samples (for reference)
    samples: Optional[int] = None


def _gcd_list(ints: List[int]) -> int:
    g = 0
    for x in ints:
        if x <= 0:
            continue
        g = x if g == 0 else math.gcd(g, x)
    return max(1, g)


def _collect_psg_streams(events: List[Event]) -> Dict[Tuple[int, str], List[Tuple[int, Dict]]]:
    """
    集約: (ch,"f"|"v"|"mode") -> [(samples, payload), ...]
    payload:
      f: {"reg16":int,"hz":float}
      v: {"vol4":int}
      mode: {"reg7":int,"mode":int}  # 参考イベント
    """
    out: Dict[Tuple[int, str], List[Tuple[int, Dict]]] = {}
    # PSGの周波数合成: reg0/1=>ch0, 2/3=>ch1, 4/5=>ch2
    fa = [0, 0, 0]
    fb = [0, 0, 0]
    last_reg16 = [None, None, None]
    last_vol4 = [None, None, None]
    last_vvc = 0xB8

    def reg_to_ch(a: int) -> Optional[int]:
        m = {0: 0, 1: 0, 2: 1, 3: 1, 4: 2, 5: 2}
        return m.get(a)

    def reg16_to_hz(reg16: int) -> float:
        if reg16 == 0:
            return 111860.78125
        return 1789772.5 / (16.0 * reg16)

    def mode_from_reg7(vvc: int, ch: int) -> int:
        tone_en = (vvc & (1 << ch)) == 0
        noise_en = (vvc & (1 << (3 + ch))) == 0
        if tone_en and noise_en: return 3
        if noise_en and not tone_en: return 2
        if tone_en and not noise_en: return 1
        return 0

    for e in events:
        if e.chip != "PSG" or e.kind != "reg-write":
            continue
        reg = int(e.extras.get("reg", -1))
        samples = e.samples
        # Freq
        ch = reg_to_ch(reg)
        if ch is not None:
            if reg % 2 == 0:
                fa[ch] = (e.value or 0) & 0xFF
            else:
                fb[ch] = (e.value or 0) & 0xFF
            reg16 = ((fb[ch] & 0xFF) << 8) | (fa[ch] & 0xFF)
            if last_reg16[ch] != reg16:
                hz = reg16_to_hz(reg16)
                out.setdefault((ch, "f"), []).append((samples, {"reg16": reg16, "hz": hz}))
                last_reg16[ch] = reg16
            continue
        # Volume
        if reg in (8, 9, 10):
            ch = reg - 8
            vol4 = (e.value or 0) & 0x0F
            if last_vol4[ch] != vol4:
                out.setdefault((ch, "v"), []).append((samples, {"vol4": vol4}))
                last_vol4[ch] = vol4
            continue
        # Reg7 mode
        if reg == 7:
            last_vvc = (e.value or 0) & 0xFF
            for ch in (0, 1, 2):
                out.setdefault((ch, "mode"), []).append(
                    (samples, {"reg7": last_vvc, "mode": mode_from_reg7(last_vvc, ch)})
                )
            continue
    return out


def _collect_scc_streams(events: List[Event]) -> Dict[Tuple[int, str], List[Tuple[int, Dict]]]:
    """
    SCCは当面v（音量）とen（enable）を収集。fは今後。
    """
    out: Dict[Tuple[int, str], List[Tuple[int, Dict]]] = {}
    last_vol = [None, None, None, None]
    last_en = None
    for e in events:
        if e.chip != "SCC":
            continue
        samples = e.samples
        if e.kind == "reg-write":
            # Volume
            if e.channel is not None and "channel" in e.extras and "vol" in e.extras:
                ch = int(e.extras["channel"])
                vol = int(e.extras["vol"])
                if last_vol[ch] != vol:
                    out.setdefault((ch, "v"), []).append((samples, {"vol": vol, "vol4": min(vol, 15)}))
                    last_vol[ch] = vol
            # Enable
            if "enable_bits" in e.extras:
                en = int(e.extras["enable_bits"])
                if last_en != en:
                    out.setdefault((-1, "en"), []).append((samples, {"enable_bits": en}))
                    last_en = en
    return out


def build_timeline(ir: IR) -> Tuple[int, int, List[TLItem]]:
    """
    量子化してTLItem列を返す。
    Returns: (tick_samples, ticks_per_second, timeline_items)
    """
    # 1) 集約
    psg = _collect_psg_streams(ir.events)
    scc = _collect_scc_streams(ir.events)

    # 2) gcdでtick刻み（samples単位）を決定
    all_samples: List[int] = []
    for streams in (psg, scc):
        for _, seq in streams.items():
            seq_sorted = sorted(seq, key=lambda x: x[0])
            for i in range(1, len(seq_sorted)):
                d = seq_sorted[i][0] - seq_sorted[i - 1][0]
                if d > 0:
                    all_samples.append(d)
    tick_samples = _gcd_list(all_samples)
    ticks_per_second = int(round(ir.header.get("sample_rate", 44100) / tick_samples))

    def samp2tick(s: int) -> int:
        return int(round(s / tick_samples))

    # 3) PSG: f/vを同tick統合（fV）しつつduration付与
    timeline: List[TLItem] = []

    def process_channel_psg(ch: int):
        f_seq = sorted(psg.get((ch, "f"), []), key=lambda x: x[0])
        v_seq = sorted(psg.get((ch, "v"), []), key=lambda x: x[0])

        f_ticks = [(samp2tick(s), s, payload) for s, payload in f_seq]
        v_ticks = [(samp2tick(s), s, payload) for s, payload in v_seq]

        i = j = 0
        merged: List[TLItem] = []
        sr = ir.header.get("sample_rate", 44100)
        while i < len(f_ticks) or j < len(v_ticks):
            if i < len(f_ticks) and (j >= len(v_ticks) or f_ticks[i][0] < v_ticks[j][0]):
                t, s, p = f_ticks[i]
                merged.append(TLItem("PSG", ch, t, s / sr, "f",
                                     reg16=int(p["reg16"]), pitch_hz=float(p["hz"]), samples=s))
                i += 1
            elif j < len(v_ticks) and (i >= len(f_ticks) or v_ticks[j][0] < f_ticks[i][0]):
                t, s, p = v_ticks[j]
                merged.append(TLItem("PSG", ch, t, s / sr, "v",
                                     vol4=int(p["vol4"]), samples=s))
                j += 1
            else:
                # 同tick => fV
                tf, sf, pf = f_ticks[i]
                tv, sv, pv = v_ticks[j]
                t = tf  # == tv
                s = max(sf, sv)
                merged.append(TLItem("PSG", ch, t, s / sr, "fV",
                                     reg16=int(pf["reg16"]), pitch_hz=float(pf["hz"]),
                                     vol4=int(pv["vol4"]), samples=s))
                i += 1
                j += 1

        # duration（次の同種イベントまでのtick差）
        for idx, item in enumerate(merged):
            if item.kind in ("f", "fV"):
                next_tick = next((merged[k].tick for k in range(idx + 1, len(merged))
                                  if merged[k].kind in ("f", "fV")), None)
                if next_tick is not None:
                    item.dur_f_ticks = max(1, next_tick - item.tick)
            if item.kind in ("v", "fV"):
                next_tick = next((merged[k].tick for k in range(idx + 1, len(merged))
                                  if merged[k].kind in ("v", "fV")), None)
                if next_tick is not None:
                    item.dur_v_ticks = max(1, next_tick - item.tick)

        timeline.extend(merged)

    for ch in (0, 1, 2):
        process_channel_psg(ch)

    # 4) SCC（暫定: vのみ）
    sr = ir.header.get("sample_rate", 44100)
    for ch in (0, 1, 2, 3):
        v_seq = sorted(scc.get((ch, "v"), []), key=lambda x: x[0])
        v_ticks = [(samp2tick(s), s, payload) for s, payload in v_seq]
        for idx, (t, s, p) in enumerate(v_ticks):
            next_tick = v_ticks[idx + 1][0] if idx + 1 < len(v_ticks) else None
            dur_v = max(1, next_tick - t) if next_tick is not None else None
            timeline.append(TLItem("SCC", ch, t, s / sr, "v",
                                   vol4=int(p["vol4"]), dur_v_ticks=dur_v, samples=s))

    timeline.sort(key=lambda x: (x.chip, x.ch, x.tick))
    return tick_samples, ticks_per_second, timeline