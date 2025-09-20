#include "envelope_shape.h"
#include <stdio.h>
#include <math.h>

/**
 * shape_fix_params_t:
 * gap 閾値を動的計算するための統計入力。
 */
typedef struct {
    double avg_gap;
    double max_gap;
} shape_fix_stats_t;

/**
 * compute_dynamic_gap_threshold
 * raw AR/DR の分布統計を利用して基準閾値を決める（簡易例）
 */
int compute_dynamic_gap_threshold(int raw_ar, double avg_gap, double max_gap) {
    // 基本値
    int base = 6;
    if (raw_ar <= 1) base += 2;
    if (avg_gap > 8.0) base += 1;
    if (max_gap > 12.0) base += 1;
    if (base > 12) base = 12;
    return base;
}

/**
 * apply_shape_fix
 * gap > dynamic_threshold なら DR を再計算
 */
void apply_shape_fix(int *ar_val, int *dr_val, int dynamic_threshold) {
    if (!ar_val || !dr_val) return;
    int ar = *ar_val;
    int dr = *dr_val;
    int gap = dr - ar;
    if (gap > dynamic_threshold) {
        // 例: gap を縮め AR+8 へ近づける（上限14）
        int target = ar + 8;
        if (target > 14) target = 14;
        if (target < dr) {
            *dr_val = target;
        }
    }
}