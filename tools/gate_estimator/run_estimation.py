#!/usr/bin/env python3
"""
Gate Estimation Runner

Main entry point for running gate estimation with different EG models.
Supports both parametric and exact (table-driven) envelope models.
"""

import argparse
import csv
import json
import os
import sys
from typing import Dict, List


def load_vgm_metadata(vgm_path: str) -> Dict:
    """
    Load VGM metadata (simplified - in real use, would parse VGM header).
    
    Args:
        vgm_path: Path to VGM file
    
    Returns:
        Dict with metadata (sample_rate, patches_used, etc.)
    """
    # Placeholder: in real implementation, would parse VGM to extract:
    # - YM2413 patches used
    # - Channels active
    # - Note events timeline
    
    # For now, return sensible defaults
    return {
        'sample_rate': 44100.0,
        'patches': list(range(1, 16)),  # All melodic patches
        'channels': list(range(9)),     # All 9 channels
    }


def estimate_for_vgm(vgm_path: str, model, gate_grid: List[int],
                    output_dir: str, label: str = 'default'):
    """
    Run gate estimation for a single VGM file.
    
    Args:
        vgm_path: Path to VGM file
        model: EG model instance
        gate_grid: List of gate values to try
        output_dir: Output directory for results
        label: Label for this run
    """
    print(f"Estimating gates for: {vgm_path}")
    
    # Load metadata
    metadata = load_vgm_metadata(vgm_path)
    sample_rate = metadata['sample_rate']
    patches = metadata['patches']
    channels = metadata['channels']
    
    print(f"  Sample rate: {sample_rate} Hz")
    print(f"  Patches: {len(patches)}")
    print(f"  Channels: {len(channels)}")
    
    # In a real implementation, we would:
    # 1. Parse VGM to extract note events per channel
    # 2. Group by patch/channel
    # 3. Extract transitions
    # 4. Run model.choose_gate_grid for each group
    # 5. Aggregate results
    
    # For now, create a placeholder result
    results = []
    
    for patch in patches[:3]:  # Limit to first 3 patches for demo
        for channel in channels[:3]:  # Limit to first 3 channels for demo
            # Placeholder: in real use, would extract actual transitions
            transitions = []  # Empty - will trigger robustness handling
            
            # Default patch parameters
            patch_params = {
                'ar': 15, 'dr': 4, 'rr': 7, 'sl': 4,
                'block': 4, 'ksr': False
            }
            
            # Run estimation
            best_gate, metrics = model.choose_gate_grid(
                transitions=transitions,
                sample_rate=sample_rate,
                ar=patch_params['ar'],
                dr=patch_params['dr'],
                rr=patch_params['rr'],
                sl=patch_params['sl'],
                block=patch_params['block'],
                ksr=patch_params['ksr'],
                gate_candidates=gate_grid
            )
            
            result = {
                'patch': patch,
                'channel': channel,
                'best_gate': best_gate,
                'score': metrics.get('score', 0.0),
                'mean_residual': metrics.get('mean_residual', 0.0),
                'num_transitions': metrics.get('num_transitions', 0)
            }
            results.append(result)
    
    # Save results
    os.makedirs(output_dir, exist_ok=True)
    results_path = os.path.join(output_dir, f'{label}_results.csv')
    
    with open(results_path, 'w', newline='', encoding='utf-8') as f:
        fieldnames = ['patch', 'channel', 'best_gate', 'score', 'mean_residual', 'num_transitions']
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(results)
    
    print(f"  [OK] Results saved to: {results_path}")
    
    # Export gates.csv for C runtime
    gates_path = os.path.join(output_dir, f'{label}_gates.csv')
    export_gates_csv(results, gates_path)
    print(f"  [OK] Gates exported to: {gates_path}")


def export_gates_csv(results: List[Dict], output_path: str):
    """
    Export gates in C-loadable format.
    
    Format: patch,channel,gate_samples
    
    Args:
        results: List of estimation results
        output_path: Output CSV path
    """
    with open(output_path, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow(['patch', 'channel', 'gate_samples'])
        
        for result in results:
            writer.writerow([
                result['patch'],
                result['channel'],
                result['best_gate']
            ])


def main():
    parser = argparse.ArgumentParser(
        description='Run gate estimation for YM2413 VGM files'
    )
    parser.add_argument('vgm', help='Input VGM file path')
    parser.add_argument('--eg-model', choices=['param', 'exact'], default='exact',
                       help='EG model type (default: exact)')
    parser.add_argument('--eg-tables', default=None,
                       help='Path to EG tables JSON (required for exact model)')
    parser.add_argument('--gate-grid', default='2048,4096,6144,8192,10240,12288,14336,16384',
                       help='Comma-separated gate values to try')
    parser.add_argument('--output-dir', default='analysis/gate_estimation',
                       help='Output directory for results')
    parser.add_argument('--label', default='default',
                       help='Label for this estimation run')
    
    args = parser.parse_args()
    
    # Parse gate grid
    gate_grid = [int(x.strip()) for x in args.gate_grid.split(',')]
    
    # Load model
    if args.eg_model == 'exact':
        if not args.eg_tables:
            # Default to bundled tables
            script_dir = os.path.dirname(os.path.abspath(__file__))
            args.eg_tables = os.path.join(script_dir, 'ym2413_eg_tables_ymfm.json')
        
        if not os.path.exists(args.eg_tables):
            print(f"[ERROR] EG tables not found: {args.eg_tables}")
            sys.exit(1)
        
        # Import exact model
        sys.path.insert(0, os.path.dirname(__file__))
        from ym2413_exact import YM2413EnvelopeExact
        
        print(f"Loading exact EG model from: {args.eg_tables}")
        model = YM2413EnvelopeExact(args.eg_tables)
    else:
        print("[ERROR] Parametric model not yet implemented")
        print("Please use --eg-model exact")
        sys.exit(1)
    
    print(f"EG Model: {args.eg_model}")
    print(f"Gate grid: {gate_grid}")
    print()
    
    # Run estimation
    estimate_for_vgm(args.vgm, model, gate_grid, args.output_dir, args.label)
    
    print("\n[OK] Estimation complete")


if __name__ == '__main__':
    main()
