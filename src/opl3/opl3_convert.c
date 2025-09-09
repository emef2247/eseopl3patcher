#include "opl3_convert.h"
#include "../opll/ym2413_voice_rom.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// --- Global FM conversion environment ---
static double g_fm_source_clock = 0;
static double g_fm_target_clock = OPL3_CLOCK;

void set_fm_source_clock(double hz) { g_fm_source_clock = hz; }
double get_fm_source_clock(void) { return g_fm_source_clock; }
void set_fm_target_clock(double hz) { g_fm_target_clock = hz; }
double get_fm_target_clock(void) { return g_fm_target_clock; }

/**
 * Calculate actual frequency for a given FM chip, clock, block, and FNUM.
 * Supports OPLL (YM2413), OPL3 (YMF262), OPL2 (YM3812), YM3526, Y8950.
 */
double calc_fmchip_frequency(FMChipType chip, double clock, int block, int fnum) {
    switch (chip) {
        case FMCHIP_YM2413: // OPLL
            // f_out = (clock / 72) * 2^(block - 1) * (FNUM / 512)
            return (clock / 72.0) * pow(2.0, block - 1) * ((double)fnum / 512.0);
        case FMCHIP_YMF262: // OPL3
            // f_out = (clock / 288) * 2^(block - 1) * (FNUM / 1024)
            return (clock / 288.0) * pow(2.0, block - 1) * ((double)fnum / 1024.0);
        case FMCHIP_YM3812: // OPL2
        case FMCHIP_YM3526:
        case FMCHIP_Y8950:
            // f_out = (clock / 72) * 2^(block - 1) * (FNUM / 1024)
            return (clock / 72.0) * pow(2.0, block - 1) * ((double)fnum / 1024.0);
        default:
            return 0.0;
    }
}

/**
 * Calculate frequency (Hz) from OPLL (YM2413) FNUM and block.
 * @param fnum 9-bit FNUM (0-511)
 * @param block 3-bit block (0-7)
 * @param clock OPLL source clock (Hz)
 * @return Frequency in Hz
 */
double opll_fnum_block_to_freq(int fnum, int block, double clock) {
    // f_out = (clock / 72) * (FNUM / 512) * 2^block
    double base = clock / 72.0;
    double freq = base * ((double)fnum / 512.0) * pow(2.0, block);
    return freq;
}


/**
 * Calculate OPL3 FNUM and block for a target frequency.
 * Output values are clipped to valid OPL3 ranges.
 * @param freq Frequency in Hz
 * @param clock OPL3 clock in Hz
 * @param[out] block Pointer to integer for resulting block
 * @param[out] fnum Pointer to integer for resulting FNUM
 */
void opl3_calc_fnum_block_from_freq(double freq, double clock, int* block, int* fnum) {
    if (freq <= 0.0 || clock <= 0.0) {
        *block = 0; *fnum = 0;
        return;
    }
    int best_block = 0;
    int best_fnum = 0;
    double min_err = 1e12;

    for (int b = 0; b < 8; ++b) {
        double base = (clock / 288.0) * pow(2.0, b);
        int cand_fnum = (int)(freq * 1024.0 / base + 0.5);
        if (cand_fnum < 0 || cand_fnum > 1023) continue;
        double calc_freq = base * ((double)cand_fnum / 1024.0);
        double err = fabs(calc_freq - freq);
        if (err < min_err) {
            min_err = err;
            best_block = b;
            best_fnum = cand_fnum;
        }
    }
    *block = best_block;
    *fnum = best_fnum;
}

