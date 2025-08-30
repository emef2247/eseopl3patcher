#ifndef VGM_HEADER_H
#define VGM_HEADER_H

#include <stdint.h>

#define VGM_HEADER_SIZE 0x100
#define OPL3_CLOCK 14318182

void build_vgm_header(
    uint8_t *header,
    const uint8_t *orig_vgm_header,
    uint32_t total_samples,
    uint32_t eof_offset,
    uint32_t gd3_offset,
    uint32_t data_offset,
    uint32_t version,
    uint32_t additional_data_bytes
);

/**
 * Sets the YMF262 (OPL3) clock value in the VGM header and zeros the YM3812 clock.
 * @param header Pointer to the VGM header
 * @param opl3_clock The clock value for YMF262 (OPL3)
 */
void set_opl3_clock(uint8_t *pHeader, uint32_t opl3_clock);

/**
 * Sets the YM3812 clock value in the VGM header and zeros the OPL3 clock.
 * @param header Pointer to the VGM header
 * @param ym3812_clock The clock value for YM3812
 */
void set_ym3812_clock(uint8_t *pHeader, uint32_t ym3812_clock);

#endif // VGM_HEADER_H