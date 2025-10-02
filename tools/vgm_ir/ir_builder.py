from typing import Dict, List, Optional
from .ir_types import Event, Note, IR
from .decoders_psg_scc import (
    psg_freq_from_regs,
    psg_mode_from_reg7,
    psg_tone_hz_from_reg,
    scc_abs_addr_from_port,
    scc_decode_abs_addr,
)


class IRBuilder:
    def __init__(self, sample_rate: int):
        self.sample_rate = sample_rate
        self.events: List[Event] = []
        self.notes: List[Note] = []

        # PSG state
        self.psg_fa = [0, 0, 0]
        self.psg_fb = [0, 0, 0]
        self.psg_vvc = 0xB8  # typical: portB out(1), portA in(0)
        self.psg_vol = [0, 0, 0]
        self.psg_open: Dict[int, Note] = {}

        # SCC state
        self.scc_vol = [0, 0, 0, 0]
        self.scc_enable = 0  # bitfield
        self.scc_open: Dict[int, Note] = {}

        # YM2413 (OPLL) — IRはイベント記録のみ（スナップショットCSVで詳細化）
        # ここでは特別なノート推定は行わない（バックエンドへ委譲）
        # 必要に応じて状態は後で追加可能

    def samples_to_time(self, samples: int) -> float:
        return samples / float(self.sample_rate)

    def _append_event(
        self,
        samples: int,
        chip: str,
        channel: Optional[int],
        kind: str,
        address: Optional[int],
        value: Optional[int],
        extras: Optional[Dict] = None,
    ):
        self.events.append(
            Event(
                time_s=self.samples_to_time(samples),
                samples=samples,
                chip=chip,
                channel=channel,
                kind=kind,
                address=address,
                value=value,
                extras=extras or {},
            )
        )

    # -----------------------------
    # PSG handling (unchanged)
    # -----------------------------
    def on_psg_write(self, abs_samples: int, aa: int, dd: int):
        ch_for_regs = {0: 0, 1: 0, 2: 1, 3: 1, 4: 2, 5: 2}
        extras = {"reg": aa}
        if aa in ch_for_regs:
            ch = ch_for_regs[aa]
            if aa % 2 == 0:
                self.psg_fa[ch] = dd & 0xFF
            else:
                self.psg_fb[ch] = dd & 0xFF

            reg16 = psg_freq_from_regs(self.psg_fa[ch], self.psg_fb[ch])
            hz = psg_tone_hz_from_reg(reg16)
            extras.update({"channel": ch, "reg16": reg16, "tone_hz": hz})
            self._append_event(abs_samples, "PSG", ch, "reg-write", aa, dd, extras)
            self._psg_note_heuristic(abs_samples, ch, reg16)
            return

        if aa == 7:
            self.psg_vvc = dd & 0xFF
            modes = {c: psg_mode_from_reg7(self.psg_vvc, c) for c in range(3)}
            extras.update({"reg7": self.psg_vvc, "modes": modes})
            self._append_event(abs_samples, "PSG", None, "reg-write", aa, dd, extras)
            for ch in range(3):
                mode = modes[ch]
                if mode in (0, 2):  # tone disabled
                    self._psg_end_if_open(abs_samples, ch, cause="tone-muted")
            return

        if aa in (8, 9, 10):
            ch = aa - 8
            old = self.psg_vol[ch]
            self.psg_vol[ch] = dd & 0x1F
            vol4 = self.psg_vol[ch] & 0x0F
            va = (self.psg_vol[ch] >> 4) & 1
            mode = psg_mode_from_reg7(self.psg_vvc, ch)
            extras.update({"channel": ch, "vol4": vol4, "VA": va, "mode": mode})
            self._append_event(abs_samples, "PSG", ch, "reg-write", aa, dd, extras)
            if vol4 > 0 and mode in (1, 3) and (old & 0x0F) == 0:
                reg16 = psg_freq_from_regs(self.psg_fa[ch], self.psg_fb[ch])
                hz = psg_tone_hz_from_reg(reg16)
                self._psg_start(abs_samples, ch, hz, vol4, cause="vol-rise")
            if vol4 == 0:
                self._psg_end_if_open(abs_samples, ch, cause="vol-zero")
            return

        self._append_event(abs_samples, "PSG", None, "reg-write", aa, dd, extras)

    def _psg_start(self, abs_samples: int, ch: int, hz: float, vol: int, cause: str):
        if ch in self.psg_open:
            self._psg_end_if_open(abs_samples, ch, cause="overlap")
        n = Note(
            chip="PSG",
            channel=ch,
            t_on=self.samples_to_time(abs_samples),
            t_off=self.samples_to_time(abs_samples),
            pitch_hz=hz,
            volume=vol,
            cause=cause,
            meta={},
        )
        self.psg_open[ch] = n

    def _psg_end_if_open(self, abs_samples: int, ch: int, cause: str):
        if ch in self.psg_open:
            n = self.psg_open.pop(ch)
            n.t_off = self.samples_to_time(abs_samples)
            n.cause = n.cause or cause
            self.notes.append(n)

    def _psg_note_heuristic(self, abs_samples: int, ch: int, reg16: int):
        if ch in self.psg_open:
            hz = psg_tone_hz_from_reg(reg16)
            note = self.psg_open[ch]
            if note.pitch_hz and hz > 0:
                delta = abs(hz - note.pitch_hz) / max(1e-9, note.pitch_hz)
                if delta >= 0.2:
                    self._psg_end_if_open(abs_samples, ch, cause="freq-change")
                    self._psg_start(abs_samples, ch, hz, note.volume or 0, cause="freq-change")
                else:
                    note.pitch_hz = hz

    # -----------------------------
    # SCC handling (unchanged)
    # -----------------------------
    def on_scc_write(self, abs_samples: int, pp: int, aa: int, dd: int):
        abs_addr = scc_abs_addr_from_port(pp, aa)
        info = scc_decode_abs_addr(abs_addr)

        extras = {"port": pp, "reg": aa, "abs_addr": abs_addr}
        if info.kind == "wtb":
            extras.update({"wtb_index": info.wtb_index})
            self._append_event(abs_samples, "SCC", info.ch, "wtb-write", abs_addr, dd, extras)
            return

        if info.kind in ("f1", "f2"):
            extras.update({"kind": info.kind})
            self._append_event(abs_samples, "SCC", info.ch, "reg-write", abs_addr, dd, extras)
            return

        if info.kind == "vol":
            ch = info.ch if info.ch is not None else -1
            old = self.scc_vol[ch]
            self.scc_vol[ch] = dd & 0xFF
            extras.update({"channel": ch, "vol": self.scc_vol[ch]})
            self._append_event(abs_samples, "SCC", ch, "reg-write", abs_addr, dd, extras)
            if old == 0 and self.scc_vol[ch] > 0 and ((self.scc_enable >> ch) & 1) == 1:
                self._scc_start(abs_samples, ch, cause="vol-rise")
            if self.scc_vol[ch] == 0:
                self._scc_end_if_open(abs_samples, ch, cause="vol-zero")
            return

        if info.kind == "en":
            self.scc_enable = dd & 0xFF
            extras.update({"enable_bits": self.scc_enable})
            self._append_event(abs_samples, "SCC", None, "reg-write", abs_addr, dd, extras)
            for ch in range(4):
                bit = (self.scc_enable >> ch) & 1
                if bit == 0:
                    self._scc_end_if_open(abs_samples, ch, cause="enable-off")
                elif bit == 1 and self.scc_vol[ch] > 0:
                    self._scc_start(abs_samples, ch, cause="enable-on")
            return

        self._append_event(abs_samples, "SCC", info.ch, "reg-write", abs_addr, dd, extras)

    def _scc_start(self, abs_samples: int, ch: int, cause: str):
        if ch in self.scc_open:
            self._scc_end_if_open(abs_samples, ch, cause="overlap")
        n = Note(
            chip="SCC",
            channel=ch,
            t_on=self.samples_to_time(abs_samples),
            t_off=self.samples_to_time(abs_samples),
            pitch_hz=None,
            volume=self.scc_vol[ch],
            cause=cause,
            meta={},
        )
        self.scc_open[ch] = n

    def _scc_end_if_open(self, abs_samples: int, ch: int, cause: str):
        if ch in self.scc_open:
            n = self.scc_open.pop(ch)
            n.t_off = self.samples_to_time(abs_samples)
            n.cause = n.cause or cause
            self.notes.append(n)

    # -----------------------------
    # OPLL handling (new)
    # -----------------------------
    def on_opll_write(self, abs_samples: int, aa: int, dd: int):
        # IR としては 1書込=1イベントを記録（詳細な展開はCSV側で）
        extras = {"reg": aa}
        self._append_event(abs_samples, "YM2413", None, "reg-write", aa, dd, extras)

    # -----------------------------
    # Finalize
    # -----------------------------
    def build(self, header: Dict) -> IR:
        last_samples = header.get("total_samples", 0)
        for ch in list(self.psg_open.keys()):
            self._psg_end_if_open(last_samples, ch, cause="eof")
        for ch in list(self.scc_open.keys()):
            self._scc_end_if_open(last_samples, ch, cause="eof")
        return IR(header=header, events=self.events, notes=self.notes)