void convert_opll_patch_to_opl3_param(int voice_index, OPL3OperatorParam *mod, OPL3OperatorParam *car) {
    if (voice_index < 0 || voice_index > 15 || !mod || !car) {
        memset(mod, 0, sizeof(OPL3OperatorParam));
        memset(car, 0, sizeof(OPL3OperatorParam));
        return;
    }
    const unsigned char *rom = YM2413_VOICES[voice_index];
    // Modulator
    mod->am   = (rom[0] >> 7) & 0x01;
    mod->vib  = (rom[0] >> 6) & 0x01;
    mod->egt  = (rom[0] >> 5) & 0x01;
    mod->ksr  = (rom[0] >> 4) & 0x01;
    mod->mult = rom[0] & 0x0F;
    mod->ksl  = (rom[1] >> 6) & 0x03;
    mod->tl   = rom[1] & 0x3F;
    mod->ar   = (rom[2] >> 4) & 0x0F;
    mod->dr   = rom[2] & 0x0F;
    mod->sl   = (rom[3] >> 4) & 0x0F;
    mod->rr   = rom[3] & 0x0F;
    mod->ws   = 0;
    // Carrier
    car->am   = (rom[8] >> 7) & 0x01;
    car->vib  = (rom[8] >> 6) & 0x01;
    car->egt  = (rom[8] >> 5) & 0x01;
    car->ksr  = (rom[8] >> 4) & 0x01;
    car->mult = rom[8] & 0x0F;
    car->ksl  = (rom[9] >> 6) & 0x03;
    car->tl   = rom[9] & 0x3F;
    car->ar   = (rom[10] >> 4) & 0x0F;
    car->dr   = rom[10] & 0x0F;
    car->sl   = (rom[11] >> 4) & 0x0F;
    car->rr   = rom[11] & 0x0F;
    car->ws   = 0;
    if (mod->ar < 2) mod->ar = 2;
    if (mod->dr < 2) mod->dr = 2;
    if (car->ar < 2) car->ar = 2;
    if (car->dr < 2) car->dr = 2;
}



