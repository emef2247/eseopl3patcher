#ifndef ESEOPL3PATCHER_OPL3_CONVERT_H
#define ESEOPL3PATCHER_OPL3_CONVERT_H

#include "../vgm/vgm_helpers.h"   /* FMChipType, CommandOptions, VGMBuffer, VGMStatus */
#include "opl3_state.h"
#include "../compat_bool.h"
#include "../vgm/vgm_helpers.h"
#include "../vgm/vgm_header.h"       /* OPL3_CLOCK */
#ifdef __cplusplus
extern "C" {
#endif

/* struct キーワードを外し、typedef 名をそのまま使用 */

int opl3_init(
    VGMContext *p_vpmctx,
    FMChipType source_fmchip,
    const CommandOptions *opts);

int duplicate_write_opl3(
    VGMContext *p_vpmctx,
    uint8_t reg,
    uint8_t val,
    const CommandOptions *p_opts);

void opl3_write_reg(
    VGMContext *p_vpmctx,
    int port,
    uint8_t reg,
    uint8_t value);

double calc_fmchip_frequency(
    FMChipType chip,
    double clock,
    unsigned char block,
    unsigned short fnum);

// Carrier TL calculation (used during conversion)
uint8_t make_carrier_40_from_vol(VGMContext *p_vpmctx,const OPL3VoiceParam *vp, uint8_t reg3n, const CommandOptions *p_opts);

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