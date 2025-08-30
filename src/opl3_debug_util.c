#include "opl3_convert.h"
#include "opl3_voice.h"
#include "opl3_event.h"
#include "opl3_debug_util.h"
#include <stdio.h>

// External voice DB (declare as extern if necessary)
extern OPL3VoiceDB g_voice_db;
extern uint8_t g_opl3_reg_104;

// Print the registers and Voice ID for each channel in OPL3State
void print_opl3_state_and_voice(const OPL3State *pState) {
    printf("=== OPL3 State and Voice Dump ===\n");
    for (int ch = 0; ch < OPL3_NUM_CHANNELS; ++ch) {
        printf("Ch %2d: regA=0x%02X regB=0x%02X regC=0x%02X ", ch, pState->regA[ch], pState->regB[ch], pState->regC[ch]);
        // Extract voice parameters for this channel
        OPL3VoiceParam vparam;
        extract_voice_param(pState, ch, g_opl3_reg_104, &vparam);
        int voice_id = opl3_voice_db_find_or_add(&g_voice_db, &vparam);
        printf("VoiceID=%d ", voice_id);

        // Optionally print main parameters (e.g., 2op/4op, operator parameters)
        printf("is4op=%d | ", vparam.is_4op);
        for (int op = 0; op < (vparam.is_4op == 2 ? 4 : 2); ++op) {
            printf("op%d[ar=%02X dr=%02X sl=%02X rr=%02X mult=%02X ws=%02X] ",
                op, vparam.op[op].ar, vparam.op[op].dr, vparam.op[op].sl, vparam.op[op].rr,
                vparam.op[op].mult, vparam.op[op].ws);
        }
        printf("\n");
    }
    printf("Rhythm mode: %s\n", pState->rhythm_mode ? "ON" : "OFF");
    printf("OPL3 mode initialized: %s\n", pState->opl3_mode_initialized ? "YES" : "NO");
    printf("=== End of Dump ===\n");
}