int opl3_voiceparam_apply(VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    int ch, const OPL3VoiceParam *vp, double detune, int opl3_keyon_wait,
    int ch_panning, double v_ratio0, double v_ratio1)
{
    if (!vp || ch < 0 || ch >= 9) return 0;
    int bytes = 0;
    int slot_mod = ch;
    int slot_car = ch + 3;
    // Modulator
    uint8_t reg20_mod = (vp->op[0].am << 7) | (vp->op[0].vib << 6) | (vp->op[0].egt << 5) | (vp->op[0].ksr << 4) | (vp->op[0].mult & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_mod, reg20_mod, detune, opl3_keyon_wait,ch_panning, v_ratio0, v_ratio1);
    uint8_t reg40_mod = ((vp->op[0].ksl & 0x03) << 6) | (vp->op[0].tl & 0x3F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_mod, reg40_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t reg60_mod = ((vp->op[0].ar & 0x0F) << 4) | (vp->op[0].dr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_mod, reg60_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t reg80_mod = ((vp->op[0].sl & 0x0F) << 4) | (vp->op[0].rr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_mod, reg80_mod, detune, opl3_keyon_wait,ch_panning, v_ratio0, v_ratio1);
    uint8_t regE0_mod = (vp->op[0].ws & 0x07);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_mod, regE0_mod, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    // Carrier
    uint8_t reg20_car = (vp->op[1].am << 7) | (vp->op[1].vib << 6) | (vp->op[1].egt << 5) | (vp->op[1].ksr << 4) | (vp->op[1].mult & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x20 + slot_car, reg20_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t reg40_car = ((vp->op[1].ksl & 0x03) << 6) | (vp->op[1].tl & 0x3F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x40 + slot_car, reg40_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t reg60_car = ((vp->op[1].ar & 0x0F) << 4) | (vp->op[1].dr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x60 + slot_car, reg60_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t reg80_car = ((vp->op[1].sl & 0x0F) << 4) | (vp->op[1].rr & 0x0F);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0x80 + slot_car, reg80_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    uint8_t regE0_car = (vp->op[1].ws & 0x07);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xE0 + slot_car, regE0_car, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    // Feedback/connection
    uint8_t regC0 = ((vp->fb[0] & 0x07) << 1) | (vp->cnt[0] & 0x01);
    bytes += duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xC0 + ch, regC0, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
    return bytes;
}

/**
 * Attenuate TL (Total Level) using volume ratio.
 * @param orig_val Original TL register value.
 * @param v_ratio Volume ratio (0.0 .. 1.0).
 * @return TL value modified by ratio.
 */
static uint8_t apply_tl_with_ratio(uint8_t orig_val, double v_ratio) {
    if (v_ratio == 1.0) return orig_val;
    // TL is lower 6 bits
    uint8_t tl = orig_val & 0x3F;
    double dB = 0.0;
    if (v_ratio < 1.0) {
        dB = -20.0 * log10(v_ratio); // 20*log10(ratio) [dB]
    }
    // 1 step = 0.75dB
    int add = (int)(dB / 0.75 + 0.5); // to round off
    int new_tl = tl + add;
    if (new_tl > 63) new_tl = 63;
    return (orig_val & 0xC0) | (new_tl & 0x3F);
}

/**
 * Write a value to the OPL3 register mirror and update internal state flags.
 * Always writes to the register mirror (reg[]).
 * @param p_state Pointer to OPL3State.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param port OPL3 port (0 or 1).
 * @param reg Register address.
 * @param value Value to write.
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value) {
    int reg_addr = reg + (port ? 0x100 : 0x000);
    p_state->reg_stamp[reg_addr] = p_state->reg[reg_addr];
    p_state->reg[reg_addr] = value;
    // Update rhythm mode or OPL3 mode initialized flags if needed
    if (reg_addr == 0x105) {
        p_state->opl3_mode_initialized = (value & 0x01) != 0;
    }
    if (reg_addr == 0x0BD) {
        p_state->rhythm_mode = (value & 0x20) != 0;
    }
    // Write to VGM stream
    forward_write(p_music_data, port, reg, value);
}

/**
 * Detune helper for FM channels (used for frequency detune effects).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @param regA Frequency LSB.
 * @param regB Frequency MSB.
 * @param detune Detune amount (percent).
 * @param p_outA Pointer to output detuned LSB.
 * @param p_outB Pointer to output detuned MSB.
 */
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB) {
    // Rhythm channels (6,7,8 and 15,16,17) are not detuned in rhythm mode
    if ((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode)) {
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


/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus.
 * @param p_state Pointer to OPL3State.
 * @param reg Register address.
 * @param val Value to write.
 * @param detune Detune amount.
 * @param opl3_keyon_wait KeyOn/Off wait (in samples).
 * @param ch_panning Channel panning mode.
 * @param v_ratio0 Volume ratio for port 0.
 * @param v_ratio1 Volume ratio for port 1.
 * @return Bytes written to port 1.
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
) {
    int addtional_bytes = 0;

    if (reg == 0x01 || reg == 0x02 || reg == 0x03 || reg == 0x04) {
        // Write only to port 0
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg == 0x05) {
        // Handle mode register (only port 1)
        // Always OPL3 mode
        opl3_write_reg(p_state, p_music_data, 1, 0x05, val & 0x1);
    } else if (reg >= 0x40 && reg <= 0x55) {
        uint8_t val0 = apply_tl_with_ratio(val, v_ratio0);
        uint8_t val1 = apply_tl_with_ratio(val, v_ratio1);
        opl3_write_reg(p_state, p_music_data, 0, reg, val0);
        opl3_write_reg(p_state, p_music_data, 1, reg, val1); addtional_bytes += 3;
    } else if (reg >= 0xA0 && reg <= 0xA8) {
        // Only write port0 for A0..A8
        int ch = reg - 0xA0;

        if ((p_state->reg[0xB0 + ch]) & 0x20) {
            opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, val);
        } else {
            // Only update the register buffer (No dump to vgm)
            p_state->reg_stamp[reg] = p_state->reg[reg];
            p_state->reg[reg] = val;
        }
    } else if (reg >= 0xB0 && reg <= 0xB8) {
        // Only perform voice registration on KeyOn event (when writing to FREQ_MSB and KEYON bit transitions 0->1)
        // Get the previous and new KEYON bit values
        uint8_t prev_val = p_state->reg_stamp[reg];
        uint8_t keyon_prev = prev_val & 0x20;
        uint8_t keyon_new  = val & 0x20;
        // KeyOn occurs: extract and register voice parameters for this channel
        if (!keyon_prev && keyon_new) {
            OPL3VoiceParam vp;
            // Always zero-initialize the whole structure before extracting parameters
            memset(&vp, 0, sizeof(OPL3VoiceParam));
            // Extract voice parameters from the OPL3 state
            extract_voice_param(p_state, &vp);
            // Set additional fields as needed before DB registration
            vp.source_fmchip = p_state->source_fmchip;
            // Register or find voice in the database
            opl3_voice_db_find_or_add(&p_state->voice_db, &vp);
        }
        
        // Write B0 (KeyOn/Block/FnumMSB) and handle detune
        // forward_write(ctx->p_music_data, 0, 0xB0 + ch, ctx->val);
        int ch = reg - 0xB0;
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        opl3_write_reg(p_state, p_music_data, 0, 0xA0 + ch, p_state->reg[0xA0 + ch]);
        opl3_write_reg(p_state, p_music_data, 0, 0xB0 + ch, val);
        // Extra 3 bytes
        addtional_bytes += 3;
        vgm_wait_samples(p_music_data, p_vstat, opl3_keyon_wait);

        uint8_t detunedA, detunedB;
        detune_if_fm(p_state, ch, p_state->reg[0xA0 + ch], val, detune, &detunedA, &detunedB);
        opl3_write_reg(p_state, p_music_data, 1, 0xA0 + ch, detunedA); addtional_bytes += 3;
        if (!((ch >= 6 && ch <= 8 && p_state->rhythm_mode) || (ch >= 15 && ch <= 17 && p_state->rhythm_mode))) {
            opl3_write_reg(p_state, p_music_data, 1, reg, detunedB); addtional_bytes += 3;
        }
        vgm_wait_samples(p_music_data, p_vstat, opl3_keyon_wait);
    } else if (reg >= 0xC0 && reg <= 0xC8) {
        int ch = reg - 0xC0;
        // Stereo panning implementation based on channel number
        // Even channels: port0->right, port1->left
        // Odd channels: port0->left, port1->right
        // This creates alternating stereo placement for a stereo effect
        uint8_t port0_panning, port1_panning;
        if (ch_panning) {
            // Apply stereo panning
            if (ch % 2 == 0) {
                // Even channel: port0 gets right channel, port1 gets left channel
                port0_panning = 0x50;  // Right channel (bit 4 and bit 6)
                port1_panning = 0xA0;  // Left channel (bit 5 and bit 7)
            } else {
                // Odd channel: port0 gets left channel, port1 gets right channel
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
            }
        } else {
                port0_panning = 0xA0;  // Left channel (bit 5 and bit 7)
                port1_panning = 0x50;  // Right channel (bit 4 and bit 6)
        }
        opl3_write_reg(p_state, p_music_data, 0, 0xC0 + ch, (0xF & val) | port0_panning);
        opl3_write_reg(p_state, p_music_data, 1, 0xC0 + ch, (0xF & val) | port1_panning); addtional_bytes += 3;
    } else if (reg == 0xBD) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
    } else if (reg >= 0xE0 && reg <= 0xF5) {
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    } else {
        // Write to both ports
        opl3_write_reg(p_state, p_music_data, 0, reg, val);
        opl3_write_reg(p_state, p_music_data, 1, reg, val); addtional_bytes += 3;
    }
    return addtional_bytes;
}

/**
 * Register all YM2413 ROM patches into OPL3VoiceDB during OPL3 initialization.
 * Each ROM patch is converted to OPL3VoiceParam format and registered in the database.
 * YM2413_VOICES[0] = Violin, [1]=Guitar, ..., [14]=Synth Bass, [15]=User patch (not registered as preset).
 */
static void register_all_opll_patches_to_opl3_voice_db(OPL3VoiceDB *db) {
    // Register only preset patches [0..14] (Violin..Synth Bass)
    for (int i = 0; i < 15; ++i) {
        OPL3VoiceParam vp;
        memset(&vp, 0, sizeof(vp));
        // Convert YM2413 ROM patch to OPL3 operator parameters
        convert_opll_patch_to_opl3_param(i, &vp.op[0], &vp.op[1]);
        vp.is_4op = 0;
        vp.fb[0]  = 1; // Use FB=1 (safe default for OPL3, not silent)
        vp.cnt[0] = 0; // Use CNT=0 (algorithm 0, typical FM)
        vp.source_fmchip = FMCHIP_YM2413;
        vp.voice_no = i;
        opl3_voice_db_find_or_add(db, &vp);
    }
}

/**
 * OPL3 initialization sequence for both ports.
 * Sets FM chip type in OPL3State and initializes register mirror.
 */
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip) {
    if (!p_state) return;
    memset(p_state->reg, 0, sizeof(p_state->reg));
    memset(p_state->reg_stamp, 0, sizeof(p_state->reg_stamp));
    p_state->rhythm_mode = false;
    p_state->opl3_mode_initialized = false;
    p_state->source_fmchip = source_fmchip;

     // Initialize OPL3VoiceDB
    p_state->source_fmchip = source_fmchip;

     // Initialize OPL3VoiceDB
    opl3_voice_db_init(&p_state->voice_db);

    if (source_fmchip == FMCHIP_YM2413) {
        for (int i = 0; i < 16; ++i) {
            printf("ROM[%d]:", i);
            for (int j = 0; j < 16; ++j) printf(" %02x", YM2413_VOICES[i][j]);
            printf("\n");
        }
        // Register all OPLL presets as OPL3 voices (for patch lookup)
        register_all_opll_patches_to_opl3_voice_db(&p_state->voice_db);
    }

    // OPL3 global registers (Port 1 only)
    opl3_write_reg(p_state, p_music_data, 1, 0x05, 0x01);  // OPL3 enable
    opl3_write_reg(p_state, p_music_data, 1, 0x04, 0x00);  // Waveform select

    // Port 0 general init
    opl3_write_reg(p_state, p_music_data, 0, 0x01, 0x00);  // LSI TEST
    opl3_write_reg(p_state, p_music_data, 0, 0x08, 0x00);  // NTS

    // Port 1 general init
    opl3_write_reg(p_state, p_music_data, 1, 0x01, 0x00);

    // Channel-level control
    for (uint8_t i = 0; i < 9; ++i) {
        // Port 0: channels 0-8
        if (stereo_mode) {
            uint8_t value0 = (i % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, value0);
        } else {
            opl3_write_reg(p_state, p_music_data, 0, 0xC0 + i, 0x50);
        }
        // Port 1: channels 9-17 (registers 0xC0-0xC8)
        if (stereo_mode) {
            uint8_t value1 = ((i + 9) % 2 == 0) ? 0xA0 : 0x50;
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, value1);
        } else {
            opl3_write_reg(p_state, p_music_data, 1, 0xC0 + i, 0xA0);
        }
    }

    // Waveform select registers (port 0 and 1)
    const uint8_t ext_regs[] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF};
    for (size_t i = 0; i < sizeof(ext_regs) / sizeof(ext_regs[0]); ++i) {
        opl3_write_reg(p_state, p_music_data, 0, ext_regs[i], 0x00);
        opl3_write_reg(p_state, p_music_data, 1, ext_regs[i], 0x00);
    }
    for (uint8_t reg = 0xF0; reg <= 0xF5; ++reg) {
        opl3_write_reg(p_state, p_music_data, 1, reg, 0x00);
    }
}


