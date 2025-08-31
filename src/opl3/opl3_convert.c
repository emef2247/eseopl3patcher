#include "opl3_convert.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "vgm_helpers.h"
#include "opl3_voice.h"
#include "opl3_debug_util.h"

<<<<<<< Updated upstream
=======
extern int verbose;

// OPLL (YM2413) preset to OPL3 parameter conversion table
// Each entry maps a OPLL instrument (0-14) to OPL3 operator parameters
typedef struct {
    // Operator 1 parameters
    uint8_t mult1, ar1, dr1, sl1, rr1, am1, vib1, ksr1;
    // Operator 2 parameters
    uint8_t mult2, ar2, dr2, sl2, rr2, am2, vib2, ksr2;
    uint8_t tl;      // Total Level for Operator 2 (OPLL TL affects only OP2)
    uint8_t fb;      // Feedback (FB)
    uint8_t alg;     // Algorithm (ALG)
    uint8_t wf;      // Waveform
} OPLL2OPL3Patch;

// Conversion table for OPLL preset instruments (No.0〜14), No.15 is user patch (handled separately)
static const OPLL2OPL3Patch opll_to_opl3_table[15] = {
    // Violin (No.0)
    {1,12,6,9,6,0,0,0, 1,12,6,9,6,0,0,0, 0, 1,0, 0},
    // Guitar (No.1)
    {2,15,4,8,5,0,0,0, 1,12,6,9,6,0,0,0, 3, 2,0, 0},
    // Piano (No.2)
    {1,14,8,7,4,0,0,0, 2,12,7,8,5,0,0,0, 2, 1,0, 0},
    // Flute (No.3)
    {1,13,7,8,6,0,1,0, 1,12,6,9,6,0,0,0, 0, 0,0, 0},
    // Clarinet (No.4)
    {1,12,6,9,5,0,0,0, 1,11,6,8,5,0,0,0, 1, 0,0, 0},
    // Trumpet (No.5)
    {2,15,5,7,4,1,0,0, 1,13,6,8,5,0,0,0, 0, 1,0, 0},
    // Oboe (No.6)
    {1,14,7,9,6,0,0,0, 1,12,6,8,5,0,0,0, 0, 0,0, 0},
    // Organ (No.7)
    {1,12,8,15,15,0,0,0, 1,12,8,15,15,0,0,0, 0, 0,0, 0},
    // Bell (No.8)
    {1,15,4,0,1,0,0,0, 1,15,4,0,1,0,0,0, 0, 0,0, 0},
    // Marimba (No.9)
    {2,15,6,5,3,0,0,0, 1,12,7,6,3,0,0,0, 2, 1,0, 0},
    // Bass (No.10)
    {1,13,8,6,4,0,0,0, 1,12,7,5,3,0,0,0, 0, 2,0, 0},
    // SynthLead (No.11)
    {2,15,5,7,4,1,0,0, 1,14,6,8,5,1,0,0, 1, 2,0, 0},
    // SynthPad (No.12)
    {1,12,8,9,6,0,1,0, 1,12,8,9,6,0,0,0, 0, 1,0, 0},
    // Sitar (No.13)
    {3,15,5,7,3,0,0,0, 1,12,6,8,5,0,0,0, 3, 1,0, 0},
    // Vibraphone (No.14)
    {2,14,6,7,4,0,1,0, 1,12,6,8,5,0,0,0, 1, 0,0, 0},
    // User patch (No.15) is not included; handled by user patch data
};

