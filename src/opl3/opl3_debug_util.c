#include "opl3_debug_util.h"
#include "opl3_voice.h"
#include <stdio.h>

/**
 * Print the contents of an OPL3VoiceParam structure (operator and voice fields).
 * This function prints all operator parameters, is_4op, voice_no, and feedback/connection for each pair.
 *
 * @param vp Pointer to OPL3VoiceParam to print.
 */
void print_opl3_voice_param(const OPL3VoiceParam *vp) {
    if (!vp) {
        printf("  (null voice param)\n");
        return;
    }

    int voice_no = vp->voice_no;
    int is4op = vp->is_4op;

    printf("VoiceNo=%d | is4op=%d | ", voice_no, is4op);

    int n_ops = (is4op == 1) ? 4 : 2;
    for (int op = 0; op < n_ops; ++op) {
        const OPL3OperatorParam *opp = &vp->op[op];
        printf("op%d[ar=%02X dr=%02X sl=%02X rr=%02X mult=%02X ws=%02X am=%d vib=%d egt=%d ksr=%d ksl=%d tl=%02X] ",
            op,
            opp->ar, opp->dr, opp->sl, opp->rr,
            opp->mult, opp->ws,
            opp->am, opp->vib, opp->egt, opp->ksr, opp->ksl, opp->tl
        );
    }
    printf("| fb[0]=%d cnt[0]=%d", vp->fb[0], vp->cnt[0]);
    if (is4op == 1) {
        printf(" fb[1]=%d cnt[1]=%d", vp->fb[1], vp->cnt[1]);
    }
    printf("\n");
}