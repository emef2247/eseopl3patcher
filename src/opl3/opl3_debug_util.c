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
    if (!vp) { printf("  (null voice param)\n"); return; }
    int voice_no = vp->voice_no;
    int is4op = vp->is_4op;
    printf("VoiceNo=%d | is4op=%d | ", voice_no, is4op);
    int n_ops = (is4op == 1) ? 4 : 2;
    for (int op = 0; op < n_ops; ++op) {
        const OPL3OpParam *opp = &vp->op[op];
        printf("op%d[ar=%02X dr=%02X sl=%02X rr=%02X mult=%02X ws=%02X am=%d vib=%d egt=%d ksr=%d ksl=%d tl=%02X] ",
               op,
               opp->ar, opp->dr, opp->sl, opp->rr,
               opp->mult, opp->ws,
               opp->am, opp->vib, opp->egt, opp->ksr, opp->ksl, opp->tl);
    }
    printf("| fb[0]=%d cnt[0]=%d", vp->fb[0], vp->cnt[0]);
    if (is4op == 1) printf(" fb[1]=%d cnt[1]=%d", vp->fb[1], vp->cnt[1]);
    printf("\n");
}

void debug_dump_opl3_voiceparam(int ch, const OPL3VoiceParam* vp, uint8_t fnum_lsb, uint8_t fnum_msb, uint8_t block, uint8_t keyon)
{
    printf("[DEBUG] KeyOn準備: ch=%d\n", ch);
    printf("  FNUM: %03X (LSB=0x%02X, MSB=0x%02X), Block=%u, KeyOn=%u\n", ((fnum_msb&0x03)<<8)|fnum_lsb, fnum_lsb, fnum_msb, block, keyon);
    printf("  Modulator: TL=%d AR=%d DR=%d SL=%d RR=%d MULT=%d KSL=%d AM=%d VIB=%d EGT=%d KSR=%d WS=%d\n",
        vp->op[0].tl, vp->op[0].ar, vp->op[0].dr, vp->op[0].sl, vp->op[0].rr,
        vp->op[0].mult, vp->op[0].ksl, vp->op[0].am, vp->op[0].vib, vp->op[0].egt, vp->op[0].ksr, vp->op[0].ws);
    printf("  Carrier:   TL=%d AR=%d DR=%d SL=%d RR=%d MULT=%d KSL=%d AM=%d VIB=%d EGT=%d KSR=%d WS=%d\n",
        vp->op[1].tl, vp->op[1].ar, vp->op[1].dr, vp->op[1].sl, vp->op[1].rr,
        vp->op[1].mult, vp->op[1].ksl, vp->op[1].am, vp->op[1].vib, vp->op[1].egt, vp->op[1].ksr, vp->op[1].ws);
    printf("  ALG=%u FB=%u CNT=%u\n", vp->cnt[0], vp->fb[0], vp->cnt[0]);
}