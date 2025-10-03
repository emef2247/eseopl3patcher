#!/usr/bin/env python3
"""
Gate Estimation Sweep Runner

Runs gate estimation across multiple scenarios (patch/channel combinations)
with coarse-to-fine sweeps. Handles robustness when sequences have no
note-to-note transitions.
"""

import argparse
import csv
import json
import os
import sys
from typing import Dict, List, Tuple, Optional


def load_sequence_data(sequence_path: str) -> Dict:
    """
    Load sequence/timeline data for gate estimation.
    
    Args:
        sequence_path: Path to JSON file with note events
    
    Returns:
        Dict with sequence metadata and note events
    """
    with open(sequence_path, 'r') as f:
        return json.load(f)


def extract_transitions(sequence: Dict, channel: Optional[int] = None) -> List[Tuple[int, int, int]]:
    """
    Extract note-to-note transitions from sequence data.
    
    Args:
        sequence: Sequence data dict with 'events' list
        channel: Optional channel filter
    
    Returns:
        List of (note_on_sample, note_off_sample, duration_samples) tuples
    """
    events = sequence.get('events', [])
    transitions = []
    
    # Simple extraction: look for consecutive note events
    prev_event = None
    for event in events:
        if channel is not None and event.get('channel') != channel:
            continue
        
        if event.get('type') == 'note_on':
            if prev_event and prev_event.get('type') == 'note_off':
                # Transition from previous note to this one
                note_on = event.get('sample', 0)
                note_off = prev_event.get('sample', 0)
                duration = note_on - note_off
                if duration > 0:
                    transitions.append((note_off, note_on, duration))
            prev_event = event
        elif event.get('type') == 'note_off':
            prev_event = event
    
    return transitions


def run_sweep(model, transitions: List, sample_rate: float,
              patch_params: Dict, gate_grid: List[int]) -> Dict:
    """
    Run gate sweep for a single scenario.
    
    Args:
        model: EG model instance (param or exact)
        transitions: List of note transitions
        sample_rate: Audio sample rate
        patch_params: Dict with 'ar', 'dr', 'rr', 'sl', 'block', 'ksr'
        gate_grid: List of gate values to try
    
    Returns:
        Dict with sweep results including best_gate and metrics
    """
    ar = patch_params.get('ar', 15)
    dr = patch_params.get('dr', 4)
    rr = patch_params.get('rr', 7)
    sl = patch_params.get('sl', 4)
    block = patch_params.get('block', 4)
    ksr = patch_params.get('ksr', False)
    
    # Call model's choose_gate_grid
    # This now handles the case when transitions is empty
    best_gate, metrics = model.choose_gate_grid(
        transitions=transitions,
        sample_rate=sample_rate,
        ar=ar, dr=dr, rr=rr, sl=sl,
        block=block, ksr=ksr,
        gate_candidates=gate_grid
    )
    
    # Robustness: check if we got valid metrics with 'score' key
    # If not, it means there were no transitions and we should handle gracefully
    if 'score' not in metrics:
        # Fallback: return default metrics
        return {
            'best_gate': best_gate,
            'score': 0.0,
            'mean_residual': 0.0,
            'std_residual': 0.0,
            'num_transitions': 0,
            'status': 'no_transitions'
        }
    
    return {
        'best_gate': best_gate,
        'score': metrics['score'],
        'mean_residual': metrics.get('mean_residual', 0.0),
        'std_residual': metrics.get('std_residual', 0.0),
        'num_transitions': metrics.get('num_transitions', 0),
        'status': 'ok' if metrics.get('num_transitions', 0) > 0 else 'no_transitions'
    }