// Convert OPLL preset instrument to OPL3 voice parameters and register as a voice
// Arguments:
//   state: pointer to OPL3State structure (voice_db will be updated)
//   inst: OPLL instrument number (0-14 for presets)
// Returns:
//   Registered voice ID (>=0 if success, <0 if error)
int register_opll_patch_as_opl3_voice(OPL3State *state, int inst) {
    if (inst < 0 || inst >= 15) {
        // Invalid instrument number
        return -1;
    }

    // Fetch patch parameters from the conversion table
    const OPLL2OPL3Patch *patch = &opll_to_opl3_table[inst];

    // Prepare OPL3VoiceParam structure and fill with converted parameters
    OPL3VoiceParam vparam;
    memset(&vparam, 0, sizeof(vparam));

    vparam.is_4op = 0; // OPLL patches are always 2op
    vparam.source_fmchip = state->source_fmchip; // Set the source FM chip type from state (should be FMCHIP_YM2413)
    vparam.patch_no = inst; // Set the patch number

    // Operator 1
    vparam.op[0].mult = patch->mult1;
    vparam.op[0].ar   = patch->ar1;
    vparam.op[0].dr   = patch->dr1;
    vparam.op[0].sl   = patch->sl1;
    vparam.op[0].rr   = patch->rr1;
    vparam.op[0].am   = patch->am1;
    vparam.op[0].vib  = patch->vib1;
    vparam.op[0].ksr  = patch->ksr1;

    // Operator 2
    vparam.op[1].mult = patch->mult2;
    vparam.op[1].ar   = patch->ar2;
    vparam.op[1].dr   = patch->dr2;
    vparam.op[1].sl   = patch->sl2;
    vparam.op[1].rr   = patch->rr2;
    vparam.op[1].am   = patch->am2;
    vparam.op[1].vib  = patch->vib2;
    vparam.op[1].ksr  = patch->ksr2;
    // OPLL TL applies only to OP2

    // Feedback and connection type (algorithm)
    vparam.fb[0]  = patch->fb;
    vparam.cnt[0] = patch->alg;

    // Waveform; set for both operators if needed
    vparam.op[0].ws = patch->wf;
    vparam.op[1].ws = patch->wf;

    // Register this voice into the voice database and return voice ID
    int voice_id = opl3_voice_db_find_or_add(&state->voice_db, &vparam);
    return voice_id;
}

>>>>>>> Stashed changes
// Write register value and update state flags
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value) {
    uint16_t addr = reg + (port ? 0x100 : 0x000);
    if (addr < OPL3_REGISTER_SIZE) {
        p_state->reg[addr] = value;
    }
    // Update internal state flags based on register values
    if (addr == 0xBD || addr == 0x1BD) {
        p_state->rhythm_mode = (value & 0x20) != 0;
    }
    if (addr == 0x105) {
        p_state->opl3_mode_initialized = (value & 0x01) != 0;
    }
    // Actually write to the output as before
    forward_write(p_music_data, port, reg, value);
}

// Apply TL (Total Level) attenuation using volume ratio
uint8_t apply_tl_with_ratio(uint8_t orig_val, double v_ratio) {
    if (v_ratio == 1.0) return orig_val;
    uint8_t tl = orig_val & 0x3F;
    double dB = 0.0;
    if (v_ratio < 1.0) {
        dB = -20.0 * log10(v_ratio); // 20*log10(ratio) [dB]
    }
    // 1 step = 0.75dB
    int add = (int)(dB / 0.75 + 0.5); // round to nearest
    int new_tl = tl + add;
    if (new_tl > 63) new_tl = 63;
    return (orig_val & 0xC0) | (new_tl & 0x3F);
}

// Detune helper for FM channels (used for frequency detune effects)
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB) {
    if (ch >= 6 && p_state->rhythm_mode) {
        *p_outA = regA;
        *p_outB = regB;
        return;
    }
    uint16_t fnum = ((regB & 3) << 8) | regA;
    double delta = fnum * (detune / 100.0);
    int fnum_detuned = (int)(fnum + delta + 0.5);
    if (fnum_detuned < 0) fnum_detuned = 0;
    if (fnum_detuned > 1023) fnum_detuned = 1023;
    *p_outA = (uint8_t)(fnum_detuned & 0xFF);
    *p_outB = (regB & 0xFC) | ((fnum_detuned >> 8) & 3);
}

