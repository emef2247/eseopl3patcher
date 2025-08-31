#include "vgm_header.h"
#include <string.h>
#include <stdint.h>

/**
 * Write a 32-bit value as little-endian into the given buffer.
 */
static void write_le32(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * Read a 32-bit little-endian value from the given buffer.
 */
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/**
 * Build a VGM header for OPL3/OPL2 output, preserving as much of the original as possible.
 * The header and offsets are configured per the given parameters and VGM format specification.
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
) {
    uint32_t orig_data_offset = 0;
    uint32_t orig_header_size = VGM_HEADER_SIZE;
    if (p_orig_vgm_header) {
        orig_data_offset = read_le32(p_orig_vgm_header + 0x34);
        orig_header_size = 0x34 + orig_data_offset;
        if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;
    }
    uint32_t new_header_size = (orig_header_size > VGM_HEADER_SIZE) ? orig_header_size : VGM_HEADER_SIZE;
    uint32_t actual_data_offset = new_header_size - 0x34;

    memset(p_header, 0, new_header_size);

    // Copy the original header if available
    if (p_orig_vgm_header) {
        memcpy(p_header, p_orig_vgm_header, orig_header_size);
    }

    // Set mandatory header fields
    p_header[0] = 'V'; p_header[1] = 'g'; p_header[2] = 'm'; p_header[3] = ' ';
    write_le32(p_header + 0x04, eof_offset); // EOF offset
    write_le32(p_header + 0x08, version);    // Version
    write_le32(p_header + 0x34, actual_data_offset); // Data offset

    // Adjust GD3 offset (0x14): relative to 0x14
    uint32_t actual_gd3_offset = gd3_offset;
    if (new_header_size > VGM_HEADER_SIZE) {
        actual_gd3_offset += (new_header_size - VGM_HEADER_SIZE);
    }
    write_le32(p_header + 0x14, actual_gd3_offset);

    write_le32(p_header + 0x18, total_samples); // Total samples

    // Read original loop offset, loop samples, and rate
    uint32_t loop_offset_orig = 0xFFFFFFFF;
    uint32_t loop_samples_orig = 0;
    uint32_t rate_orig = 0;
    if (p_orig_vgm_header) {
        loop_offset_orig  = read_le32(p_orig_vgm_header + 0x1C);
        loop_samples_orig = read_le32(p_orig_vgm_header + 0x20);
        rate_orig         = read_le32(p_orig_vgm_header + 0x24);
    }
    uint32_t new_loop_offset = loop_offset_orig;

    // If loop offset is valid, adjust by header size difference (new_header_size - orig_header_size)
    if (loop_offset_orig != 0xFFFFFFFF) {
        int32_t header_diff = (int32_t)new_header_size - (int32_t)orig_header_size;
        new_loop_offset = loop_offset_orig + header_diff;
        if (additional_data_bytes > 0) {
            new_loop_offset += additional_data_bytes;
        }
    }
    write_le32(p_header + 0x1C, new_loop_offset);    // Loop offset
    write_le32(p_header + 0x20, loop_samples_orig);  // Loop samples
    write_le32(p_header + 0x24, rate_orig);          // Rate
}

/**
 * Set the YM2413 clock value in the VGM header.
 */
void set_ym2413_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x40, value);
}

/**
 * Set the YM3812 clock value in the VGM header.
 */
void set_ym3812_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x50, value);
}

/**
 * Set the YM2151 clock value in the VGM header.
 */
void set_ym2151_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x30, value);
}

/**
 * Set the YM2612 clock value in the VGM header.
 */
void set_ym2612_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x2C, value);
}

/**
 * Set the YM2203 clock value in the VGM header.
 */
void set_ym2203_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x44, value);
}

/**
 * Set the YM2608 clock value in the VGM header.
 */
void set_ym2608_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x48, value);
}

/**
 * Set the YM2610 clock value in the VGM header.
 */
void set_ym2610_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x4C, value);
}

/**
 * Set the YM3526 clock value in the VGM header.
 */
void set_ym3526_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x54, value);
}

/**
 * Set the Y8950 clock value in the VGM header.
 */
void set_y8950_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x58, value);
}

/**
 * Set the YMF262 (OPL3) clock value in the VGM header.
 */
void set_ymf262_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x5C, value);
}

/**
 * Set the YMF278B clock value in the VGM header.
 */
void set_ymf278b_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x60, value);
}

/**
 * Set the YMF271 clock value in the VGM header.
 */
void set_ymf271_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x64, value);
}

/**
 * Set the YMZ280B clock value in the VGM header.
 */
void set_ymz280b_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x68, value);
}

/**
 * Export the header and GD3 information from VGMContext into the output buffer.
 * Copies the header and GD3 block in order.
 */
void vgm_export_header_and_gd3(const VGMContext *ctx, VGMBuffer *out_buf) {
    // Write header (0x100 bytes)
    vgm_buffer_append(out_buf, ctx->header.raw, VGM_HEADER_SIZE);

    // Write GD3 tag if present and size > 0
    if (ctx->gd3.data && ctx->gd3.size > 0) {
        vgm_buffer_append(out_buf, ctx->gd3.data, ctx->gd3.size);
    }
}