def sweep_scenarios(model, scenarios: List[Dict], sample_rate: float,
                   gate_grid: List[int], output_path: str):
    """
    Run sweeps across multiple scenarios and save results.
    
    Args:
        model: EG model instance
        scenarios: List of scenario dicts with 'name', 'sequence_path', 'patch_params', etc.
        sample_rate: Audio sample rate
        gate_grid: Gate values to try
        output_path: CSV output path for results
    """
    results = []
    
    for scenario in scenarios:
        name = scenario.get('name', 'unknown')
        sequence_path = scenario.get('sequence_path')
        patch_params = scenario.get('patch_params', {})
        channel = scenario.get('channel')
        
        print(f"Processing scenario: {name}")
        
        if not sequence_path or not os.path.exists(sequence_path):
            print(f"  [SKIP] Sequence file not found: {sequence_path}")
            continue
        
        # Load and extract transitions
        try:
            sequence = load_sequence_data(sequence_path)
            transitions = extract_transitions(sequence, channel)
            print(f"  Extracted {len(transitions)} transitions")
            
            # Run sweep
            result = run_sweep(model, transitions, sample_rate, patch_params, gate_grid)
            
            # Store result
            result['scenario'] = name
            result['patch'] = patch_params.get('patch', 'unknown')
            result['channel'] = channel if channel is not None else 'all'
            results.append(result)
            
            print(f"  Best gate: {result['best_gate']}, Score: {result['score']:.6f}, Status: {result['status']}")
            
        except Exception as e:
            print(f"  [ERROR] {e}")
            # Store error result
            results.append({
                'scenario': name,
                'patch': patch_params.get('patch', 'unknown'),
                'channel': channel if channel is not None else 'all',
                'best_gate': gate_grid[0] if gate_grid else 8192,
                'score': 0.0,
                'mean_residual': 0.0,
                'std_residual': 0.0,
                'num_transitions': 0,
                'status': f'error: {str(e)}'
            })
    
    # Write results to CSV
    if results:
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        with open(output_path, 'w', newline='', encoding='utf-8') as f:
            fieldnames = ['scenario', 'patch', 'channel', 'best_gate', 'score',
                         'mean_residual', 'std_residual', 'num_transitions', 'status']
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(results)
        print(f"\n[OK] Results saved to: {output_path}")
    else:
        print("\n[WARN] No results to save")


def main():
    parser = argparse.ArgumentParser(description='Run gate estimation sweep')
    parser.add_argument('--scenarios', required=True, help='JSON file with scenarios list')
    parser.add_argument('--eg-model', choices=['param', 'exact'], default='exact',
                       help='EG model type')
    parser.add_argument('--eg-tables', default=None,
                       help='Path to EG tables JSON (required for exact model)')
    parser.add_argument('--sample-rate', type=float, default=44100.0,
                       help='Audio sample rate (Hz)')
    parser.add_argument('--gate-grid', default='2048,4096,6144,8192,10240,12288,14336,16384',
                       help='Comma-separated gate values to try')
    parser.add_argument('--output', required=True, help='Output CSV path')
    
    args = parser.parse_args()
    
    # Parse gate grid
    gate_grid = [int(x.strip()) for x in args.gate_grid.split(',')]
    
    # Load model
    if args.eg_model == 'exact':
        if not args.eg_tables:
            print("[ERROR] --eg-tables required for exact model")
            sys.exit(1)
        
        # Import exact model
        sys.path.insert(0, os.path.dirname(__file__))
        from ym2413_exact import YM2413EnvelopeExact
        
        print(f"Loading exact EG model from: {args.eg_tables}")
        model = YM2413EnvelopeExact(args.eg_tables)
    else:
        # Placeholder for parametric model
        print("[ERROR] Parametric model not yet implemented")
        sys.exit(1)
    
    # Load scenarios
    with open(args.scenarios, 'r') as f:
        scenarios_data = json.load(f)
    scenarios = scenarios_data.get('scenarios', [])
    
    print(f"Loaded {len(scenarios)} scenarios")
    print(f"Gate grid: {gate_grid}")
    print()
    
    # Run sweeps
    sweep_scenarios(model, scenarios, args.sample_rate, gate_grid, args.output)


if __name__ == '__main__':
    main()