// Judge OPL3 register type from register offset (for conversion logic)
opl3_regtype_t opl3_judge_regtype(uint8_t reg) {
    if (reg >= 0xA0 && reg <= 0xA8) return OPL3_REGTYPE_FREQ_LSB;
    if (reg >= 0xB0 && reg <= 0xB8) return OPL3_REGTYPE_FREQ_MSB;
    if (reg >= 0xC0 && reg <= 0xC8) return OPL3_REGTYPE_CH_CTRL;
    if (reg >= 0x40 && reg <= 0x55) return OPL3_REGTYPE_OP_TL;
    if (reg == 0xBD) return OPL3_REGTYPE_RHYTHM_BD;
    return OPL3_REGTYPE_OTHER;
}

// Apply register value to OPL3/OPL2 ports (multi-port/stereo logic)
// All per-channel register operations are routed here
int apply_to_ports(const opl3_convert_ctx_t *ctx) {
    int port1_bytes = 0;
    switch (ctx->reg_type) {
        case OPL3_REGTYPE_FREQ_LSB: {
            // 0xA0 + ch (Frequency LSB)
            int addr = 0xA0 + ctx->ch;
            if ((ctx->p_state->reg[addr] & 0x20)) {
                opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xA0 + ctx->ch, ctx->val);
            }
            break;
        }
        case OPL3_REGTYPE_FREQ_MSB: {
            // 0xB0 + ch (Frequency MSB, KEYON/BLOCK)
            int addr_regA = 0xA0 + ctx->ch;
            int addr_regB = 0xB0 + ctx->ch;
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, addr_regB, ctx->val);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, addr_regA, ctx->p_state->reg[addr_regA]);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, addr_regB, ctx->val);
            if (ctx->p_vstat) {
                vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);
            }
            // Apply detune if requested and write to port 1 (chorus effect)
            uint8_t detunedA, detunedB;
            detune_if_fm(ctx->p_state, ctx->ch, ctx->p_state->reg[addr_regA], ctx->p_state->reg[addr_regB], ctx->detune, &detunedA, &detunedB);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, addr_regA, detunedA); port1_bytes += 3;
            if (!(ctx->ch >= 6 && ctx->ch <= 8 && ctx->p_state->rhythm_mode)) {
                opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, addr_regB, detunedB); port1_bytes += 3;
            }
            if (ctx->p_vstat) {
                vgm_wait_samples(ctx->p_music_data, ctx->p_vstat, ctx->opl3_keyon_wait);
            }
            // Only extract and register voice parameters when KeyOn (bit5) is set
            if (ctx->val & 0x20) {
                // Extract and register voice parameters
                OPL3VoiceParam vparam;
                extract_voice_param(ctx->p_state, ctx->ch, &vparam);
                // Set source_fmchip and patch_no from state defaults (for YM3812/OPL3 use FMCHIP_YM3812 etc.)
                vparam.source_fmchip = ctx->p_state->source_fmchip;
                vparam.patch_no = -1; // Unknown patch_no for non-preset chips unless tracked separately
                int prev_voice_count = ctx->p_state->voice_db.count;
                int voice_id = opl3_voice_db_find_or_add(&ctx->p_state->voice_db, &vparam);
                if (voice_id >= 0 && ctx->p_state->voice_db.count > prev_voice_count) {
                    print_opl3_voice_param(ctx->p_state, &vparam);
                } else if (voice_id < 0) {
                    fprintf(stderr, "Error: Failed to find or add voice parameters for channel %d\n", ctx->ch);
                }
            }
            break;
        }
        case OPL3_REGTYPE_CH_CTRL: {
            // 0xC0 + ch (Channel control: Feedback, Algorithm, Panning)
            uint8_t port0_panning, port1_panning;
            if (ctx->ch_panning) {
                if (ctx->ch % 2 == 0) {
                    port0_panning = 0x50;  // Right channel (bit 4/6)
                    port1_panning = 0xA0;  // Left channel (bit 5/7)
                } else {
                    port0_panning = 0xA0;  // Left channel
                    port1_panning = 0x50;  // Right channel
                }
            } else {
                port0_panning = 0xA0; // Left
                port1_panning = 0x50; // Right
            }
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0xC0 + ctx->ch, ctx->val | port0_panning);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0xC0 + ctx->ch, ctx->val | port1_panning); port1_bytes += 3;
            break;
        }
        case OPL3_REGTYPE_OP_TL: {
            // 0x40 + slot (Operator TL: Total Level)
            uint8_t val0 = apply_tl_with_ratio(ctx->val, ctx->v_ratio0);
            uint8_t val1 = apply_tl_with_ratio(ctx->val, ctx->v_ratio1);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 0, 0x40 + ctx->ch, val0);
            opl3_write_reg(ctx->p_state, ctx->p_music_data, 1, 0x40 + ctx->ch, val1); port1_bytes += 3;
            break;
        }
        default: break;
    }
    return port1_bytes;
}

