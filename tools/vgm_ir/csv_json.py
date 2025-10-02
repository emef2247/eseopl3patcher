import csv
import json
from pathlib import Path
from typing import Iterable
from .ir_types import Event, Note, IR
from .quantize import TLItem


def write_events_csv(path: Path, events: Iterable[Event]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["time_s", "samples", "chip", "channel", "kind", "address_hex", "value", "extras_json"])
        for e in events:
            addr_hex = "" if e.address is None else f"0x{e.address:04X}"
            w.writerow([f"{e.time_s:.9f}", e.samples, e.chip, e.channel, e.kind, addr_hex, e.value,
                        json.dumps(e.extras, ensure_ascii=False, separators=(",", ":"))])


def write_notes_csv(path: Path, notes: Iterable[Note]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["chip", "channel", "t_on", "t_off", "duration_s", "pitch_hz", "volume", "cause", "meta_json"])
        for n in notes:
            w.writerow([
                n.chip, n.channel,
                f"{n.t_on:.9f}", f"{n.t_off:.9f}", f"{n.duration_s:.9f}",
                "" if n.pitch_hz is None else f"{n.pitch_hz:.6f}",
                "" if n.volume is None else n.volume,
                n.cause,
                json.dumps(n.meta, ensure_ascii=False, separators=(",", ":")),
            ])


def write_ir_json(path: Path, ir: IR) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(ir.to_dict(), f, ensure_ascii=False, indent=2)


def write_timeline_csv(path: Path, tick_samples: int, ticks_per_second: int, items: Iterable[TLItem]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        # ヘッダ（tick基準情報）
        w.writerow([f"# tick_samples={tick_samples}", f"ticks_per_second={ticks_per_second}"])
        w.writerow(["chip", "ch", "tick", "time_s", "kind", "reg16", "pitch_hz", "vol4",
                    "dur_f_ticks", "dur_v_ticks", "samples"])
        for it in items:
            w.writerow([
                it.chip, it.ch, it.tick, f"{it.time_s:.9f}", it.kind,
                "" if it.reg16 is None else it.reg16,
                "" if it.pitch_hz is None else f"{it.pitch_hz:.6f}",
                "" if it.vol4 is None else it.vol4,
                "" if it.dur_f_ticks is None else it.dur_f_ticks,
                "" if it.dur_v_ticks is None else it.dur_v_ticks,
                "" if it.samples is None else it.samples,
            ])