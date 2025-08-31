#ifndef VGM_HEADER_H
#define VGM_HEADER_H

#include <stdint.h>
#include "vgm_helpers.h" // For VGMHeaderInfo, VGMGD3Tag, etc.

#define VGM_HEADER_SIZE 0x100
#define OPL3_CLOCK 14318182

/**
 * Builds a VGM header for OPL3/OPL2 output, preserving as much of the original as possible.
 * The header and offsets are configured per the given parameters and VGM format specification.
 *
 * @param p_header                Output buffer for the VGM header (must be at least max(input_header_size, VGM_HEADER_SIZE) bytes)
 * @param p_orig_vgm_header       Input VGM header (at least up to input_header_size bytes)
 * @param total_samples           New total samples (0x18)
 * @param eof_offset              New EOF offset (0x04)
 * @param gd3_offset              New GD3 offset (relative to 0x14)
 * @param data_offset             New data offset (relative to 0x34, usually 0xCC for 0x100-byte header)
 * @param version                 New VGM version (0x08)
 * @param additional_data_bytes   Additional bytes for port 1 (if any)
 */
void build_vgm_header(
    uint8_t *p_header,
    const uint8_t *p_orig_vgm_header,
    uint32_t total_samples,
    uint32_t eof_offset,
    uint32_t gd3_offset,
    uint32_t data_offset,
    uint32_t version,
    uint32_t additional_data_bytes
);

/**
 * Sets the YM2413 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2413
 */
void set_ym2413_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the YM3812 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM3812
 */
void set_ym3812_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the YM2151 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2151
 */
void set_ym2151_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the YM2612 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2612
 */
void set_ym2612_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the YM2203 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2203
 */
void set_ym2203_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the YM2608 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2608
 */
void set_ym2608_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YM2610 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2610
 */
void set_ym2610_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YM3526 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM3526
 */
void set_ym3526_clock(uint8_t *p_header, uint32_t value);

/**
 * Sets the Y8950 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for Y8950
 */
void set_y8950_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YMF262 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF262
 */
void set_ymf262_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YMF278B clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF278B
 */
void set_ymf278b_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YMF271 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF271
 */
void set_ymf271_clock(uint8_t *p_header, uint32_t value);


/**
 * Sets the YMZ280B clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMZ280B
 */
void set_ymz280b_clock(uint8_t *p_header, uint32_t value);

/**
 * Copies header and GD3 information from VGMContext to the VGM file buffer.
 * This can be used to finalize/export the file.
 * @param ctx Pointer to VGMContext holding header and GD3 data
 * @param out_buf Output buffer to which the header and GD3 block are written
 */
void vgm_export_header_and_gd3(const VGMContext *ctx, VGMBuffer *out_buf);

#endif // VGM_HEADER_H