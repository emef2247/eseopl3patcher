from __future__ import annotations
import json
import math
from dataclasses import dataclass
from typing import Dict, List, Tuple, Any

@dataclass
class PatchParams:
    ar: int
    dr: int
    sl: int
    rr: int
    ksr: bool

@dataclass
class NoteContext:
    fnum: int
    blk: int
    t_on: float
    ioi: float

class YM2413EnvelopeExact:
    def __init__(self, tables: Dict[str, Any], exp_shape: float = 0.85):
        self.tables = tables
        self.exp_shape = exp_shape
        self._atk = self._require_times("attack")
        self._dec = self._require_times("decay")
        self._rel = self._require_times("release")
        self._ksr_shift = self._require_ksr_shift()

    def _require_times(self, name: str) -> List[float]:
        try:
            arr = self.tables["eg_times_ms"][name]
            if len(arr) != 16:
                raise ValueError(f"{name} must have 16 entries")
            return [max(1e-6, float(ms) / 1000.0) for ms in arr]
        except Exception as e:
            raise ValueError(f"Invalid eg_times_ms.{name}: {e}")

    def _require_ksr_shift(self) -> List[int]:
        try:
            arr = self.tables["ksr"]["per_blk_shift"]
            if len(arr) != 8:
                raise ValueError("per_blk_shift must have 8 entries (blk=0..7)")
            return [int(v) for v in arr]
        except Exception as e:
            raise ValueError(f"Invalid ksr.per_blk_shift: {e}")

    def _rate_to_time(self, rate_code: int, kind: str, ksr_on: bool, blk: int) -> float:
        rc = max(0, min(15, int(rate_code)))
        if ksr_on:
            rc = max(0, min(15, rc + self._ksr_shift[max(0, min(7, blk))]))
        table = {"attack": self._atk, "decay": self._dec, "release": self._rel}[kind]
        return table[rc]

    def _sl_to_amp(self, sl_code: int) -> float:
        sl_code = max(0, min(15, sl_code))
        return 1.0 - (sl_code / 15.0)

    def residual_at(self, patch: PatchParams, note: NoteContext, gate: float, t_query: float) -> float:
        t_on = note.t_on
        t_off = note.t_on + max(0.0, min(1.0, gate)) * note.ioi

        ar_s = self._rate_to_time(patch.ar, "attack", patch.ksr, note.blk)
        dr_s = self._rate_to_time(patch.dr, "decay",  patch.ksr, note.blk)
        rr_s = self._rate_to_time(patch.rr, "release",patch.ksr, note.blk)
        sl_amp = self._sl_to_amp(patch.sl)

        return self._amp_at_time(t_query, t_on, t_off, ar_s, dr_s, rr_s, sl_amp)

    def choose_gate_grid(
        self,
        patch,
        notes,
        gate_min: float = 0.45,
        gate_max: float = 0.98,
        gate_step: float = 0.005,
        residual_threshold_db: float = -60.0,
        overlap_weight: float = 1.0,
        gap_weight: float = 0.25,
        db_weight: float = 0.25,
    ):
        best_gate, best_score, best_metrics = None, float("inf"), {}
        n_steps = int((gate_max - gate_min) / gate_step) + 1
        gates = [round(gate_min + i * gate_step, 4) for i in range(n_steps)]

        def amp_to_db(a: float) -> float:
            a = max(1e-6, min(1.0, a))
            return 20.0 * math.log10(a)

        any_transition = False

        for g in gates:
            residual_sum = 0.0
            residual_db_sum = 0.0
            overlap_events = 0
            gap_penalty_sum = 0.0
            sustain_loss_sum = 0.0
            count = 0

            for i, n in enumerate(notes):
                if i + 1 >= len(notes):
                    break
                any_transition = True
                next_on = notes[i + 1].t_on

                amp_res = self.residual_at(patch, n, g, next_on)
                residual_sum += amp_res
                res_db = amp_to_db(amp_res)
                residual_db_sum += max(0.0, (res_db - residual_threshold_db) / abs(residual_threshold_db))
                count += 1
                if res_db > residual_threshold_db:
                    overlap_events += 1

                t_probe = n.t_on + min(0.8 * n.ioi, max(1e-4, g * n.ioi) - 1e-4)
                amp_probe = self.residual_at(patch, n, g, t_probe)
                sustain_target = max(self._sl_to_amp(patch.sl), 0.2)
                loss = max(0.0, sustain_target - amp_probe)
                sustain_loss_sum += loss

                if g < 0.60:
                    gap_penalty_sum += (0.60 - g)

            if count == 0:
                continue
            avg_residual = residual_sum / count
            avg_residual_db_norm = residual_db_sum / count
            avg_sustain_loss = sustain_loss_sum / count

            score = (
                avg_residual * overlap_weight
                + (avg_sustain_loss + gap_penalty_sum) * gap_weight
                + avg_residual_db_norm * db_weight
            )

            if score < best_score:
                best_gate = g
                best_score = score
                best_metrics = {
                    "avg_residual": avg_residual,
                    "avg_residual_db_norm": avg_residual_db_norm,
                    "overlap_events": overlap_events,
                    "avg_sustain_loss": avg_sustain_loss,
                    "score": score,
                }

        if best_gate is None:
            # No note-to-note transition found in this sequence: return neutral defaults
            return 0.8, {
                "avg_residual": 0.0,
                "avg_residual_db_norm": 0.0,
                "overlap_events": 0,
                "avg_sustain_loss": 0.0,
                "score": 0.0,
            }
        return best_gate, best_metrics

    def _amp_at_time(self, t: float, t_on: float, t_off: float,
                     ar_s: float, dr_s: float, rr_s: float, sl_amp: float) -> float:
        if t <= t_on:
            return 0.0

        ta = max(1e-6, ar_s)
        dt = t - t_on
        if dt < ta:
            return self._rise(dt / ta)

        td = max(1e-6, dr_s)
        if t < t_off:
            dd = dt - ta
            if dd < td:
                return self._mix(1.0, sl_amp, dd / td)
            else:
                return sl_amp

        tr = max(1e-6, rr_s)
        if (t_off - t_on) < ta:
            lvl_off = self._rise((t_off - t_on) / ta)
        else:
            dd = (t_off - t_on) - ta
            if dd < td:
                lvl_off = self._mix(1.0, sl_amp, dd / td)
            else:
                lvl_off = sl_amp

        dr = t - t_off
        if dr >= tr:
            return 0.0
        return self._mix(lvl_off, 0.0, dr / tr)

    def _rise(self, x: float) -> float:
        s = self.exp_shape
        if s <= 0.0:
            return x
        return 1.0 - math.exp(-x * (1.0 / max(1e-3, s)))

    def _mix(self, a: float, b: float, x: float) -> float:
        x = max(0.0, min(1.0, x))
        s = self.exp_shape
        if s <= 0.0:
            return a + (b - a) * x
        return a + (b - a) * (x ** (1.0 / max(1e-3, s)))


def load_tables_json(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)