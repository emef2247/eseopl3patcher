#include "opl3_convert.h"
#include "opl3_voice.h"
#include <string.h>
#include <stdlib.h>

// --- YMF262 (OPL3) Channel/Slot/Register mapping tables ---
// Reference: YMF262 datasheet (see attached image1)

// Each channel has 2 operators. The following table maps logical channel and operator to the slot number.
// The indexes are [channel][operator] (operator: 0=slot1, 1=slot2).
// This covers both Port 0 (A1=L, ch 0..8) and Port 1 (A1=H, ch 9..17).

static const int opl3_slot_reg_offset[18][2] = {
    // ch: 0..8 (Port 0, A1=L)
    { 0, 3 },   // ch 0
    { 1, 4 },   // ch 1
    { 2, 5 },   // ch 2
    { 6, 9 },   // ch 3
    { 7,10 },   // ch 4
    { 8,11 },   // ch 5
    {12,15 },   // ch 6 (RYT)
    {13,16 },   // ch 7 (RYT)
    {14,17 },   // ch 8 (RYT)
    // ch: 9..17 (Port 1, A1=H)
    {18,21 },   // ch 9
    {19,22 },   // ch10
    {20,23 },   // ch11
    {24,27 },   // ch12
    {25,28 },   // ch13
    {26,29 },   // ch14
    {30,33 },   // ch15 (RYT)
    {31,34 },   // ch16 (RYT)
    {32,35 }    // ch17 (RYT)
};

// Register base addresses for each operator parameter set (TL excluded)
#define REG20 0x20
#define REG40 0x40
#define REG60 0x60
#define REG80 0x80
#define REGE0 0xE0
#define REGC0 0xC0

// Initialize the voice database
void opl3_voice_db_init(OPL3VoiceDB *p_db) {
    p_db->count = 0;
    p_db->capacity = 16;
    p_db->p_voices = (OPL3VoiceParam*)calloc(p_db->capacity, sizeof(OPL3VoiceParam));
}

// Free the voice database
void opl3_voice_db_free(OPL3VoiceDB *p_db) {
    if (p_db->p_voices) free(p_db->p_voices);
    p_db->p_voices = NULL;
    p_db->count = p_db->capacity = 0;
}

// Compare two OPL3VoiceParam (compare only meaningful fields)
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b) {
    if (a->is_4op != b->is_4op) return 1;
    int n_ops = (a->is_4op) ? 4 : 2;
    for (int i = 0; i < n_ops; ++i) {
        if (memcmp(&a->op[i], &b->op[i], sizeof(OPL3OperatorParam)) != 0)
            return 1;
    }
    int n_fb = (a->is_4op) ? 2 : 1;
    for (int i = 0; i < n_fb; ++i) {
        if (a->fb[i] != b->fb[i] || a->cnt[i] != b->cnt[i]) return 1;
    }
    return 0;
}

// Find or add a new voice, return its Voice ID (0-based)
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, const OPL3VoiceParam *p_vp) {
    // Search for an existing matching voice
    for (int i = 0; i < p_db->count; ++i) {
        if (opl3_voice_param_cmp(&p_db->p_voices[i], p_vp) == 0) {
            return i;
        }
    }
    // If not found, add the new voice to the database
    if (p_db->count >= p_db->capacity) {
        int new_capacity = (p_db->capacity > 0) ? (p_db->capacity * 2) : 16;
        OPL3VoiceParam* new_voices = (OPL3VoiceParam*)realloc(p_db->p_voices, new_capacity * sizeof(OPL3VoiceParam));
        if (!new_voices) {
            // Allocation failed
            return -1;
        }
        p_db->p_voices = new_voices;
        p_db->capacity = new_capacity;
    }
    p_db->p_voices[p_db->count] = *p_vp;
    return p_db->count++;
}

// Returns 1 (true) if the channel is in 4op mode, 0 (false) otherwise.
int is_4op_channel(const OPL3State *p_state, int ch) {
    uint8_t reg_104 = p_state->reg[0x104];
    // Only channels 0-5 and 9-14 can be part of a 4op pair
    if ((ch >= 0 && ch <= 2) || (ch >= 3 && ch <= 5)) {
        int pair = ch % 3;
        return (reg_104 & (1 << pair)) ? 1 : 0;
    } else if ((ch >= 9 && ch <= 11) || (ch >= 12 && ch <= 14)) {
        int pair = (ch - 9) % 3;
        return (reg_104 & (1 << pair)) ? 1 : 0;
    }
    // All other channels are always 2op
    return 0;
}

