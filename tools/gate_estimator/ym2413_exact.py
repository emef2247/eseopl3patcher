#!/usr/bin/env python3
"""
YM2413 Exact Envelope Generator Model

Table-driven EG model using ymfm/MAME/EMU2413 envelope timing tables.
Provides compatible API with parametric model for use in gate estimation workflows.
"""

import json
import math
from typing import Dict, Tuple, Optional


class YM2413EnvelopeExact:
    """
    Exact envelope generator model for YM2413 using table-driven timing.
    
    Reads attack/decay/release times from JSON tables and computes envelope
    behavior including Key Scale Rate (KSR) effects.
    """
    
    def __init__(self, tables_path: str):
        """
        Initialize the exact EG model from JSON tables.
        
        Args:
            tables_path: Path to JSON file containing eg_times_ms and ksr tables
        """
        with open(tables_path, 'r') as f:
            self.tables = json.load(f)
        
        # Extract timing tables (in milliseconds)
        eg_times = self.tables['eg_times_ms']
        self.attack_times_ms = eg_times['attack']    # 16 values
        self.decay_times_ms = eg_times['decay']      # 16 values
        self.release_times_ms = eg_times['release']  # 16 values
        
        # Extract KSR shift values (per-block)
        self.ksr_shifts = self.tables['ksr']['per_blk_shift']  # 8 values
        
        # Clock frequency for precise timing
        self.clock_hz = self.tables.get('clock_hz', 3579545)
        
        # Validate table sizes
        assert len(self.attack_times_ms) == 16, "Attack table must have 16 entries"
        assert len(self.decay_times_ms) == 16, "Decay table must have 16 entries"
        assert len(self.release_times_ms) == 16, "Release table must have 16 entries"
        assert len(self.ksr_shifts) == 8, "KSR table must have 8 entries"
    
    def _apply_ksr(self, rate: int, block: int, ksr_enable: bool) -> int:
        """
        Apply Key Scale Rate to effective rate.
        
        Args:
            rate: Base rate value (0-15)
            block: FNUM block (0-7)
            ksr_enable: Whether KSR is enabled for this operator
        
        Returns:
            Effective rate after KSR shift
        """
        if not ksr_enable or rate == 0:
            return rate
        
        shift = self.ksr_shifts[block] if block < len(self.ksr_shifts) else 0
        effective_rate = min(rate + shift, 15)
        return effective_rate
    
    def _get_attack_time_ms(self, ar: int, block: int, ksr: bool) -> float:
        """Get attack time in milliseconds."""
        effective_ar = self._apply_ksr(ar, block, ksr)
        return self.attack_times_ms[effective_ar]
    
    def _get_decay_time_ms(self, dr: int, block: int, ksr: bool) -> float:
        """Get decay time in milliseconds."""
        effective_dr = self._apply_ksr(dr, block, ksr)
        return self.decay_times_ms[effective_dr]
    
    def _get_release_time_ms(self, rr: int, block: int, ksr: bool) -> float:
        """Get release time in milliseconds."""
        effective_rr = self._apply_ksr(rr, block, ksr)
        return self.release_times_ms[effective_rr]
    
    def residual_at(self, gate_samples: int, sample_rate: float,
                   ar: int, dr: int, rr: int, sl: int,
                   block: int = 4, ksr: bool = False) -> float:
        """
        Compute residual envelope level at gate end (key-off).
        
        Args:
            gate_samples: Duration of gate (key-on) in samples
            sample_rate: Audio sample rate (Hz)
            ar: Attack Rate (0-15)
            dr: Decay Rate (0-15)
            rr: Release Rate (0-15)
            sl: Sustain Level (0-15)
            block: FNUM block for KSR (0-7)
            ksr: Key Scale Rate enable
        
        Returns:
            Residual envelope level at gate end (0.0=silent, 1.0=full)
        """
        gate_ms = (gate_samples / sample_rate) * 1000.0
        
        # Get effective times with KSR
        attack_ms = self._get_attack_time_ms(ar, block, ksr)
        decay_ms = self._get_decay_time_ms(dr, block, ksr)
        
        # Convert sustain level to linear scale (sl=15 is essentially silent)
        # Sustain levels: 0=0dB, 1=-3dB, ..., 15=-93dB (effectively silent)
        sl_linear = 1.0 if sl == 0 else math.pow(10.0, -sl * 3.0 / 20.0)
        if sl >= 15:
            sl_linear = 0.0  # Treat sl=15 as silent
        
        # Envelope phases: Attack -> Decay -> Sustain
        time_elapsed = 0.0
        
        # Attack phase
        if attack_ms > 0 and time_elapsed + attack_ms < gate_ms:
            time_elapsed += attack_ms
            level = 1.0  # Peak after attack
        elif attack_ms > 0:
            # Still in attack at gate end
            progress = gate_ms / attack_ms
            return min(progress, 1.0)
        else:
            level = 1.0  # Instant attack
        
        # Decay phase
        if decay_ms > 0 and time_elapsed + decay_ms < gate_ms:
            time_elapsed += decay_ms
            level = sl_linear  # Reached sustain level
        elif decay_ms > 0 and time_elapsed < gate_ms:
            # In decay phase at gate end
            decay_progress = (gate_ms - time_elapsed) / decay_ms
            level = 1.0 - decay_progress * (1.0 - sl_linear)
            return max(level, sl_linear)
        
        # Sustain phase - level remains constant
        return level
    
    def choose_gate_grid(self, transitions: list, sample_rate: float,
                        ar: int, dr: int, rr: int, sl: int,
                        block: int = 4, ksr: bool = False,
                        gate_candidates: Optional[list] = None) -> Tuple[int, Dict]:
        """
        Choose optimal gate value from grid search over note transitions.
        
        Args:
            transitions: List of (note_on_sample, note_off_sample, duration_samples) tuples
            sample_rate: Audio sample rate (Hz)
            ar, dr, rr, sl: Envelope parameters
            block: FNUM block for KSR
            ksr: Key Scale Rate enable
            gate_candidates: Optional list of gate values to try (default: auto-generate)
        
        Returns:
            Tuple of (best_gate_samples, metrics_dict)
            metrics_dict contains: {'score', 'residuals', 'mean_residual', ...}
            
            When there are no transitions, returns default metrics with score=0
        """
        # Handle case with no transitions (robustness fix)
        if not transitions or len(transitions) == 0:
            default_gate = 8192 if gate_candidates is None else gate_candidates[0]
            return default_gate, {
                'score': 0.0,
                'residuals': [],
                'mean_residual': 0.0,
                'std_residual': 0.0,
                'num_transitions': 0,
                'note': 'No transitions available for estimation'
            }
        
        # Default gate candidates if not provided
        if gate_candidates is None:
            gate_candidates = [2048, 4096, 6144, 8192, 10240, 12288, 14336, 16384]
        
        best_gate = gate_candidates[0]
        best_score = float('-inf')
        best_metrics = {}
        
        for gate in gate_candidates:
            residuals = []
            
            for (note_on, note_off, duration) in transitions:
                # Compute residual at note-off time
                residual = self.residual_at(
                    gate_samples=gate,
                    sample_rate=sample_rate,
                    ar=ar, dr=dr, rr=rr, sl=sl,
                    block=block, ksr=ksr
                )
                residuals.append(residual)
            
            # Scoring: prefer gates where residual is close to target (e.g., 0.1-0.3)
            # Penalize both too high (incomplete decay) and too low (over-decay)
            target_residual = 0.2
            errors = [(r - target_residual) ** 2 for r in residuals]
            mean_error = sum(errors) / len(errors) if errors else 0.0
            score = -mean_error  # Higher is better
            
            mean_residual = sum(residuals) / len(residuals) if residuals else 0.0
            std_residual = 0.0
            if len(residuals) > 1:
                variance = sum((r - mean_residual) ** 2 for r in residuals) / len(residuals)
                std_residual = math.sqrt(variance)
            
            metrics = {
                'score': score,
                'residuals': residuals,
                'mean_residual': mean_residual,
                'std_residual': std_residual,
                'num_transitions': len(transitions),
                'gate_value': gate
            }
            
            if score > best_score:
                best_score = score
                best_gate = gate
                best_metrics = metrics
        
        return best_gate, best_metrics


