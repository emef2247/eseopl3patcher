from __future__ import annotations
from dataclasses import dataclass
from typing import Optional, Tuple

# Simplified envelope model for YM2413-like behavior.
# Intent: structure matches public emulator concepts (AR/DR/SL/RR, KSR, KO state machine),
# but rate->time is parametric to be replaced with exact tables from ymfm/EMU2413 later.

@dataclass
class PatchParams:
    ar: int  # 0..15
    dr: int  # 0..15
    sl: int  # 0..15  (0=full, 15=lowest sustain)
    rr: int  # 0..15
    ksr: bool  # Key scaling rate enabled

@dataclass
class NoteContext:
    fnum: int   # raw fnum from A0/A1 (or equivalent)
    blk: int    # 0..7
    t_on: float # seconds
    ioi: float  # seconds to next onset (inter-onset interval)
    # If KO already toggled in source, we keep it; otherwise estimator will select KO_off by gate.

class YM2413EnvelopeModel:
    def __init__(
        self,
        # Base time scales (rough; will be replaced by proper tables later)
        base_attack_ms: float = 0.50,   # AR=8..10 around ~fast 0.1..0.2s; AR small -> slower
        base_decay_ms: float = 1.00,
        base_release_ms: float = 0.80,
        # Shape controls: 0=linear, 1=exp-like
        exp_shape: float = 0.85,
        # KSR scaling per octave (blk step). Positive reduces times for higher pitch.
        ksr_scale_per_blk: float = 0.15,
        # Sustain level mapping: convert 0..15 SL to amplitude (0..1)
        sl_floor: float = 0.05,
        sl_curve: float = 1.0,
    ):
        self.base_attack_ms = base_attack_ms
        self.base_decay_ms = base_decay_ms
        self.base_release_ms = base_release_ms
        self.exp_shape = exp_shape
        self.ksr_scale_per_blk = ksr_scale_per_blk
        self.sl_floor = sl_floor
        self.sl_curve = sl_curve

    # --- Public API ---

    def residual_at(self, patch: PatchParams, note: NoteContext, gate: float, t_query: float) -> float:
        """
        Compute amplitude of this note at absolute time t_query.
        KO_off is set to note.t_on + gate * note.ioi.
        """
        t_on = note.t_on
        t_off = note.t_on + max(0.0, min(1.0, gate)) * note.ioi
        ksr_factor = self._ksr_factor(patch, note.fnum, note.blk)

        ar_s = self._code_to_time_sec(patch.ar, self.base_attack_ms, ksr_factor)
        dr_s = self._code_to_time_sec(patch.dr, self.base_decay_ms, ksr_factor)
        rr_s = self._code_to_time_sec(patch.rr, self.base_release_ms, ksr_factor)

        sl_amp = self._sl_to_amp(patch.sl)

        return self._amp_at_time(t_query, t_on, t_off, ar_s, dr_s, rr_s, sl_amp)

    def choose_gate_grid(self, patch: PatchParams, notes: list[NoteContext],
                         gate_min: float = 0.50, gate_max: float = 0.98, gate_step: float = 0.01,
                         residual_threshold_db: float = -40.0,
                         overlap_weight: float = 1.0, gap_weight: float = 0.2) -> Tuple[float, dict]:
        """
        Grid search a single Gate for a sequence (pattern/channel).
        Objective focuses on minimizing residual amplitude at next onset (dB-domain),
        with mild penalty for too-early KO (gap) based on sustain loss.
        """
        best_gate, best_score, best_metrics = None, float("inf"), {}
        gates = [round(gate_min + i * gate_step, 4) for i in range(int((gate_max - gate_min) / gate_step) + 1)]

        for g in gates:
            residual_sum_db = 0.0
            overlap_events = 0
            gap_penalty_sum = 0.0
            sustain_loss_sum = 0.0
            count = 0

            for i, n in enumerate(notes):
                # Skip last note for residual-at-next-onset
                if i + 1 >= len(notes):
                    break
                next_on = notes[i + 1].t_on

                # Residual from current note at next onset (convert to dB)
                amp_res = self.residual_at(patch, n, g, next_on)
                # Convert amplitude to dB (relative to full scale)
                amp_res_db = self._amp_to_db(amp_res)
                residual_sum_db += amp_res_db
                count += 1
                if amp_res_db > residual_threshold_db:
                    overlap_events += 1

                # Gap/sustain evaluation: amplitude near 0.7*IOI should not be too low while KO=1
                # Shortened from 0.8 to 0.7 for provisional release timing
                t_probe = n.t_on + min(0.7 * n.ioi, max(1e-4, g * n.ioi) - 1e-4)
                amp_probe = self.residual_at(patch, n, g, t_probe)
                # Sustain target approx: between SL and 1 depending on decay progress
                sustain_target = max(self._sl_to_amp(patch.sl), 0.2)
                loss = max(0.0, sustain_target - amp_probe)
                sustain_loss_sum += loss

                # Penalize KO too early vs IOI (encourage at least 55% of IOI by default, lowered from 65%)
                if g < 0.55:
                    gap_penalty_sum += (0.55 - g)

            if count == 0:
                continue
            avg_residual_db = residual_sum_db / count
            avg_sustain_loss = sustain_loss_sum / count
            # Normalize dB score to positive range for mixing with other penalties
            score = (avg_residual_db + 60.0) * overlap_weight + (avg_sustain_loss + gap_penalty_sum) * gap_weight

            if score < best_score:
                best_gate = g
                best_score = score
                best_metrics = {
                    "avg_residual_db": avg_residual_db,
                    "overlap_events": overlap_events,
                    "avg_sustain_loss": avg_sustain_loss,
                    "score": score,
                }

        return best_gate if best_gate is not None else 0.8, best_metrics

    # --- Internals ---

    def _ksr_factor(self, patch: PatchParams, fnum: int, blk: int) -> float:
        if not patch.ksr:
            return 1.0
        # Simplified: higher blk speeds up envelope (shorter times).
        # Each blk step reduces time by ksr_scale_per_blk.
        factor = max(0.3, 1.0 - self.ksr_scale_per_blk * max(0, min(7, blk)))
        return factor

    def _code_to_time_sec(self, code: int, base_ms: float, ksr_factor: float) -> float:
        """
        Map 0..15 code to a time constant. Code==0 considered 'hold' (very long).
        Shape is exponential-like; calibrated later with real tables.
        """
        if code <= 0:
            return 10.0  # effectively 'no change'
        # Faster for larger code. 15 -> very fast; 1 -> very slow.
        from math import pow
        rel = code - 8
        scale = pow(0.5, rel / 4.0)  # every +4 steps ~half the time
        t = max(1e-4, base_ms * scale * ksr_factor)
        return t

    def _sl_to_amp(self, sl_code: int) -> float:
        # Map 0..15 to amplitude [sl_floor..1], 0=1.0, 15=sl_floor
        sl_code = max(0, min(15, sl_code))
        span = 1.0 - self.sl_floor
        x = 1.0 - (sl_code / 15.0)
        try:
            from math import pow
            if self.sl_curve != 1.0:
                x = pow(x, self.sl_curve)
        except Exception:
            pass
        return self.sl_floor + span * x

    def _amp_at_time(self, t: float, t_on: float, t_off: float,
                     ar_s: float, dr_s: float, rr_s: float, sl_amp: float) -> float:
        """
        Piecewise AD(S)R with simple curves. Returns amplitude [0..1].
        """
        if t <= t_on:
            return 0.0

        # Attack to 1.0 over ar_s
        ta = max(1e-6, ar_s)
        dt = t - t_on
        if dt < ta:
            return self._rise(dt / ta)

        # Decay from 1.0 to sl_amp over dr_s while KO=1 (i.e., before t_off)
        td = max(1e-6, dr_s)
        if t < t_off:
            dd = dt - ta
            if dd < td:
                return self._mix(1.0, sl_amp, dd / td)
            else:
                return sl_amp

        # Release from current level to 0 over rr_s starting at t_off
        tr = max(1e-6, rr_s)
        # Level at t_off:
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

    def _amp_to_db(self, amp: float) -> float:
        """Convert linear amplitude to dB (relative to full scale = 1.0)"""
        from math import log10
        if amp <= 1e-12:
            return -120.0  # floor
        return 20.0 * log10(amp)

    def _rise(self, x: float) -> float:
        # 0..1 -> 0..1, exp-shaped rise
        s = self.exp_shape
        if s <= 0.0:
            return x
        from math import exp
        return 1.0 - exp(-x * (1.0 / max(1e-3, s)))

    def _mix(self, a: float, b: float, x: float) -> float:
        x = max(0.0, min(1.0, x))
        s = self.exp_shape
        if s <= 0.0:
            return a + (b - a) * x
        return a + (b - a) * (x ** (1.0 / max(1e-3, s)))