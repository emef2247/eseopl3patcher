#include "vgm_header.h"
#include <string.h>
#include <stdint.h>

// Write a uint32_t as little-endian
static void write_le32(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

// Read a uint32_t as little-endian
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/**
 * Build a VGM header for OPL3/OPL2 output, preserving as much of the original as possible.
 * - The header size is determined by the original VGM header's data_offset (0x34) + 0x34.
 * - Copies the original header, then overwrites specified fields.
 * - If the header size changes, loop offset is updated accordingly using the header size difference.
 * - Clock values are NOT changed; use set_opl3_clock/set_ym3812_clock after this function if needed.
 *
 * @param header                Output buffer for the VGM header (must be at least max(input_header_size, VGM_HEADER_SIZE) bytes)
 * @param orig_vgm_header       Input VGM header (at least up to input_header_size bytes)
 * @param total_samples         New total samples (0x18)
 * @param eof_offset            New EOF offset (0x04)
 * @param gd3_offset            New GD3 offset (relative to 0x14)
 * @param data_offset           New data offset (relative to 0x34, usually 0xCC for 0x100-byte header)
 * @param version               New VGM version (0x08)
 * @param additional_data_bytes Additional bytes for port 1 (if any)
 */
void build_vgm_header(
    uint8_t *header,
    const uint8_t *orig_vgm_header,
    uint32_t total_samples,
    uint32_t eof_offset,
    uint32_t gd3_offset,
    uint32_t data_offset,
    uint32_t version,
    uint32_t additional_data_bytes
) {
    // Compute original header size from input data_offset
    uint32_t orig_data_offset = 0;
    uint32_t orig_header_size = VGM_HEADER_SIZE;
    if (orig_vgm_header) {
        orig_data_offset = read_le32(orig_vgm_header + 0x34);
        orig_header_size = 0x34 + orig_data_offset;
        if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;
    }

    // Set new header size and actual data offset
    uint32_t new_header_size = (orig_header_size > VGM_HEADER_SIZE)
        ? orig_header_size
        : VGM_HEADER_SIZE;
    uint32_t actual_data_offset = new_header_size - 0x34;

    memset(header, 0, new_header_size);

    // Copy the original header if available
    if (orig_vgm_header) {
        memcpy(header, orig_vgm_header, orig_header_size);
    }

    // Overwrite mandatory header fields
    header[0] = 'V'; header[1] = 'g'; header[2] = 'm'; header[3] = ' ';
    write_le32(header + 0x04, eof_offset);    // EOF offset
    write_le32(header + 0x08, version);       // Version
    write_le32(header + 0x34, actual_data_offset); // Data offset

    // Adjust GD3 offset (0x14): relative to 0x14. If header size increases, adjust accordingly
    uint32_t actual_gd3_offset = gd3_offset;
    if (new_header_size > VGM_HEADER_SIZE) {
        actual_gd3_offset += (new_header_size - VGM_HEADER_SIZE);
    }
    write_le32(header + 0x14, actual_gd3_offset);

    write_le32(header + 0x18, total_samples); // Total samples

    // Read original loop offset, loop samples, and rate
    uint32_t loop_offset_orig = 0xFFFFFFFF;
    uint32_t loop_samples_orig = 0;
    uint32_t rate_orig = 0;
    if (orig_vgm_header) {
        loop_offset_orig  = read_le32(orig_vgm_header + 0x1C);
        loop_samples_orig = read_le32(orig_vgm_header + 0x20);
        rate_orig         = read_le32(orig_vgm_header + 0x24);
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
    write_le32(header + 0x1C, new_loop_offset);    // Loop offset
    write_le32(header + 0x20, loop_samples_orig);  // Loop samples
    write_le32(header + 0x24, rate_orig);          // Rate
}

/**
 * Set the YMF262 (OPL3) clock value in the VGM header
 * @param header Pointer to the VGM header
 * @param opl3_clock The clock value for YMF262 (OPL3)
 */
void set_opl3_clock(uint8_t *header, uint32_t opl3_clock) {
    write_le32(header + 0x5C, opl3_clock);    // YMF262 (OPL3) clock
}

/**
 * Set the YM3812 clock value in the VGM header
 * @param header Pointer to the VGM header
 * @param ym3812_clock The clock value for YM3812
 */
void set_ym3812_clock(uint8_t *header, uint32_t ym3812_clock) {
    write_le32(header + 0x50, ym3812_clock);  // YM3812 clock
}