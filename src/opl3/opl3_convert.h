#ifndef ESEOPL3PATCHER_OPL3_CONVERT_H
#define ESEOPL3PATCHER_OPL3_CONVERT_H

#include "../vgm/vgm_helpers.h"   /* FMChipType, CommandOptions, VGMBuffer, VGMStatus */
#include "opl3_state.h"
#include "../compat_bool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* struct キーワードを外し、typedef 名をそのまま使用 */
void opl3_init(
    VGMBuffer *p_music_data,
    int stereo_mode,
    OPL3State *p_state,
    FMChipType source_fmchip);

int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg,
    uint8_t val,
    const CommandOptions *opts,
    uint16_t next_wait_samples);

void opl3_write_reg(
    OPL3State *p_state,
    VGMBuffer *p_music_data,
    int port,
    uint8_t reg,
    uint8_t value);

double calc_fmchip_frequency(
    FMChipType chip,
    double clock,
    unsigned char block,
    unsigned short fnum);

// Carrier TL calculation (used during conversion)
uint8_t make_carrier_40_from_vol(const OPL3VoiceParam *vp, uint8_t reg3n);

/**
 * Find optimal FNUM and BLOCK values for a given frequency with error in cents.
 * @param freq Target frequency in Hz.
 * @param clock Chip clock frequency.
 * @param best_block Output: optimal block value.
 * @param best_fnum Output: optimal FNUM value.
 * @param best_err Output: error in cents.
 * @param pref_block Preferred block value (hint).
 * @param mult0 Multiplier 0 (reserved).
 * @param mult1 Multiplier 1 (reserved).
 */
void opl3_find_fnum_block_with_ml_cents(double freq, double clock,
                                        unsigned char *best_block, unsigned short *best_fnum,
                                        double *best_err,
                                        int pref_block,
                                        double mult0, double mult1);

#ifdef __cplusplus
}
#endif
#endif /* ESEOPL3PATCHER_OPL3_CONVERT_H */