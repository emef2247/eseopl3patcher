from __future__ import annotations
import csv
import os
from typing import Dict, List, Tuple

from .model import PatchParams, NoteContext


# YM2413 ROM patch presets (AR, DR, SL, RR, KSR)
# Source: YM2413 datasheet and emulator references (MAME, ymfm)
# Patch 0 is user-defined, patches 1-15 are ROM presets
YM2413_ROM_PATCHES = {
    # patch_num: (ar_mod, dr_mod, sl_mod, rr_mod, ksr_mod, ar_car, dr_car, sl_car, rr_car, ksr_car)
    0: (15, 7, 0, 7, False, 15, 7, 0, 7, False),  # User-defined default
    1: (15, 7, 0, 1, False, 15, 8, 0, 7, False),  # Violin
    2: (15, 14, 0, 7, False, 15, 8, 0, 8, False), # Guitar
    3: (15, 0, 7, 8, False, 15, 7, 7, 8, False),  # Piano
    4: (15, 7, 0, 8, False, 15, 8, 0, 8, False),  # Flute
    5: (15, 9, 0, 8, False, 15, 8, 0, 8, False),  # Clarinet
    6: (15, 4, 0, 8, False, 15, 8, 0, 8, False),  # Oboe
    7: (15, 7, 0, 2, False, 15, 9, 0, 8, False),  # Trumpet
    8: (15, 8, 0, 8, False, 15, 8, 0, 8, False),  # Organ
    9: (15, 12, 0, 8, False, 15, 12, 0, 8, False), # Horn
    10: (15, 7, 0, 8, False, 15, 8, 0, 8, False), # Synthesizer
    11: (15, 9, 0, 9, False, 15, 7, 0, 7, False), # Harpsichord
    12: (15, 5, 0, 9, False, 15, 5, 0, 7, False), # Vibraphone
    13: (15, 0, 15, 10, False, 15, 0, 15, 10, False), # Synth Bass
    14: (15, 11, 0, 11, False, 15, 9, 0, 7, False), # Acoustic Bass
    15: (15, 11, 0, 7, False, 15, 11, 0, 8, False), # Electric Guitar
}


def get_patch_params(inst: int) -> PatchParams:
    """
    Get carrier EG parameters for YM2413 ROM patch.
    For simplicity, we use carrier parameters as they dominate the output envelope.
    """
    inst = max(0, min(15, inst))
    params = YM2413_ROM_PATCHES.get(inst, YM2413_ROM_PATCHES[0])
    # Use carrier (last 5 values)
    ar_car, dr_car, sl_car, rr_car, ksr_car = params[5], params[6], params[7], params[8], params[9]
    return PatchParams(ar=ar_car, dr=dr_car, sl=sl_car, rr=rr_car, ksr=ksr_car)


def load_durations_csv(csv_path: str) -> Dict[int, List[NoteContext]]:
    """
    Load YM2413 durations CSV and extract note contexts per channel.
    Returns dict: {channel -> [NoteContext, ...]}
    
    Expected CSV columns (minimum):
    - ch: channel number
    - t_on_s: note on time (seconds)
    - interval_to_next_on_s: time to next onset (IOI)
    - blk_on: block (octave)
    - fnum_on: frequency number
    """
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"CSV not found: {csv_path}")
    
    channels: Dict[int, List[NoteContext]] = {}
    
    with open(csv_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                ch = int(float(row["ch"]))
                t_on = float(row["t_on_s"])
                
                # Handle missing or empty interval_to_next_on_s (last note in sequence)
                ioi_str = row.get("interval_to_next_on_s", "").strip()
                if ioi_str and ioi_str != "":
                    ioi = float(ioi_str)
                else:
                    # For last note, use a reasonable default based on previous IOI or typical note length
                    ioi = 0.25
                
                blk = int(float(row["blk_on"]))
                fnum = int(float(row["fnum_on"]))
                
                note = NoteContext(fnum=fnum, blk=blk, t_on=t_on, ioi=ioi)
                if ch not in channels:
                    channels[ch] = []
                channels[ch].append(note)
            except (KeyError, ValueError) as e:
                # Skip malformed rows
                continue
    
    return channels


def load_pattern_from_csv(csv_path: str, inst: int = 2) -> Dict[Tuple[str, int], Dict[str, any]]:
    """
    Load a durations CSV and return in pattern format expected by optimizer.
    Uses default patch parameters based on instrument number.
    
    Args:
        csv_path: path to durations CSV
        inst: YM2413 instrument/patch number (0-15), default 2 (Guitar)
    
    Returns:
        Dict keyed by (pattern_name, channel) with patch and notes
    """
    basename = os.path.basename(csv_path).replace("_durations.csv", "")
    channels = load_durations_csv(csv_path)
    patch = get_patch_params(inst)
    
    result = {}
    for ch, notes in channels.items():
        if notes:  # Only include channels with notes
            result[(basename, ch)] = {
                "patch": patch,
                "notes": notes,
            }
    
    return result
