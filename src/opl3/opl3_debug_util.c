#include "opl3_debug_util.h"
#include "opl3_voice.h"
#include <stdio.h>

/**
 * Print the voice parameters with voice ID, total count, FM chip type (as string), and patch number.
 * This function looks up the voice ID in the OPL3State's voice_db and prints operator fields.
 */
void print_opl3_voice_param(const OPL3State *p_state, const OPL3VoiceParam *vp) {
    if (!p_state || !vp) {
        printf("  (null state or voice param)\n");
        return;
    }

    const OPL3VoiceDB *db = &p_state->voice_db;
    int total_count = db->count;
    int voice_id = -1;
    for (int i = 0; i < total_count; ++i) {
        if (opl3_voice_param_cmp(&db->p_voices[i], vp) == 0) {
            voice_id = i;
            break;
        }
    }

    // Print FM chip type as string (if available)
    const char *fmchip_str = "Unknown";
    switch (p_state->source_fmchip) {
        case FMCHIP_YM2413:   fmchip_str = "YM2413"; break;
        case FMCHIP_YM2612:   fmchip_str = "YM2612"; break;
        case FMCHIP_YM2151:   fmchip_str = "YM2151"; break;
        case FMCHIP_YM2203:   fmchip_str = "YM2203"; break;
        case FMCHIP_YM2608:   fmchip_str = "YM2608"; break;
        case FMCHIP_YM2610:   fmchip_str = "YM2610"; break;
        case FMCHIP_YM3812:   fmchip_str = "YM3812"; break;
        case FMCHIP_YM3526:   fmchip_str = "YM3526"; break;
        case FMCHIP_Y8950:    fmchip_str = "Y8950"; break;
        case FMCHIP_YMF262:   fmchip_str = "YMF262(OPL3)"; break;
        case FMCHIP_YMF278B:  fmchip_str = "YMF278B"; break;
        case FMCHIP_YMF271:   fmchip_str = "YMF271"; break;
        case FMCHIP_YMZ280B:  fmchip_str = "YMZ280B"; break;
        case FMCHIP_2xYM2413:   fmchip_str = "2xYM2413"; break;
        case FMCHIP_2xYM2612:   fmchip_str = "2xYM2612"; break;
        case FMCHIP_2xYM2151:   fmchip_str = "2xYM2151"; break;
        case FMCHIP_2xYM2203:   fmchip_str = "2xYM2203"; break;
        case FMCHIP_2xYM2608:   fmchip_str = "2xYM2608"; break;
        case FMCHIP_2xYM2610:   fmchip_str = "2xYM2610"; break;
        case FMCHIP_2xYM3812:   fmchip_str = "2xYM3812"; break;
        case FMCHIP_2xYM3526:   fmchip_str = "2xYM3526"; break;
        case FMCHIP_2xY8950:    fmchip_str = "2xY8950"; break;
        case FMCHIP_2xYMF262:   fmchip_str = "2xYMF262(OPL3)"; break;
        case FMCHIP_2xYMF278B:  fmchip_str = "2xYMF278B"; break;
        case FMCHIP_2xYMF271:   fmchip_str = "2xYMF271"; break;
        case FMCHIP_2xYMZ280B:  fmchip_str = "2xYMZ280B"; break;
        // Add more as needed
        default: break;
    }

    // If patch number is to be printed, try to get it (-1 if not present)
    int patch_no = -1;
    // If you add patch_no to OPL3VoiceParam, fetch here

    printf("Voice %d/%d | FMChip=%s | Patch#=%d | is4op=%d | ",
           voice_id, total_count, fmchip_str, patch_no, vp->is_4op);

    int n_ops = (vp->is_4op == 2) ? 4 : 2;
    for (int op = 0; op < n_ops; ++op) {
        const OPL3OperatorParam *opp = &vp->op[op];
        printf("op%d[ar=%02X dr=%02X sl=%02X rr=%02X mult=%02X ws=%02X am=%d vib=%d egt=%d ksr=%d ksl=%d] ",
            op,
            opp->ar, opp->dr, opp->sl, opp->rr,
            opp->mult, opp->ws,
            opp->am, opp->vib, opp->egt, opp->ksr, opp->ksl
        );
    }
    printf("| fb[0]=%d cnt[0]=%d", vp->fb[0], vp->cnt[0]);
    if (vp->is_4op == 2) {
        printf(" fb[1]=%d cnt[1]=%d", vp->fb[1], vp->cnt[1]);
    }
    printf("\n");
}