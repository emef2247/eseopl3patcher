from __future__ import annotations
import json
from dataclasses import dataclass
from typing import Dict, List, Tuple, Any, Optional

# Simplified types shared with existing loader
@dataclass
class OpllOpParams:
    am: int; vib: int; ksr: int; mult: int; ksl: int; tl: int
    ar: int; dr: int; sl: int; rr: int; fb: int; wf: int

@dataclass
class OpllChannelState:
    ch: int
    fnum: int
    blk: int
    ko: int
    vol: int  # channel volume (if available)
    mod: OpllOpParams
    car: OpllOpParams
    is_rhythm: bool = False
    rhythm_mask: int = 0  # bitfield for BD/SD/TOM/TC/HH

class OpllToOpl3Mapper:
    def __init__(self, mapping_tables: Dict[str, Any]):
        self.tab = mapping_tables
        self.rate_map_ar = mapping_tables["rate_map"]["ar"]   # list len 16
        self.rate_map_dr = mapping_tables["rate_map"]["dr"]   # list len 16
        self.rate_map_rr = mapping_tables["rate_map"]["rr"]   # list len 16
        self.fb_map     = mapping_tables["fb_map"]            # list len 8 or dict
        self.wf_map     = mapping_tables["wave_map"]          # { "opll_wf0": 0, "opll_wf1": 1 }
        self.ksr_rule   = mapping_tables["ksr_rule"]          # {"opl3_ksr_on": true}
        self.tl_scale   = mapping_tables["tl_scale"]          # {"vol_to_tl_car": [0..15]->0..63, "tl_mod_passthru": true}
        self.rhythm     = mapping_tables["rhythm"]             # routing/presets

    def map_rate_ar(self, r: int) -> int:
        return int(self.rate_map_ar[max(0, min(15, r))])

    def map_rate_dr(self, r: int) -> int:
        return int(self.rate_map_dr[max(0, min(15, r))])

    def map_rate_rr(self, r: int) -> int:
        return int(self.rate_map_rr[max(0, min(15, r))])

    def map_fb(self, fb: int) -> int:
        return int(self.fb_map[max(0, min(len(self.fb_map)-1, fb))])

    def map_wave(self, opll_wf: int) -> int:
        return int(self.wf_map["opll_wf1"] if opll_wf else self.wf_map["opll_wf0"])

    def vol_to_tl_car(self, vol: int) -> int:
        # Map OPLL channel volume (0..15) to OPL3 TL (0..63). Use provided LUT.
        lut = self.tl_scale["vol_to_tl_car"]
        return int(lut[max(0, min(len(lut)-1, vol))])

    def pass_tl_mod(self, tl: int) -> int:
        # Optionally scale mod TL too
        if self.tl_scale.get("scale_tl_mod", False):
            a = float(self.tl_scale.get("tl_mod_a", 1.0))
            b = float(self.tl_scale.get("tl_mod_b", 0.0))
            v = int(round(a * tl + b))
            return max(0, min(63, v))
        return max(0, min(63, tl))

    def map_channel(self, st: OpllChannelState) -> Dict[str, Any]:
        """Return a dict describing OPL3 writes for this channel at its current state."""
        out: Dict[str, Any] = {"ch": st.ch, "regs": []}

        # Frequency/Block mapping
        fnum_opl3 = max(0, min(1023, st.fnum << self.tab["fnum_shift"]))
        blk_opl3  = max(0, min(7, st.blk))

        # Operator params mapping (2-op only: mod->car connection)
        def op_to_opl3(op: OpllOpParams, is_mod: bool) -> Dict[str, int]:
            return {
                "am": op.am,
                "vib": op.vib,
                "ksr": 1 if (op.ksr and self.ksr_rule.get("opl3_ksr_on", True)) else 0,
                "mult": op.mult & 0x0F,
                "ksl": op.ksl & 0x03,
                "tl": self.pass_tl_mod(op.tl) if is_mod else self.vol_to_tl_car(st.vol),
                "ar": self.map_rate_ar(op.ar),
                "dr": self.map_rate_dr(op.dr),
                "sl": op.sl & 0x0F,
                "rr": self.map_rate_rr(op.rr),
                "wf": self.map_wave(op.wf),
                "fb": self.map_fb(op.fb) if is_mod else 0,
            }

        mod = op_to_opl3(st.mod, is_mod=True)
        car = op_to_opl3(st.car, is_mod=False)

        out["voice"] = {"fnum": fnum_opl3, "blk": blk_opl3, "ko": st.ko}
        out["ops"] = {"mod": mod, "car": car}
        out["alg"] = 0  # fixed 2-op (mod->car)

        # Rhythm handling (if this channel is rhythm source)
        if st.is_rhythm:
            out["rhythm"] = self.map_rhythm(st.rhythm_mask)
        return out

    def map_rhythm(self, mask: int) -> Dict[str, Any]:
        # Map OPLL rhythm mask to OPL3 rhythm bits and per-instrument patches
        # mask bits: 0:BD 1:SD 2:TOM 3:TC 4:HH (example)
        r = {"bd": 0, "sd": 0, "tom": 0, "tc": 0, "hh": 0, "patches": {}}
        names = ["bd", "sd", "tom", "tc", "hh"]
        for i, nm in enumerate(names):
            if mask & (1 << i):
                r[nm] = 1
                if nm in self.rhythm["patches"]:
                    r["patches"][nm] = self.rhythm["patches"][nm]
        return r