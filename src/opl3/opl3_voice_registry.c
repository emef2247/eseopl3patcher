#include <stddef.h>                 /* NULL */
#include <stdio.h> 
#include "opl3_voice_registry.h"
#include "opl3_voice.h"             /* opl3_voice_db_find_or_add */
#include "../opll/ym2413_patch_convert.h"

/*
 * Register all YM2413 (OPLL) presets (1..15) and rhythm voices (16..20)
 * into the OPL3 voice database.
 *
 * inst number mapping:
 *   1..15 : melodic presets
 *   16..20: rhythm voices (BD, SD, TOM, CYM, HH)
 */
void opl3_register_all_ym2413(OPL3VoiceDB *db, const CommandOptions *opts) {
    if (!db) return;
    printf("opl3_register_all_ym2413\n");

    for (int inst = 1; inst <= 15; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp, opts);
        opl3_voice_db_find_or_add(db, &vp);
    }
    for (int inst = 16; inst <= 20; ++inst) {
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_with_fb(inst, NULL, &vp, opts);
        opl3_voice_db_find_or_add(db, &vp);
    }
}