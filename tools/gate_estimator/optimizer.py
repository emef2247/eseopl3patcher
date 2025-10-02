from __future__ import annotations
from dataclasses import dataclass
from typing import List, Dict, Any, Tuple, Optional
import csv
import json
import os
import glob

from .model import YM2413EnvelopeModel, PatchParams, NoteContext
from .csv_loader import load_pattern_from_csv

@dataclass
class GateEstimate:
    pattern: str
    channel: int
    gate: float
    avg_residual_db: float
    overlap_events: int
    avg_sustain_loss: float
    score: float

class GateEstimator:
    def __init__(self, model: Optional[YM2413EnvelopeModel] = None):
        self.model = model or YM2413EnvelopeModel()

    def estimate_for_sequence(self, patch: PatchParams, notes: List[NoteContext]) -> Tuple[float, Dict[str, Any]]:
        return self.model.choose_gate_grid(patch, notes)

    # --- I/O helpers (repo-specific conventions; adjust after schema review) ---

    def load_pattern_data(self, ir_root: str) -> Dict[Tuple[str, int], Dict[str, Any]]:
        """
        Expecting under ir_root a manifest JSON named patterns.json.
        Returns a dict keyed by (pattern_name, channel) with:
          {
            "patch": PatchParams(...),
            "notes": [NoteContext, ...]
          }
        """
        manifest_path = os.path.join(ir_root, "patterns.json")
        if not os.path.exists(manifest_path):
            raise FileNotFoundError(f"Manifest not found: {manifest_path}")
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)

        data: Dict[Tuple[str, int], Dict[str, Any]] = {}
        for entry in manifest.get("patterns", []):
            patt = entry["pattern"]
            ch = int(entry["channel"])
            patch = PatchParams(
                ar=entry["patch"]["ar"],
                dr=entry["patch"]["dr"],
                sl=entry["patch"]["sl"],
                rr=entry["patch"]["rr"],
                ksr=bool(entry["patch"].get("ksr", True)),
            )
            notes: List[NoteContext] = []
            for n in entry["notes"]:
                notes.append(NoteContext(
                    fnum=int(n["fnum"]),
                    blk=int(n["blk"]),
                    t_on=float(n["t_on"]),
                    ioi=float(n["ioi"]),
                ))
            data[(patt, ch)] = {"patch": patch, "notes": notes}
        return data

    def load_csv_directory(self, csv_dir: str, pattern: str = "*.csv", inst: int = 2) -> Dict[Tuple[str, int], Dict[str, Any]]:
        """
        Load all CSV files matching pattern from csv_dir.
        Uses default YM2413 patch parameters based on inst.
        
        Args:
            csv_dir: directory containing durations CSV files
            pattern: glob pattern for CSV files (default: *.csv)
            inst: YM2413 instrument number (0-15)
        
        Returns:
            Dict keyed by (pattern_name, channel)
        """
        search_path = os.path.join(csv_dir, pattern)
        csv_files = glob.glob(search_path)
        if not csv_files:
            raise FileNotFoundError(f"No CSV files found matching: {search_path}")
        
        data: Dict[Tuple[str, int], Dict[str, Any]] = {}
        for csv_path in csv_files:
            try:
                pattern_data = load_pattern_from_csv(csv_path, inst=inst)
                data.update(pattern_data)
            except Exception as e:
                print(f"Warning: Failed to load {csv_path}: {e}")
                continue
        
        return data

    def write_csv(self, out_path: str, rows: List[GateEstimate]) -> None:
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["pattern", "channel", "gate", "avg_residual_db", "overlap_events", "avg_sustain_loss", "score"])
            for r in rows:
                w.writerow([r.pattern, r.channel, f"{r.gate:.3f}", f"{r.avg_residual_db:.2f}", r.overlap_events, f"{r.avg_sustain_loss:.5f}", f"{r.score:.6f}"])