// --- Patch conversion (ROM/user patch to OPL3VoiceParam) ---
static void ym2413_patch_to_opl3_voiceparam(int inst, const uint8_t *ym2413_regs, OPL3VoiceParam *vp) {
    memset(vp, 0, sizeof(OPL3VoiceParam));
    const uint8_t *src;
    if (inst == 0 && ym2413_regs) {
        src = ym2413_regs; // $00-$07: user patch
    } else {
        src = YM2413_VOICES[inst-1];
    }
    // Copy all fields as is (ranges are compatible)
    vp->op[0].am   = (src[0] >> 7) & 1;
    vp->op[0].vib  = (src[0] >> 6) & 1;
    vp->op[0].egt  = (src[0] >> 5) & 1;
    vp->op[0].ksr  = (src[0] >> 4) & 1;
    vp->op[0].mult = src[0] & 0x0F;
    vp->op[0].ksl  = (src[1] >> 6) & 3;
    vp->op[0].tl   = src[1] & 0x3F;
    vp->op[0].ar   = (src[2] >> 4) & 0x0F;
    vp->op[0].dr   = src[2] & 0x0F;
    vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    vp->op[0].rr   = src[3] & 0x0F;
    vp->op[0].ws   = 0;
    int ofs = (inst == 15 && ym2413_regs) ? 4 : 8;
    vp->op[1].am   = (src[ofs+0] >> 7) & 1;
    vp->op[1].vib  = (src[ofs+0] >> 6) & 1;
    vp->op[1].egt  = (src[ofs+0] >> 5) & 1;
    vp->op[1].ksr  = (src[ofs+0] >> 4) & 1;
    vp->op[1].mult = src[ofs+0] & 0x0F;
    vp->op[1].ksl  = (src[ofs+1] >> 6) & 3;
    vp->op[1].tl   = src[ofs+1] & 0x3F;
    vp->op[1].ar   = (src[ofs+2] >> 4) & 0x0F;
    vp->op[1].dr   = src[ofs+2] & 0x0F;
    vp->op[1].sl   = (src[ofs+3] >> 4) & 0x0F;
    vp->op[1].rr   = src[ofs+3] & 0x0F;
    vp->op[1].ws   = 0;
    vp->fb[0] = 1;
    vp->cnt[0] = 0;
    vp->is_4op = 0;
    vp->voice_no = inst;
    vp->source_fmchip = FMCHIP_YM2413;

    // --- AR/DR correction ---
    // OPL3でAR=0だと音が立ち上がらない場合があるため最小値を設定
    if (vp->op[0].ar < 2) vp->op[0].ar = 2;
    if (vp->op[1].ar < 2) vp->op[1].ar = 2;
    if (vp->op[0].dr < 2) vp->op[0].dr = 2;
    if (vp->op[1].dr < 2) vp->op[1].dr = 2;
}

