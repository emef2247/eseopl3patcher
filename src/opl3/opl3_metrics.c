#include "opl3_metrics.h"

#ifdef ENABLE_OPL3_METRICS
#include <stdio.h>

static FILE *g_fp = NULL;

void opl3_metrics_init(const char *path) {
    g_fp = fopen(path ? path : "opl3_metrics.csv", "w");
    if (g_fp) {
        fprintf(g_fp, "time_samples,ch,event,fnum,block\n");
    }
}

void opl3_metrics_close(void) {
    if (g_fp) {
        fclose(g_fp);
        g_fp = NULL;
    }
}

/* 現状 time_samples を持っていないので 0。後でサンプルカウンタ導入可能 */
void opl3_metrics_note_on(int ch, uint16_t fnum, uint8_t block) {
    if (g_fp) fprintf(g_fp, "0,%d,ON,%u,%u\n", ch, fnum, block);
}
void opl3_metrics_note_off(int ch) {
    if (g_fp) fprintf(g_fp, "0,%d,OFF,,\n", ch);
}
#endif