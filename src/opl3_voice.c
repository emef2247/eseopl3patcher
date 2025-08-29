#include "opl3_convert.h"
#include "opl3_voice.h"
#include <string.h>
#include <stdlib.h>

// Initialize the voice database
void opl3_voice_db_init(OPL3VoiceDB *db) {
    db->count = 0;
    db->capacity = 16;
    db->voices = (OPL3VoiceParam*)calloc(db->capacity, sizeof(OPL3VoiceParam));
}

// Free the voice database
void opl3_voice_db_free(OPL3VoiceDB *db) {
    if (db->voices) free(db->voices);
    db->voices = NULL;
    db->count = db->capacity = 0;
}

// Compare two OPL3VoiceParam
static int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b) {
    return memcmp(a, b, sizeof(OPL3VoiceParam));
}

// Find or add a new voice, return its Voice ID (0-based)
int opl3_voice_db_find_or_add(OPL3VoiceDB *db, const OPL3VoiceParam *vp) {
    for (int i = 0; i < db->count; ++i) {
        if (opl3_voice_param_cmp(&db->voices[i], vp) == 0) {
            return i;
        }
    }
    // Add new voice
    if (db->count >= db->capacity) {
        db->capacity *= 2;
        db->voices = (OPL3VoiceParam*)realloc(db->voices, db->capacity * sizeof(OPL3VoiceParam));
    }
    db->voices[db->count] = *vp;
    return db->count++;
}

// Check if given channel is in 4op mode (reg_104: OPL3 0x104)
int is_4op_channel(const uint8_t reg_104, int ch) {
    // OPL3 4op pairs: 0+3 (bit0), 1+4 (bit1), 2+5 (bit2)
    if (ch == 0 || ch == 3) return (reg_104 & 0x01) ? 2 : 1;
    if (ch == 1 || ch == 4) return (reg_104 & 0x02) ? 2 : 1;
    if (ch == 2 || ch == 5) return (reg_104 & 0x04) ? 2 : 1;
    // OPL3 4op is only for ch0-5
    return 1;
}

// Extract voice parameters for the specified channel
// state: pointer to OPL3State (must contain regA, regB, regC)
// ch: logical channel (0..17), reg_104: OPL3 0x104 value
void extract_voice_param(const OPL3State *state, int ch, uint8_t reg_104, OPL3VoiceParam *out) {
    memset(out, 0, sizeof(OPL3VoiceParam));
    int mode = is_4op_channel(reg_104, ch);
    out->is_4op = mode;
    int ch_main = ch;
    int ch_pair = -1;
    if (mode == 2) {
        // Determine 4op channel pair
        if (ch < 3) ch_pair = ch + 3;
        else if (ch >= 3 && ch <= 5) ch_pair = ch - 3;
        // For 4op: collect both channels
    }
    int chs[2] = {ch_main, (mode == 2 ? ch_pair : -1)};
    for (int pair = 0; pair < (mode == 2 ? 2 : 1); ++pair) {
        int c = chs[pair];
        if (c < 0) continue;
        // OPL3 operator mapping (see OPL3 docs)
        for (int op = 0; op < 2; ++op) {
            // Operator index for OPL3: slot = ch + op*3
            int op_idx = c + op * 3;
            OPL3OperatorParam *opp = &out->op[pair * 2 + op];
            // The following assumes state contains regA, regB, regC arrays for 18 channels (port0+port1)
            opp->am   = (state->regA[op_idx] >> 7) & 1;
            opp->vib  = (state->regA[op_idx] >> 6) & 1;
            opp->egt  = (state->regA[op_idx] >> 5) & 1;
            opp->ksr  = (state->regA[op_idx] >> 4) & 1;
            opp->mult =  state->regA[op_idx] & 0x0F;
            opp->ksl  = (state->regB[op_idx] >> 6) & 3;
            // TL intentionally omitted
            opp->ar   = (state->regB[op_idx] >> 0) & 0x1F;
            opp->dr   = (state->regC[op_idx] >> 4) & 0x0F;
            opp->sl   = (state->regC[op_idx] >> 0) & 0x0F;
            opp->rr   = (state->regB[op_idx] >> 0) & 0x0F;
            opp->ws   = (state->regC[op_idx] >> 0) & 0x07;
        }
        // Feedback and connection type
        out->fb[pair]  = (state->regC[c] >> 1) & 0x07;
        out->cnt[pair] = (state->regC[c] >> 0) & 0x01;
    }
}