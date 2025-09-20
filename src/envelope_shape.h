#ifndef ESEOPL3PATCHER_ENVELOPE_SHAPE_H
#define ESEOPL3PATCHER_ENVELOPE_SHAPE_H

#include <stdint.h>
#include "compat_bool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double avg_gap;
    int    max_gap;
    int    count;
} ShapeGapStats;

void shape_stats_init(ShapeGapStats *st);
void shape_stats_feed(ShapeGapStats *st, int gap);
int  shape_compute_dynamic_threshold(int base_threshold, int ar, const ShapeGapStats *st);
bool shape_fix_apply(int *ar, int *dr,
                     int threshold,
                     int target_delta,
                     int dr_cap,
                     bool verbose);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_ENVELOPE_SHAPE_H */