from __future__ import annotations
import csv
import os
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional

from .model import PatchParams, NoteContext

def _to_int(x: str) -> int:
    s = str(x).strip().lower()
    if s.startswith("0x"):
        return int(s, 16)
    return int(s)

def _pick(headers, candidates):
    for c in candidates:
        if c in headers:
            return c
    return None

@dataclass
class ChanState:
    fnum_lo: int = 0
    fnum_hi: int = 0  # 3 bits
    blk: int = 0      # 3 bits
    ko: int = 0       # 0/1
    last_on_time: Optional[float] = None

def load_from_timeline_csv(
    csv_path: str,
    default_patch: PatchParams,
    max_channels: int = 9,
) -> Dict[Tuple[str, int], Dict[str, object]]:
    """
    Parse a YM2413 timeline CSV and build per-channel note sequences.
    - Detect KO rising edges on 0x20..0x28 writes.
    - Track FNUM low (0x10..0x18) and high+blk+ko (0x20..0x28).
    Returns:
      {(pattern_name, ch): {"patch": PatchParams, "notes": [NoteContext, ...]}}
    """
    if not os.path.exists(csv_path):
        raise FileNotFoundError(csv_path)

    pattern_name = os.path.splitext(os.path.basename(csv_path))[0].replace("_timeline_YM2413", "")

    # Initialize channel states and note buffers
    chans: Dict[int, ChanState] = {ch: ChanState() for ch in range(max_channels)}
    notes: Dict[int, List[NoteContext]] = {ch: [] for ch in range(max_channels)}

    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        headers = [h.strip() for h in (reader.fieldnames or [])]

        time_key = _pick(headers, ["time", "time_s", "sec", "t"])
        addr_key = _pick(headers, ["addr", "address", "reg", "register"])
        data_key = _pick(headers, ["data", "val", "value"])
        ch_key = _pick(headers, ["ch", "channel"])

        if not all([time_key, addr_key, data_key, ch_key]):
            raise ValueError(f"Timeline CSV missing required headers. Found: {headers}")

        for row in reader:
            try:
                t = float(row[time_key])
                addr = _to_int(row[addr_key])
                data = _to_int(row[data_key])
                ch = int(row[ch_key])
            except Exception:
                # Skip malformed lines
                continue
            if ch < 0 or ch >= max_channels:
                continue

            st = chans[ch]

            # 0x10..0x18: FNUM low 8 bits
            if 0x10 <= addr <= 0x18:
                st.fnum_lo = data & 0xFF

            # 0x20..0x28: FNUM high (3b), BLK (3b), KO (bit4)
            if 0x20 <= addr <= 0x28:
                st.fnum_hi = data & 0x07
                st.blk = (data >> 1) & 0x07  # BLK occupies bits 1..3 on YM2413
                ko = (data >> 4) & 0x01

                # KO rising edge -> note on
                if st.ko == 0 and ko == 1:
                    t_on = t
                    fnum = (st.fnum_hi << 8) | st.fnum_lo
                    # NoteContext iois will be filled after building all onsets
                    notes[ch].append(NoteContext(fnum=fnum, blk=st.blk, t_on=t_on, ioi=0.0))
                    st.last_on_time = t_on

                st.ko = ko

    # Fill IOI per channel (to next onset)
    for ch, seq in notes.items():
        for i in range(len(seq) - 1):
            seq[i].ioi = max(1e-6, seq[i + 1].t_on - seq[i].t_on)
        # last note has no next onset -> drop to avoid skew
        if seq and seq[-1].ioi <= 0.0:
            seq.pop()

    # Build dataset dict
    dataset: Dict[Tuple[str, int], Dict[str, object]] = {}
    for ch, seq in notes.items():
        if not seq:
            continue
        dataset[(pattern_name, ch)] = {"patch": default_patch, "notes": seq}

    return dataset