// Get the OPL3 channel mode (2op or 4op)
int get_opl3_channel_mode(const OPL3State *p_state, int ch) {
    uint8_t reg_104 = p_state->reg[0x104];
    if ((ch >= 0 && ch <= 2) || (ch >= 3 && ch <= 5)) {
        int pair = ch % 3;
        return (reg_104 & (1 << pair)) ? OPL3_MODE_4OP : OPL3_MODE_2OP;
    } else if ((ch >= 9 && ch <= 11) || (ch >= 12 && ch <= 14)) {
        int pair = (ch - 9) % 3;
        return (reg_104 & (1 << pair)) ? OPL3_MODE_4OP : OPL3_MODE_2OP;
    }
    return OPL3_MODE_2OP;
}


// Extract voice parameters for the specified channel.
// If the channel is in 4op mode, extract both channels of the pair from the lower-numbered channel only.
void extract_voice_param(const OPL3State *p_state, int ch, OPL3VoiceParam *p_out) {
    memset(p_out, 0, sizeof(OPL3VoiceParam));

    int four_op = is_4op_channel(p_state, ch);

    p_out->is_4op = four_op ? 1 : 0;

    // For 4op, only extract from the lower-numbered channel in the pair
    int is_port1 = (ch >= 9);
    int ch_base = is_port1 ? 9 : 0;
    int ch_local = ch - ch_base;

    int ch_pair = -1;
    if (four_op) {
        if (ch_local < 3) ch_pair = ch_base + ch_local + 3; // e.g. 0+3, 1+4, 2+5 or 9+12, 10+13, 11+14
        else if (ch_local >= 3 && ch_local <= 5) ch_pair = ch_base + ch_local - 3;
        // Only extract for the lower-numbered channel in each pair
        if (ch_local >= 3 && ch_local <= 5) {
            // Clear all fields for this channel to avoid duplicate or partial registration
            memset(p_out, 0, sizeof(OPL3VoiceParam));
            return;
        }
    }

    int n_pairs = four_op ? 2 : 1;
    int chs[2] = { ch, (four_op ? ch_pair : -1) };
    for (int pair = 0; pair < n_pairs; ++pair) {
        int c = chs[pair];
        if (c < 0 || c >= 18) continue;
        for (int op = 0; op < 2; ++op) {
            int slot = opl3_slot_reg_offset[c][op];
            OPL3OperatorParam *p_opp = &p_out->op[pair * 2 + op];
            uint8_t reg20 = p_state->reg[REG20 + slot];
            uint8_t reg40 = p_state->reg[REG40 + slot];
            uint8_t reg60 = p_state->reg[REG60 + slot];
            uint8_t reg80 = p_state->reg[REG80 + slot];
            uint8_t regE0 = p_state->reg[REGE0 + slot];

            p_opp->am   = (reg20 >> 7) & 1;
            p_opp->vib  = (reg20 >> 6) & 1;
            p_opp->egt  = (reg20 >> 5) & 1;
            p_opp->ksr  = (reg20 >> 4) & 1;
            p_opp->mult =  reg20 & 0x0F;
            p_opp->ksl  = (reg40 >> 6) & 3;
            // TL intentionally omitted
            p_opp->ar   = (reg60 >> 4) & 0x0F;
            p_opp->dr   = (reg60 >> 0) & 0x0F;
            p_opp->sl   = (reg80 >> 4) & 0x0F;
            p_opp->rr   = (reg80 >> 0) & 0x0F;
            p_opp->ws   = regE0 & 0x07;
        }
        // Feedback and connection type are per channel
        uint8_t regC0 = p_state->reg[REGC0 + c];
        p_out->fb[pair]  = (regC0 >> 1) & 0x07;
        p_out->cnt[pair] = (regC0 >> 0) & 0x01;
    }
    // Zero unused operators if not 4op
    if (!four_op) {
        memset(&p_out->op[2], 0, sizeof(OPL3OperatorParam) * 2);
        p_out->fb[1] = 0;
        p_out->cnt[1] = 0;
    }
}