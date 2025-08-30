#include "opl3_convert.h"
#include "opl3_voice.h"
#include "opl3_event.h"
#include "opl3_debug_util.h"
#include <stdio.h>

// Print the voice parameters with automatic voice ID and total count lookup via OPL3State
void print_opl3_voice_param(const OPL3State *p_state, const OPL3VoiceParam *vp) {
    if (!vp || !p_state) {
        printf("  (null voice param or state)\n");
        return;
    }
    const OPL3VoiceDB *db = &p_state->voice_db;
    int total_count = db->count;
    int voice_id = -1;
    // Search for the matching voice in the database
    for (int i = 0; i < total_count; ++i) {
        if (opl3_voice_param_cmp(&db->p_voices[i], vp) == 0) {
            voice_id = i;
            break;
        }
    }
    printf("Voice %d/%d: is4op=%d | ", voice_id, total_count, vp->is_4op);
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

// Print the registers and Voice ID for each channel in OPL3State, using the database in p_state
void print_opl3_state_and_voice(const OPL3State *p_state) {
    printf("=== OPL3 State and Voice Dump ===\n");
    for (int ch = 0; ch < OPL3_NUM_CHANNELS; ++ch) {
        printf("Ch %2d: regA=0x%02X regB=0x%02X regC=0x%02X ",
            ch,
            p_state->reg[0xA0 + ch],
            p_state->reg[0xB0 + ch],
            p_state->reg[0xC0 + ch]
        );
        // Optionally show WS (Wave Select) registers for both operators
        printf("WS[0]=0x%02X WS[1]=0x%02X ",
            p_state->reg[0xE0 + ch],           // Operator 0
            p_state->reg[0xE0 + ch + 3]        // Operator 1 (slot offset: ch + 3)
        );

        // Extract voice parameters for this channel
        OPL3VoiceParam vparam;
        extract_voice_param(p_state, ch, &vparam);
        // Lookup the voice ID in this state's DB
        int voice_id = -1;
        const OPL3VoiceDB *db = &p_state->voice_db;
        int total_count = db->count;
        for (int i = 0; i < total_count; ++i) {
            if (opl3_voice_param_cmp(&db->p_voices[i], &vparam) == 0) {
                voice_id = i;
                break;
            }
        }
        printf("VoiceID=%d ", voice_id);

        // Print main parameters (2op/4op, operator parameters)
        printf("is4op=%d | ", vparam.is_4op);
        int n_ops = (vparam.is_4op == 2) ? 4 : 2;
        for (int op = 0; op < n_ops; ++op) {
            printf(
                "op%d[ar=%02X dr=%02X sl=%02X rr=%02X mult=%02X ws=%02X] ",
                op,
                vparam.op[op].ar,
                vparam.op[op].dr,
                vparam.op[op].sl,
                vparam.op[op].rr,
                vparam.op[op].mult,
                vparam.op[op].ws
            );
        }
        printf("\n");
    }
    printf("Rhythm mode: %s\n", p_state->rhythm_mode ? "ON" : "OFF");
    printf("OPL3 mode initialized: %s\n", p_state->opl3_mode_initialized ? "YES" : "NO");
    printf("=== End of Dump ===\n");
}