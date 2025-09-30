#include "envelope_shape.h"
#include <stdio.h>

void shape_stats_init(ShapeGapStats *st) {
    if (!st) return;
    st->avg_gap = 0.0;
    st->max_gap = 0;
    st->count   = 0;
}

void shape_stats_feed(ShapeGapStats *st, int gap) {
    if (!st || gap < 0) return;
    st->count++;
    if (gap > st->max_gap) st->max_gap = gap;
    st->avg_gap += (gap - st->avg_gap) / (double)st->count;
}

int shape_compute_dynamic_threshold(int base_threshold, int ar, const ShapeGapStats *st) {
    if (!st) return base_threshold;
    int th = base_threshold;
    if (st->avg_gap > 8.0) th -= 1;
    if (st->max_gap > 13)  th -= 1;
    if (ar <= 1)           th -= 1;
    if (th < 4) th = 4;
    return th;
}

bool shape_fix_apply(int *ar, int *dr,
                     int threshold,
                     int target_delta,
                     int dr_cap,
                     bool verbose) {
    if (!ar || !dr) return false;
    int A = *ar & 0x0F;
    int D = *dr & 0x0F;
    if (D <= A) return false;
    int gap = D - A;
    if (gap <= threshold) return false;
    int target_dr = A + target_delta;
    if (target_dr > dr_cap) target_dr = dr_cap;
    if (target_dr < A + 1) target_dr = A + 1;
    if (target_dr != D) {
        if (verbose) {
            fprintf(stderr, "[SHAPE] fix AR=%d DR=%d gap=%d th=%d -> DR'=%d\n",
                    A, D, gap, threshold, target_dr);
        }
        *dr = target_dr;
        return true;
    }
    return false;
}