#ifndef OPL3_METRICS_H
#define OPL3_METRICS_H

#include <stdint.h>

#ifdef ENABLE_OPL3_METRICS
void opl3_metrics_init(const char *path);
void opl3_metrics_close(void);
void opl3_metrics_note_on(int ch, uint16_t fnum, uint8_t block);
void opl3_metrics_note_off(int ch);
#else
#define opl3_metrics_init(path)         ((void)0)
#define opl3_metrics_close()            ((void)0)
#define opl3_metrics_note_on(c,f,b)     ((void)0)
#define opl3_metrics_note_off(c)        ((void)0)
#endif

#endif /* OPL3_METRICS_H */