// --- YM2413→OPL3 conversion state ---
#define YM2413_NUM_CH 9
#define YM2413_REGS_SIZE 0x40
static uint8_t ym2413_regs[YM2413_REGS_SIZE] = {0};
static uint8_t opl3_keyon_state[YM2413_NUM_CH] = {0}; // 0: KeyOff, 1: KeyOn


// --- YM2413→OPL3 register conversion main ---
static void debug_dump_opl3_voiceparam(int ch, const OPL3VoiceParam* vp, uint8_t fnum_lsb, uint8_t fnum_msb, uint8_t block, uint8_t keyon)
{
    printf("[DEBUG] KeyOn準備: ch=%d\n", ch);
    printf("  FNUM: %03X (LSB=0x%02X, MSB=0x%02X), Block=%u, KeyOn=%u\n", ((fnum_msb&0x03)<<8)|fnum_lsb, fnum_lsb, fnum_msb, block, keyon);
    printf("  Modulator: TL=%d AR=%d DR=%d SL=%d RR=%d MULT=%d KSL=%d AM=%d VIB=%d EGT=%d KSR=%d WS=%d\n",
        vp->op[0].tl, vp->op[0].ar, vp->op[0].dr, vp->op[0].sl, vp->op[0].rr,
        vp->op[0].mult, vp->op[0].ksl, vp->op[0].am, vp->op[0].vib, vp->op[0].egt, vp->op[0].ksr, vp->op[0].ws);
    printf("  Carrier:   TL=%d AR=%d DR=%d SL=%d RR=%d MULT=%d KSL=%d AM=%d VIB=%d EGT=%d KSR=%d WS=%d\n",
        vp->op[1].tl, vp->op[1].ar, vp->op[1].dr, vp->op[1].sl, vp->op[1].rr,
        vp->op[1].mult, vp->op[1].ksl, vp->op[1].am, vp->op[1].vib, vp->op[1].egt, vp->op[1].ksr, vp->op[1].ws);
    printf("  ALG=%u FB=%u CNT=%u\n", vp->cnt[0], vp->fb[0], vp->cnt[0]);
}