// Rhythm mode register handler
void handle_bd(VGMBuffer *p_music_data, OPL3State *p_state, uint8_t val) {
    p_state->rhythm_mode = (val & 0x20) != 0;
    opl3_write_reg(p_state, p_music_data, 0, 0xBD, val);
}

// Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring)
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
) {
    int port1_bytes = 0;
    opl3_regtype_t reg_type = opl3_judge_regtype(reg);

    if (reg_type == OPL3_REGTYPE_FREQ_LSB || reg_type == OPL3_REGTYPE_FREQ_MSB ||
        reg_type == OPL3_REGTYPE_CH_CTRL || reg_type == OPL3_REGTYPE_OP_TL) {
        int ch;
        if (reg_type == OPL3_REGTYPE_FREQ_LSB)      ch = reg - 0xA0;
        else if (reg_type == OPL3_REGTYPE_FREQ_MSB) ch = reg - 0xB0;
        else if (reg_type == OPL3_REGTYPE_CH_CTRL)  ch = reg - 0xC0;
        else /* OPL3_REGTYPE_OP_TL */               ch = reg - 0x40;
        opl3_convert_ctx_t ctx = {
            .p_music_data = p_music_data,
            .p_vstat = p_vstat,
            .p_state = p_state,
            .ch = ch,
            .reg_type = reg_type,
            .reg = reg,
            .val = val,
            .detune = detune,
            .opl3_keyon_wait = opl3_keyon_wait,
            .ch_panning = ch_panning,
            .v_ratio0 = v_ratio0,
            .v_ratio1 = v_ratio1
        };
        port1_bytes += apply_to_ports(&ctx);
    } else if (reg_type == OPL3_REGTYPE_RHYTHM_BD) {
        handle_bd(p_music_data, p_state, val);
    } else {
        // Write unknown or non-channel register to both ports (for mirroring)
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); port1_bytes += 3;
    }
    return port1_bytes;
}

// OPL3 initialization sequence
// Now takes FMChipType to set default source_fmchip in OPL3State
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip) {
    memset(p_state->reg, 0, OPL3_REGISTER_SIZE);
    p_state->rhythm_mode = false;
    p_state->opl3_mode_initialized = false;
    p_state->source_fmchip = source_fmchip; // Set default FM chip type for this conversion session
    opl3_voice_db_init(&p_state->voice_db);
    // OPL3 global registers
    opl3_write_reg(p_state, p_music_data, 1, 0x05, 0x01); // OPL3 enable
    opl3_write_reg(p_state, p_music_data, 1, 0x04, 0x00); // Waveform select
    opl3_write_reg(p_state, p_music_data, 0, 0x01, 0x00); // LSI TEST reg
    opl3_write_reg(p_state, p_music_data, 0, 0x08, 0x00); // NTS
    opl3_write_reg(p_state, p_music_data, 1, 0x01, 0x00);
    // Channel-level control
    for (uint8_t i = 0; i < 9; ++i) {
        if (stereo_mode) {
            uint8_t value0 = (i % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, value0);
        } else {
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, 0x50);
        }
        if (stereo_mode) {
            uint8_t value1 = ((i + 9) % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, value1);
        } else {
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, 0xA0);
        }
    }
    // Extended waveform select
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs)/sizeof(ext_regs[0]); ++i) {
        opl3_write_reg(p_state, p_music_data, 0, ext_regs[i], 0x00);
        opl3_write_reg(p_state, p_music_data, 1, ext_regs[i], 0x00);
    }
    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        opl3_write_reg(p_state, p_music_data, 1, reg, 0x00);
    }
}