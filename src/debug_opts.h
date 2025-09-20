#ifndef ESEOPL3PATCHER_DEBUG_OPTS_H
#define ESEOPL3PATCHER_DEBUG_OPTS_H

#include "../compat_bool.h"

/** Global debug / diagnostic options */
typedef struct {
    bool strip_non_opl;       /* Remove AY8910/K051649 etc. from output */
    bool test_tone;           /* Inject a simple test tone sequence */
    bool fast_attack;         /* Force fast envelope (AR=15 etc.) */
    bool no_post_keyon_tl;    /* Suppress TL changes right after KeyOn */
    bool single_port;         /* Emit only port0 writes (suppress port1) */
} DebugOpts;

/* Global instance (defined in main.c) */
extern DebugOpts g_dbg;

#endif /* ESEOPL3PATCHER_DEBUG_OPTS_H */