def load_model(tables_path: str) -> YM2413EnvelopeExact:
    """
    Convenience function to load the exact EG model.
    
    Args:
        tables_path: Path to JSON tables file
    
    Returns:
        Initialized YM2413EnvelopeExact instance
    """
    return YM2413EnvelopeExact(tables_path)


if __name__ == '__main__':
    # Simple test/demo
    import sys
    
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <tables.json>")
        sys.exit(1)
    
    model = load_model(sys.argv[1])
    
    # Test parameters
    ar, dr, rr, sl = 15, 4, 7, 4
    block = 4
    ksr = False
    sample_rate = 44100.0
    
    print(f"YM2413 Exact EG Model Test")
    print(f"Tables: {sys.argv[1]}")
    print(f"Params: AR={ar}, DR={dr}, RR={rr}, SL={sl}, Block={block}, KSR={ksr}")
    print()
    
    # Test residual computation
    test_gates = [4096, 8192, 12288, 16384]
    print("Residual levels at different gate lengths:")
    for gate in test_gates:
        residual = model.residual_at(gate, sample_rate, ar, dr, rr, sl, block, ksr)
        gate_ms = (gate / sample_rate) * 1000.0
        print(f"  Gate: {gate:5d} samples ({gate_ms:6.2f} ms) -> Residual: {residual:.4f}")
    
    print()
    
    # Test choose_gate_grid with sample transitions
    transitions = [
        (0, 8000, 8000),
        (10000, 18000, 8000),
        (20000, 28000, 8000),
    ]
    print(f"Testing grid search with {len(transitions)} transitions...")
    best_gate, metrics = model.choose_gate_grid(transitions, sample_rate, ar, dr, rr, sl, block, ksr)
    print(f"Best gate: {best_gate} samples")
    print(f"Score: {metrics['score']:.6f}")
    print(f"Mean residual: {metrics['mean_residual']:.4f}")
    print(f"Std residual: {metrics['std_residual']:.4f}")
    
    # Test with no transitions (robustness check)
    print()
    print("Testing with no transitions (robustness)...")
    best_gate, metrics = model.choose_gate_grid([], sample_rate, ar, dr, rr, sl, block, ksr)
    print(f"Best gate: {best_gate} samples")
    print(f"Score: {metrics['score']:.6f}")
    print(f"Note: {metrics.get('note', 'N/A')}")
