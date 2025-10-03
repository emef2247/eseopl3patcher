#!/usr/bin/env python3
"""
Gate Estimation Batch Aggregator

Aggregates results from multiple sweep runs into combined_best and 
combined_summary CSVs for easy comparison and selection.
"""

import argparse
import csv
import glob
import os
from typing import Dict, List


def load_sweep_result(csv_path: str) -> List[Dict]:
    """
    Load a single sweep result CSV.
    
    Args:
        csv_path: Path to sweep results CSV
    
    Returns:
        List of result dicts
    """
    results = []
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Convert numeric fields
            if 'best_gate' in row:
                row['best_gate'] = int(row['best_gate'])
            if 'score' in row:
                row['score'] = float(row['score'])
            if 'mean_residual' in row:
                row['mean_residual'] = float(row['mean_residual'])
            if 'std_residual' in row:
                row['std_residual'] = float(row['std_residual'])
            if 'num_transitions' in row:
                row['num_transitions'] = int(row['num_transitions'])
            results.append(row)
    return results


def aggregate_best(all_results: List[Dict]) -> List[Dict]:
    """
    Aggregate to find best gate per scenario/patch/channel combination.
    
    Args:
        all_results: Combined list of all sweep results
    
    Returns:
        List of best results per scenario/patch/channel
    """
    # Group by (scenario, patch, channel)
    groups = {}
    for result in all_results:
        key = (
            result.get('scenario', 'unknown'),
            result.get('patch', 'unknown'),
            result.get('channel', 'unknown')
        )
        if key not in groups:
            groups[key] = []
        groups[key].append(result)
    
    # Select best from each group (highest score)
    best_results = []
    for key, group in groups.items():
        # Sort by score (descending), then by best_gate
        sorted_group = sorted(group, key=lambda x: (-x.get('score', 0), x.get('best_gate', 0)))
        best = sorted_group[0]
        best['source_file'] = best.get('_source_file', 'unknown')
        best_results.append(best)
    
    return best_results


def aggregate_summary(all_results: List[Dict]) -> List[Dict]:
    """
    Create summary statistics per scenario/patch/channel.
    
    Args:
        all_results: Combined list of all sweep results
    
    Returns:
        List of summary dicts with statistics
    """
    # Group by (scenario, patch, channel)
    groups = {}
    for result in all_results:
        key = (
            result.get('scenario', 'unknown'),
            result.get('patch', 'unknown'),
            result.get('channel', 'unknown')
        )
        if key not in groups:
            groups[key] = []
        groups[key].append(result)
    
    # Compute statistics for each group
    summaries = []
    for key, group in groups.items():
        scenario, patch, channel = key
        
        gates = [r['best_gate'] for r in group if 'best_gate' in r]
        scores = [r['score'] for r in group if 'score' in r]
        num_trans = [r['num_transitions'] for r in group if 'num_transitions' in r]
        
        summary = {
            'scenario': scenario,
            'patch': patch,
            'channel': channel,
            'num_runs': len(group),
            'min_gate': min(gates) if gates else 0,
            'max_gate': max(gates) if gates else 0,
            'mean_gate': sum(gates) / len(gates) if gates else 0,
            'best_score': max(scores) if scores else 0.0,
            'mean_score': sum(scores) / len(scores) if scores else 0.0,
            'total_transitions': sum(num_trans) if num_trans else 0
        }
        summaries.append(summary)
    
    return summaries


def main():
    parser = argparse.ArgumentParser(
        description='Aggregate gate estimation sweep results'
    )
    parser.add_argument('--input-dir', required=True,
                       help='Directory containing sweep result CSVs')
    parser.add_argument('--pattern', default='*_results.csv',
                       help='Glob pattern for result files (default: *_results.csv)')
    parser.add_argument('--output-best', default='combined_best.csv',
                       help='Output path for best results CSV')
    parser.add_argument('--output-summary', default='combined_summary.csv',
                       help='Output path for summary statistics CSV')
    
    args = parser.parse_args()
    
    # Find all result files
    pattern = os.path.join(args.input_dir, args.pattern)
    result_files = glob.glob(pattern)
    
    if not result_files:
        print(f"[ERROR] No result files found matching: {pattern}")
        return 1
    
    print(f"Found {len(result_files)} result files")
    
    # Load all results
    all_results = []
    for result_file in result_files:
        print(f"  Loading: {os.path.basename(result_file)}")
        try:
            results = load_sweep_result(result_file)
            # Tag with source file
            for r in results:
                r['_source_file'] = os.path.basename(result_file)
            all_results.extend(results)
        except Exception as e:
            print(f"    [WARN] Failed to load: {e}")
    
    print(f"\nTotal results loaded: {len(all_results)}")
    
    # Aggregate best results
    print("\nComputing best results per scenario/patch/channel...")
    best_results = aggregate_best(all_results)
    print(f"  Found {len(best_results)} unique combinations")
    
    # Save best results
    if best_results:
        with open(args.output_best, 'w', newline='', encoding='utf-8') as f:
            fieldnames = ['scenario', 'patch', 'channel', 'best_gate', 'score',
                         'mean_residual', 'std_residual', 'num_transitions',
                         'status', 'source_file']
            writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
            writer.writeheader()
            writer.writerows(best_results)
        print(f"  [OK] Saved: {args.output_best}")
    
    # Aggregate summary statistics
    print("\nComputing summary statistics...")
    summaries = aggregate_summary(all_results)
    print(f"  Found {len(summaries)} unique combinations")
    
    # Save summaries
    if summaries:
        with open(args.output_summary, 'w', newline='', encoding='utf-8') as f:
            fieldnames = ['scenario', 'patch', 'channel', 'num_runs',
                         'min_gate', 'max_gate', 'mean_gate',
                         'best_score', 'mean_score', 'total_transitions']
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(summaries)
        print(f"  [OK] Saved: {args.output_summary}")
    
    print("\n[OK] Aggregation complete")
    return 0


if __name__ == '__main__':
    exit(main())
