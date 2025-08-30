#include "vgm_header.h"
#include <string.h>
#include <stdlib.h>

// Build a VGM header for OPL3/OPL2 output
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
    // Copy original header up to VGM_HEADER_SIZE or original size
    memcpy(p_header, p_orig_vgm_header, VGM_HEADER_SIZE);

    // Set version
    p_header[0x08] = (version) & 0xFF;
    p_header[0x09] = (version >> 8) & 0xFF;
    p_header[0x0A] = (version >> 16) & 0xFF;
    p_header[0x0B] = (version >> 24) & 0xFF;

    // Set EOF offset (0x04)
    p_header[0x04] = (eof_offset) & 0xFF;
    p_header[0x05] = (eof_offset >> 8) & 0xFF;
    p_header[0x06] = (eof_offset >> 16) & 0xFF;
    p_header[0x07] = (eof_offset >> 24) & 0xFF;

    // Set total # samples (0x18)
    p_header[0x18] = (total_samples) & 0xFF;
    p_header[0x19] = (total_samples >> 8) & 0xFF;
    p_header[0x1A] = (total_samples >> 16) & 0xFF;
    p_header[0x1B] = (total_samples >> 24) & 0xFF;

    // Set GD3 offset (0x14)
    p_header[0x14] = (gd3_offset) & 0xFF;
    p_header[0x15] = (gd3_offset >> 8) & 0xFF;
    p_header[0x16] = (gd3_offset >> 16) & 0xFF;
    p_header[0x17] = (gd3_offset >> 24) & 0xFF;

    // Set data offset (0x34)
    p_header[0x34] = (data_offset) & 0xFF;
    p_header[0x35] = (data_offset >> 8) & 0xFF;
    p_header[0x36] = (data_offset >> 16) & 0xFF;
    p_header[0x37] = (data_offset >> 24) & 0xFF;

    // Set additional_data_bytes if needed (not always used)
    if (additional_data_bytes) {
        // This is a placeholder for any additional patching required
    }
}

// Set the YMF262 (OPL3) clock value in the VGM header and zero the YM3812 clock
void set_opl3_clock(uint8_t *p_header, uint32_t opl3_clock) {
    // OPL3 clock is at 0x50, YM3812 at 0x44
    p_header[0x50] = (opl3_clock) & 0xFF;
    p_header[0x51] = (opl3_clock >> 8) & 0xFF;
    p_header[0x52] = (opl3_clock >> 16) & 0xFF;
    p_header[0x53] = (opl3_clock >> 24) & 0xFF;
    // Zero YM3812 clock
    p_header[0x44] = 0; p_header[0x45] = 0; p_header[0x46] = 0; p_header[0x47] = 0;
}

// Set the YM3812 clock value in the VGM header and zero the OPL3 clock
void set_ym3812_clock(uint8_t *p_header, uint32_t ym3812_clock) {
    // YM3812 clock is at 0x44, OPL3 at 0x50
    p_header[0x44] = (ym3812_clock) & 0xFF;
    p_header[0x45] = (ym3812_clock >> 8) & 0xFF;
    p_header[0x46] = (ym3812_clock >> 16) & 0xFF;
    p_header[0x47] = (ym3812_clock >> 24) & 0xFF;
    // Zero OPL3 clock
    p_header[0x50] = 0; p_header[0x51] = 0; p_header[0x52] = 0; p_header[0x53] = 0;
}

/**
 * Write the VGM header and GD3 block from VGMContext to the output VGMBuffer.
 * This function appends the header (0x100 bytes), then the GD3 block (if any).
 */
void vgm_export_header_and_gd3(const VGMContext *ctx, VGMBuffer *out_buf) {
    // Write VGM header (always 0x100 bytes)
    vgm_buffer_append(out_buf, ctx->header.raw, sizeof(ctx->header.raw));

    // If GD3 tag exists and has size, write GD3 block after header
    if (ctx->gd3.data && ctx->gd3.size > 0) {
        vgm_buffer_append(out_buf, ctx->gd3.data, ctx->gd3.size);
    }
    // Optionally, more metadata can be exported here in the future.
}