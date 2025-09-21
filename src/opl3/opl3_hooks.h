#ifndef OPL3_HOOKS_H
#define OPL3_HOOKS_H

#include "opl3_state.h"

typedef struct OPL3Hooks {
    void (*on_pre_keyon)(int ch, OPL3VoiceParam *vp); /* KeyOn直前（VoiceParam確定後） */
    void (*on_post_keyon)(int ch);                    /* KeyOnコマンド書き込み直後 */
    void (*on_note_off)(int ch);                      /* KeyOffエッジ時 */
} OPL3Hooks;

extern OPL3Hooks g_opl3_hooks;

#endif /* OPL3_HOOKS_H */