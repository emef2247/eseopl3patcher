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
 * Extracts all operator and channel parameters for a single OPL3 channel
 * from the register mirror and fills OPL3VoiceParam.
 * This version does not require ch or reg_type, but scans register mirror.
 *
 * @param p_state Pointer to OPL3State containing register mirror
 * @param p_out Pointer to output OPL3VoiceParam structure
 */
void extract_voice_param(const OPL3State *p_state, OPL3VoiceParam *p_out) {
    memset(p_out, 0, sizeof(OPL3VoiceParam));

    // Try to detect the most recent "KeyOn" event to identify the current channel.
    int latest_keyon_ch = -1;
    for (int ch = 0; ch < OPL3_NUM_CHANNELS; ++ch) {
        uint8_t reg_val = p_state->reg[0xB0 + ch];
        uint8_t keyon = reg_val & 0x20;
        if (keyon) {
            // Use timestamp or some heuristic to pick the most recent KeyOn.
            // Here, we simply pick the first found "active" channel.
            latest_keyon_ch = ch;
            // If you have timestamps per channel, use them for accuracy.
            break;
        }
    }

    // If no KeyOn found, fallback to channel 0.
    int ch = (latest_keyon_ch >= 0) ? latest_keyon_ch : 0;

    // Operator slot mapping for OPL3 (for 2-operator mode)
    // Each channel has 2 operators: slot1 (modulator) and slot2 (carrier)
    static const int slot_table[9][2] = {
        {0, 3}, {1, 4}, {2, 5}, {6, 9}, {7, 10}, {8, 11}, {12, 15}, {13, 16}, {14, 17}
    };

    int slot_mod = slot_table[ch % 9][0];
    int slot_car = slot_table[ch % 9][1];
    int reg_base_mod = 0x40 + slot_mod;
    int reg_base_car = 0x40 + slot_car;

    // Extract operator parameters for modulator
    OPL3OperatorParam *mod = &p_out->op[0];
    uint8_t val;
    val = p_state->reg[reg_base_mod];
    mod->tl = val & 0x3F;
    mod->ksl = (val >> 6) & 0x03;

    val = p_state->reg[0x20 + slot_mod];
    mod->mult = val & 0x0F;
    mod->ksr = (val >> 4) & 0x01;
    mod->egt = (val >> 5) & 0x01;
    mod->vib = (val >> 6) & 0x01;
    mod->am  = (val >> 7) & 0x01;

    val = p_state->reg[0x60 + slot_mod];
    mod->ar = (val >> 4) & 0x0F;
    mod->dr = val & 0x0F;

    val = p_state->reg[0x80 + slot_mod];
    mod->sl = (val >> 4) & 0x0F;
    mod->rr = val & 0x0F;

    val = p_state->reg[0xE0 + slot_mod];
    mod->ws = val & 0x07;

    // Extract operator parameters for carrier
    OPL3OperatorParam *car = &p_out->op[1];
    val = p_state->reg[reg_base_car];
    car->tl = val & 0x3F;
    car->ksl = (val >> 6) & 0x03;

    val = p_state->reg[0x20 + slot_car];
    car->mult = val & 0x0F;
    car->ksr = (val >> 4) & 0x01;
    car->egt = (val >> 5) & 0x01;
    car->vib = (val >> 6) & 0x01;
    car->am  = (val >> 7) & 0x01;

    val = p_state->reg[0x60 + slot_car];
    car->ar = (val >> 4) & 0x0F;
    car->dr = val & 0x0F;

    val = p_state->reg[0x80 + slot_car];
    car->sl = (val >> 4) & 0x0F;
    car->rr = val & 0x0F;

    val = p_state->reg[0xE0 + slot_car];
    car->ws = val & 0x07;

    // Channel-level parameters (Feedback and Algorithm)
    val = p_state->reg[0xC0 + ch];
    p_out->fb[0]  = (val >> 1) & 0x07;
    p_out->cnt[0] = val & 0x01;

    // Fill rest of OPL3VoiceParam as needed (e.g., voice number, FM chip type)
    p_out->voice_no = ch;
    p_out->is_4op = 0; // Not handled here (2-op only)
}