// --- YM2413→OPL3 register conversion main ---
int duplicate_write_opl3_ym2413(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    uint8_t reg, uint8_t val, double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
) {
    ym2413_regs[reg] = val;

    if (reg <= 0x07 || reg == 0x0E) return 0;

    // $10-$18: FNUM LSB (buffer only)
    if (reg >= 0x10 && reg <= 0x18) {
        int ch = reg - 0x10;
        ym2413_regs[reg] = val; // just buffer, do NOT emit yet
        return 0;
    }

    // $20-$28: KeyOn/FNUM MSB/Block
    if (reg >= 0x20 && reg <= 0x28) {
        int ch = reg - 0x20;
        ym2413_regs[reg] = val;

        uint8_t inst = (ym2413_regs[0x30 + ch] >> 4) & 0x0F;
        uint8_t vol  = ym2413_regs[0x30 + ch] & 0x0F;

        // TL/VOL correction
       // --- TL/VOL correction ---
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_voiceparam(inst, ym2413_regs, &vp);
        uint8_t vol_tl = (uint8_t)((15 - vol) / 15.0 * 63.0 + 0.5); // 0=最大, 63=最小
        vp.op[1].tl = vol_tl;
        vp.op[1].tl = 0; // for debug, always max volume  

        // --- KeyOff ---
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, 0x00, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
        //vgm_wait_samples(p_music_data, p_vstat, 1);

        // --- Patch apply ---
        opl3_voiceparam_apply(p_music_data, p_vstat, p_state, ch, &vp, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        // --- Frequency conversion (正しいFNUM/blockでHz計算) ---
        uint8_t  fnum_lsb = ym2413_regs[0x10 + ch];
        uint16_t fnum = ((val & 0x01) << 8) | fnum_lsb;          // FNUM正しい9bit
        uint8_t block = (val >> 1) & 0x07;                       // blockはbit1-3
        uint8_t keyon = (val & 0x10) ? 1 : 0;                    // KeyOnはbit4

        double freq = calc_fmchip_frequency(FMCHIP_YM2413, get_fm_source_clock(), block, fnum);
        int opl3_block, opl3_fnum;
        opl3_calc_fnum_block_from_freq(freq, get_fm_target_clock(), &opl3_block, &opl3_fnum);

        uint8_t out_lsb = opl3_fnum & 0xFF;
        uint8_t out_msb = ((opl3_fnum >> 8) & 0x03) | ((opl3_block & 0x07) << 2) | (keyon ? 0x20 : 0);

        // --- FNUM LSB ---
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xA0 + ch, out_lsb, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        // --- KeyOn/Off ---
        duplicate_write_opl3(p_music_data, p_vstat, p_state, 0xB0 + ch, out_msb, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);

        //if (keyon) vgm_wait_samples(p_music_data, p_vstat, OPL3_KEYON_WAIT_AFTER_ON_DEFAULT);
        //else      vgm_wait_samples(p_music_data, p_vstat, 1);

        // デバッグ出力
        printf("[DEBUG][YM2413->OPL3] ch=%d inst=%d vol=%d block=%d fnum=%03X keyon=%d freq=%.2fHz\n",
            ch, inst, vol, block, fnum, keyon, freq);

        return 0;
    }
    // $30-$38: instrument/volume (INST/VOL) immediate handling
    if (reg >= 0x30 && reg <= 0x38) {
        int ch = reg - 0x30;
        uint8_t inst = (val >> 4) & 0x0F;
        uint8_t vol  = val & 0x0F;

        // --- Register user patch if needed ---
        if (inst == 0) {
            OPL3VoiceParam user_vp;
            ym2413_patch_to_opl3_voiceparam(inst, ym2413_regs, &user_vp);
            int found = opl3_voice_db_find_or_add(&p_state->voice_db, &user_vp);
            printf("[DEBUG] User patch (INST=0) registered to voice_db (voice_no=%d, found=%d)\n", user_vp.voice_no, found);
        }

        // --- TL+VOL補正（即時反映） ---
        OPL3VoiceParam vp;
        ym2413_patch_to_opl3_voiceparam(inst, ym2413_regs, &vp);
        uint8_t vol_tl = (uint8_t)((15 - vol) / 15.0 * 63.0 + 0.5); // 0=最大, 63=最小
        vp.op[1].tl = vol_tl;
        vp.op[1].tl = 0; // for debug, always max volume  

        // TL書き込み
        duplicate_write_opl3(
            p_music_data, p_vstat, p_state,
            0x40 + ch, ((vp.op[0].ksl & 0x03) << 6) | (vp.op[0].tl & 0x3F), detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1
        );
        duplicate_write_opl3(
            p_music_data, p_vstat, p_state,
            0x40 + (ch + 3), ((vp.op[1].ksl & 0x03) << 6) | (vp.op[1].tl & 0x3F), detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1
        );
        printf("[DEBUG] VOL update: ch=%d inst=%d vol=%d -> TL_mod=%d (slot_mod=%d), TL_car=%d (slot_car=%d)\n",
            ch, inst, vol, vp.op[0].tl, ch, vp.op[1].tl, ch + 3);
        return 0;
    }

    return 0;
}