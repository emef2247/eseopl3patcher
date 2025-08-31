#include "opl3_voice.h"
#include "opl3_convert.h"
#include <string.h>
#include <stdlib.h>

/**
 * Initialize the OPL3VoiceDB dynamic array.
 */
void opl3_voice_db_init(OPL3VoiceDB *p_db) {
    p_db->count = 0;
    p_db->capacity = 16;
    p_db->p_voices = (OPL3VoiceParam *)calloc(p_db->capacity, sizeof(OPL3VoiceParam));
}

/**
 * Free memory used by the OPL3VoiceDB.
 */
void opl3_voice_db_free(OPL3VoiceDB *p_db) {
    if (p_db->p_voices) free(p_db->p_voices);
    p_db->p_voices = NULL;
    p_db->count = 0;
    p_db->capacity = 0;
}

/**
 * Compare two OPL3VoiceParam structures.
 * Returns 0 if equal, nonzero otherwise (compares all meaningful fields).
 */
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b) {
    // Compare operator parameters, mode, feedback, connection, source FM chip, and patch number
    if (a->is_4op != b->is_4op) return 1;
    if (memcmp(a->op, b->op, sizeof(a->op))) return 1;
    if (memcmp(a->fb, b->fb, sizeof(a->fb))) return 1;
    if (memcmp(a->cnt, b->cnt, sizeof(a->cnt))) return 1;
    if (a->source_fmchip != b->source_fmchip) return 1;
    if (a->patch_no != b->patch_no) return 1;
    return 0;
}

/**
 * Find a matching voice in the DB or add a new one. Returns its 0-based voice ID.
 */
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, const OPL3VoiceParam *p_vp) {
    for (int i = 0; i < p_db->count; ++i) {
        if (opl3_voice_param_cmp(&p_db->p_voices[i], p_vp) == 0) {
            return i;
        }
    }
    // Add new voice if not found
    if (p_db->count >= p_db->capacity) {
        p_db->capacity *= 2;
        p_db->p_voices = (OPL3VoiceParam *)realloc(p_db->p_voices, p_db->capacity * sizeof(OPL3VoiceParam));
    }
    p_db->p_voices[p_db->count] = *p_vp;
    return p_db->count++;
}

/**
 * Check if a given channel is in 4op mode (returns 2 for 4op, 1 for 2op).
 * Uses OPL3State pointer and channel index.
 */
int is_4op_channel(const OPL3State *p_state, int ch) {
    uint8_t reg_104 = p_state->reg[0x104];
    // OPL3 4op pairs: 0+3 (bit0), 1+4 (bit1), 2+5 (bit2)
    if (ch == 0 || ch == 3) return (reg_104 & 0x01) ? 2 : 1;
    if (ch == 1 || ch == 4) return (reg_104 & 0x02) ? 2 : 1;
    if (ch == 2 || ch == 5) return (reg_104 & 0x04) ? 2 : 1;
    // OPL3 4op is only for ch0-5
    return 1;
}

/**
 * Get the OPL3 channel mode (OPL3_MODE_2OP or OPL3_MODE_4OP).
 */
int get_opl3_channel_mode(const OPL3State *p_state, int ch) {
    return is_4op_channel(p_state, ch) == 2 ? OPL3_MODE_4OP : OPL3_MODE_2OP;
}

/**
 * Extract voice parameters for the given logical channel (0..17).
 * Fills out an OPL3VoiceParam structure.
 */
