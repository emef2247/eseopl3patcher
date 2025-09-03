#include "opl3_voice.h"
#include "opl3_convert.h"
#include <string.h>
#include <stdlib.h>

/**
 * Initialize the OPL3VoiceDB dynamic array.
 * @param p_db Pointer to OPL3VoiceDB to initialize.
 */
void opl3_voice_db_init(OPL3VoiceDB *p_db) {
    p_db->count = 0;
    p_db->capacity = OPL3_DB_INITIAL_SIZE;
    p_db->p_voices = (OPL3VoiceParam *)calloc(p_db->capacity, sizeof(OPL3VoiceParam));
}

/**
 * Free memory used by the OPL3VoiceDB.
 * @param p_db Pointer to OPL3VoiceDB to free.
 */
void opl3_voice_db_free(OPL3VoiceDB *p_db) {
    if (p_db->p_voices) free(p_db->p_voices);
    p_db->p_voices = NULL;
    p_db->count = 0;
    p_db->capacity = 0;
}

/**
 * Compare two OPL3VoiceParam structures for equality, ignoring TL (Total Level).
 * Returns 1 if parameters match (except TL), 0 otherwise.
 * TL is explicitly ignored for matching purposes.
 */
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b) {
    if (!a || !b) return 0;
    int n_ops = a->is_4op ? 4 : 2;
    for (int op = 0; op < n_ops; ++op) {
        if (a->op[op].am   != b->op[op].am)   return 0;
        if (a->op[op].vib  != b->op[op].vib)  return 0;
        if (a->op[op].egt  != b->op[op].egt)  return 0;
        if (a->op[op].ksr  != b->op[op].ksr)  return 0;
        if (a->op[op].mult != b->op[op].mult) return 0;
        if (a->op[op].ksl  != b->op[op].ksl)  return 0;
        // TL (Total Level) is intentionally ignored for comparison.
        if (a->op[op].ar   != b->op[op].ar)   return 0;
        if (a->op[op].dr   != b->op[op].dr)   return 0;
        if (a->op[op].sl   != b->op[op].sl)   return 0;
        if (a->op[op].rr   != b->op[op].rr)   return 0;
        if (a->op[op].ws   != b->op[op].ws)   return 0;
    }
    if (a->is_4op        != b->is_4op)        return 0;
    if (a->fb[0]         != b->fb[0])         return 0;
    //if (a->cnt[0]        != b->cnt[0])        return 0;
    //if (a->source_fmchip != b->source_fmchip) return 0;
    return 1;
}

/**
 * Find a matching voice in the DB or add a new one.
 * If found, returns its voice_no (unique ID).
 * If not found, adds the voice, assigns a new voice_no, and returns it.
 * @param p_db Pointer to OPL3VoiceDB.
 * @param p_vp Pointer to OPL3VoiceParam to find or add. (voice_no will be set if new)
 * @return voice_no (unique ID) in database.
 */
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, OPL3VoiceParam *p_vp) {
    for (int i = 0; i < p_db->count; ++i) {
        if (opl3_voice_param_cmp(&p_db->p_voices[i], p_vp)) {
            p_vp->voice_no = p_db->p_voices[i].voice_no;
            return p_db->p_voices[i].voice_no;
        }
    }
    // Add new voice if not found
    if (p_db->count >= p_db->capacity) {
        p_db->capacity *= 2;
        p_db->p_voices = (OPL3VoiceParam *)realloc(p_db->p_voices, p_db->capacity * sizeof(OPL3VoiceParam));
    }
    int new_voice_no = p_db->count > 0 ? p_db->p_voices[p_db->count - 1].voice_no + 1 : 0;
    p_vp->voice_no = new_voice_no;
    p_db->p_voices[p_db->count] = *p_vp;
    p_db->count++;
    return new_voice_no;
}

/**
 * Check if a given channel is in 4op mode (returns 1 for 4op, 0 for 2op).
 * Uses OPL3State pointer and channel index.
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @return 1 for 4op, 0 for 2op.
 */
int is_4op_channel(const OPL3State *p_state, int ch) {
    uint8_t reg_104 = p_state->reg[0x104];
    // OPL3 4op pairs: 0+3 (bit0), 1+4 (bit1), 2+5 (bit2)
    if (ch == 0 || ch == 3) return (reg_104 & 0x01) ? 1 : 0;
    if (ch == 1 || ch == 4) return (reg_104 & 0x02) ? 1 : 0;
    if (ch == 2 || ch == 5) return (reg_104 & 0x04) ? 1 : 0;
    // OPL3 4op mode is only for channels 0-5
    return 0;
}

/**
 * Get the OPL3 channel mode (OPL3_MODE_2OP or OPL3_MODE_4OP).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @return OPL3_MODE_2OP or OPL3_MODE_4OP.
 */
int get_opl3_channel_mode(const OPL3State *p_state, int ch) {
    return is_4op_channel(p_state, ch) ? OPL3_MODE_4OP : OPL3_MODE_2OP;
}

/**
 * Extract voice parameters for the given logical channel (0..17).
 * Fills out an OPL3VoiceParam structure, including TL value.
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @param p_out Pointer to OPL3VoiceParam to fill.
 */
void extract_voice_param(const OPL3State *p_state, int ch, OPL3VoiceParam *p_out) {
    memset(p_out, 0, sizeof(OPL3VoiceParam));    
    int mode = is_4op_channel(p_state, ch);
    p_out->is_4op = mode;
    int ch_main = ch;
    int ch_pair = -1;
    if (mode == 1) {
        // Determine 4op channel pair
        if (ch < 3) ch_pair = ch + 3;
        else if (ch >= 3 && ch <= 5) ch_pair = ch - 3;
    }
    int chs[2] = {ch_main, (mode == 1 ? ch_pair : -1)};
    for (int pair = 0; pair < (mode == 1 ? 2 : 1); ++pair) {
        int c = chs[pair];
        if (c < 0) continue;
        // Each 2op pair has 2 operators: slot = c + op*3
        for (int op = 0; op < 2; ++op) {
            int op_idx = c + op * 3;
            OPL3OperatorParam *opp = &p_out->op[pair * 2 + op];
            // Fill operator parameters from register mirror
            opp->am   = (p_state->reg[0x20 + op_idx] >> 7) & 1;
            opp->vib  = (p_state->reg[0x20 + op_idx] >> 6) & 1;
            opp->egt  = (p_state->reg[0x20 + op_idx] >> 5) & 1;
            opp->ksr  = (p_state->reg[0x20 + op_idx] >> 4) & 1;
            opp->mult =  p_state->reg[0x20 + op_idx] & 0x0F;
            opp->ksl  = (p_state->reg[0x40 + op_idx] >> 6) & 3;
            opp->tl   = p_state->reg[0x40 + op_idx] & 0x3F;
            opp->ar   = (p_state->reg[0x60 + op_idx]) & 0x1F;
            opp->dr   = (p_state->reg[0x80 + op_idx] >> 4) & 0x0F;
            opp->sl   = (p_state->reg[0x80 + op_idx]) & 0x0F;
            opp->rr   = (p_state->reg[0xA0 + op_idx]) & 0x0F;
            opp->ws   = (p_state->reg[0xE0 + op_idx]) & 0x07;
        }
        // Feedback and connection type
        p_out->fb[pair]  = (p_state->reg[0xC0 + c] >> 1) & 0x07;
        p_out->cnt[pair] = (p_state->reg[0xC0 + c]) & 0x01;
    }
    // The unique voice_no will be set by the voice_db on insertion.
}