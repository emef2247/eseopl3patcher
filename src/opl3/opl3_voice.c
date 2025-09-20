#include "opl3_voice.h"
#include "opl3_convert.h"
#include <string.h>
#include <stdlib.h>

void opl3_voice_db_init(OPL3VoiceDB *p_db) {
    p_db->count = 0;
    p_db->capacity = OPL3_DB_INITIAL_SIZE;
    p_db->p_voices = (OPL3VoiceParam*)calloc(p_db->capacity, sizeof(OPL3VoiceParam));
}
void opl3_voice_db_free(OPL3VoiceDB *p_db) {
    if (p_db->p_voices) free(p_db->p_voices);
    p_db->p_voices = NULL;
    p_db->count = 0;
    p_db->capacity = 0;
}

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
        if (a->op[op].ar   != b->op[op].ar)   return 0;
        if (a->op[op].dr   != b->op[op].dr)   return 0;
        if (a->op[op].sl   != b->op[op].sl)   return 0;
        if (a->op[op].rr   != b->op[op].rr)   return 0;
        if (a->op[op].ws   != b->op[op].ws)   return 0;
    }
    if (a->is_4op != b->is_4op) return 0;
    if (a->fb[0]  != b->fb[0])  return 0;
    return 1;
}

int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, OPL3VoiceParam *p_vp) {
    for (int i = 0; i < p_db->count; ++i) {
        if (opl3_voice_param_cmp(&p_db->p_voices[i], p_vp)) {
            p_vp->voice_no = p_db->p_voices[i].voice_no;
            return p_db->p_voices[i].voice_no;
        }
    }
    if (p_db->count >= p_db->capacity) {
        p_db->capacity *= 2;
        p_db->p_voices = (OPL3VoiceParam*)realloc(p_db->p_voices, p_db->capacity * sizeof(OPL3VoiceParam));
    }
    int new_voice_no = (p_db->count > 0) ? p_db->p_voices[p_db->count - 1].voice_no + 1 : 0;
    p_vp->voice_no = new_voice_no;
    p_db->p_voices[p_db->count++] = *p_vp;
    return new_voice_no;
}

int is_4op_channel(const OPL3State *p_state, int ch) {
    uint8_t reg_104 = p_state->reg[0x104];
    if (ch == 0 || ch == 3) return (reg_104 & 0x01) ? 1 : 0;
    if (ch == 1 || ch == 4) return (reg_104 & 0x02) ? 1 : 0;
    if (ch == 2 || ch == 5) return (reg_104 & 0x04) ? 1 : 0;
    return 0;
}
int get_opl3_channel_mode(const OPL3State *p_state, int ch) {
    return is_4op_channel(p_state, ch) ? OPL3_MODE_4OP : OPL3_MODE_2OP;
}

void extract_voice_param(const OPL3State *p_state, OPL3VoiceParam *p_out) {
    memset(p_out, 0, sizeof(OPL3VoiceParam));
    int latest_keyon_ch = -1;
    for (int ch = 0; ch < 9; ++ch) { /* 2-OP 対象9chのみスキャン */
        uint8_t reg_val = p_state->reg[0xB0 + ch];
        if (reg_val & 0x20) { latest_keyon_ch = ch; break; }
    }
    int ch = (latest_keyon_ch >= 0) ? latest_keyon_ch : 0;

    static const int slot_table[9][2] = {
        {0,3},{1,4},{2,5},{6,9},{7,10},{8,11},{12,15},{13,16},{14,17}
    };
    int slot_mod = slot_table[ch][0];
    int slot_car = slot_table[ch][1];

    /* Modulator */
    uint8_t v;
    OPL3OpParam *mod = &p_out->op[0];
    v = p_state->reg[0x40 + slot_mod]; mod->tl  = v & 0x3F; mod->ksl = (v >> 6) & 0x03;
    v = p_state->reg[0x20 + slot_mod]; mod->mult= v & 0x0F; mod->ksr = (v >> 4) & 1; mod->egt=(v>>5)&1; mod->vib=(v>>6)&1; mod->am=(v>>7)&1;
    v = p_state->reg[0x60 + slot_mod]; mod->ar  = (v >> 4) & 0x0F; mod->dr = v & 0x0F;
    v = p_state->reg[0x80 + slot_mod]; mod->sl  = (v >> 4) & 0x0F; mod->rr = v & 0x0F;
    v = p_state->reg[0xE0 + slot_mod]; mod->ws  = v & 0x07;

    /* Carrier */
    OPL3OpParam *car = &p_out->op[1];
    v = p_state->reg[0x40 + slot_car]; car->tl  = v & 0x3F; car->ksl = (v >> 6) & 0x03;
    v = p_state->reg[0x20 + slot_car]; car->mult= v & 0x0F; car->ksr = (v >> 4) & 1; car->egt=(v>>5)&1; car->vib=(v>>6)&1; car->am=(v>>7)&1;
    v = p_state->reg[0x60 + slot_car]; car->ar  = (v >> 4) & 0x0F; car->dr = v & 0x0F;
    v = p_state->reg[0x80 + slot_car]; car->sl  = (v >> 4) & 0x0F; car->rr = v & 0x0F;
    v = p_state->reg[0xE0 + slot_car]; car->ws  = v & 0x07;

    v = p_state->reg[0xC0 + ch];
    p_out->fb[0]  = (v >> 1) & 0x07;
    p_out->cnt[0] = v & 0x01;
    p_out->voice_no = ch;
    p_out->is_4op = 0;
}