void extract_voice_param(const OPL3State *p_state, int ch, OPL3VoiceParam *p_out) {
    memset(p_out, 0, sizeof(OPL3VoiceParam));
    int mode = is_4op_channel(p_state, ch);
    p_out->is_4op = mode;
    int ch_main = ch;
    int ch_pair = -1;
    if (mode == 2) {
        // Determine 4op channel pair
        if (ch < 3) ch_pair = ch + 3;
        else if (ch >= 3 && ch <= 5) ch_pair = ch - 3;
    }
    int chs[2] = {ch_main, (mode == 2 ? ch_pair : -1)};
    for (int pair = 0; pair < (mode == 2 ? 2 : 1); ++pair) {
        int c = chs[pair];
        if (c < 0) continue;
        // Each 2op pair has 2 operators: slot = c + op*3
        for (int op = 0; op < 2; ++op) {
            int op_idx = c + op * 3;
            OPL3OperatorParam *opp = &p_out->op[pair * 2 + op];
            // The following assumes state contains OPL3 register mirror per spec
            opp->am   = (p_state->reg[0x20 + op_idx] >> 7) & 1;
            opp->vib  = (p_state->reg[0x20 + op_idx] >> 6) & 1;
            opp->egt  = (p_state->reg[0x20 + op_idx] >> 5) & 1;
            opp->ksr  = (p_state->reg[0x20 + op_idx] >> 4) & 1;
            opp->mult =  p_state->reg[0x20 + op_idx] & 0x0F;
            opp->ksl  = (p_state->reg[0x40 + op_idx] >> 6) & 3;
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
    // Set chip type and patch number if tracked elsewhere (leave as default otherwise)
    // p_out->source_fmchip and p_out->patch_no can be set by the caller if desired
}

/**
 * Returns the FM chip name string for the given FMChipType enum value.
 */
const char* fmchip_type_name(FMChipType type) {
    // Print FM chip type as string (if available)
    switch (type) {
        case FMCHIP_YM2413:   return "YM2413";
        case FMCHIP_YM2612:   return "YM2612";
        case FMCHIP_YM2151:   return "YM2151";
        case FMCHIP_YM2203:   return "YM2203";
        case FMCHIP_YM2608:   return "YM2608";
        case FMCHIP_YM2610:   return "YM2610";
        case FMCHIP_YM3812:   return "YM3812";
        case FMCHIP_YM3526:   return "YM3526";
        case FMCHIP_Y8950:    return "Y8950";
        case FMCHIP_YMF262:   return "YMF262";
        case FMCHIP_YMF278B:  return "YMF278B";
        case FMCHIP_YMF271:   return "YMF271";
        case FMCHIP_YMZ280B:  return "YMZ280B";
        case FMCHIP_2xYM2413:  return "2xYM2413";
        case FMCHIP_2xYM2612:  return "2xYM2612";
        case FMCHIP_2xYM2151:  return "2xYM2151";
        case FMCHIP_2xYM2203:  return "2xYM2203";
        case FMCHIP_2xYM2608:  return "2xYM2608";
        case FMCHIP_2xYM2610:  return "2xYM2610";
        case FMCHIP_2xYM3812:  return "2xYM3812";
        case FMCHIP_2xYM3526:  return "2xYM3526";
        case FMCHIP_2xY8950:   return "2xY8950";
        case FMCHIP_2xYMF262:  return "2xYMF262";
        case FMCHIP_2xYMF278B: return "2xYMF278B";
        case FMCHIP_2xYMF271:  return "2xYMF271";
        case FMCHIP_2xYMZ280B: return "2xYMZ280B";
        // Add more as needed
        default: return "UNKNOWN";
    }
}

/**
 * Detect which FM chip is present in the VGM header and used in the input file.
 * Returns FMChipType value.
 */
FMChipType detect_fmchip_from_header(const unsigned char *p_vgm_data, long filesize) {
    if (filesize < 0x70) return FMCHIP_NONE;
    // Check clock values for known chips in VGM header (see vgm_header.h offsets)
    if (*(uint32_t*)(p_vgm_data + 0x40)) return FMCHIP_YM2413;
    if (*(uint32_t*)(p_vgm_data + 0x2C)) return FMCHIP_YM2612;
    if (*(uint32_t*)(p_vgm_data + 0x30)) return FMCHIP_YM2151;
    if (*(uint32_t*)(p_vgm_data + 0x44)) return FMCHIP_YM2203;
    if (*(uint32_t*)(p_vgm_data + 0x48)) return FMCHIP_YM2608;
    if (*(uint32_t*)(p_vgm_data + 0x4C)) return FMCHIP_YM2610;
    if (*(uint32_t*)(p_vgm_data + 0x50)) return FMCHIP_YM3812;
    if (*(uint32_t*)(p_vgm_data + 0x54)) return FMCHIP_YM3526;
    if (*(uint32_t*)(p_vgm_data + 0x58)) return FMCHIP_Y8950;
    if (*(uint32_t*)(p_vgm_data + 0x5C)) return FMCHIP_YMF262;
    if (*(uint32_t*)(p_vgm_data + 0x60)) return FMCHIP_YMF278B;
    if (*(uint32_t*)(p_vgm_data + 0x64)) return FMCHIP_YMF271;
    if (*(uint32_t*)(p_vgm_data + 0x68)) return FMCHIP_YMZ280B;
    return FMCHIP_NONE;
}