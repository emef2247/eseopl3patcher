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

/**
 * Sets the YM2413 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2413
 */
void set_ym2413_clock(uint8_t *p_header, uint32_t value) {
    // YM2413 clock is at 0x44, OPL3 at 0x50
    p_header[0x10] = (value) & 0xFF;
    p_header[0x11] = (value >> 8) & 0xFF;
    p_header[0x12] = (value >> 16) & 0xFF;
    p_header[0x13] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM2612 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2612
 */
void set_ym2612_clock(uint8_t *p_header, uint32_t value){
    // YM2612 clock is at 0x2C
    p_header[0x2C] = (value) & 0xFF;
    p_header[0x2D] = (value >> 8) & 0xFF;
    p_header[0x2E] = (value >> 16) & 0xFF;
    p_header[0x2F] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM2151 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2151
 */
void set_ym2151_clock(uint8_t *p_header, uint32_t value) {
    // YM2151 clock is at 0x30
    p_header[0x30] = (value) & 0xFF;
    p_header[0x31] = (value >> 8) & 0xFF;
    p_header[0x32] = (value >> 16) & 0xFF;
    p_header[0x33] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM2203 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2203
 */
void set_ym2203_clock(uint8_t *p_header, uint32_t value){
    // YM2203 clock is at 0x44
    p_header[0x44] = (value) & 0xFF;
    p_header[0x45] = (value >> 8) & 0xFF;
    p_header[0x46] = (value >> 16) & 0xFF;
    p_header[0x47] = (value >> 24) & 0xFF;
}


/**
 * Sets the YM2608 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2608
 */
void set_ym2608_clock(uint8_t *p_header, uint32_t value){
    // YM2608 clock is at 0x48
    p_header[0x48] = (value) & 0xFF;
    p_header[0x49] = (value >> 8) & 0xFF;
    p_header[0x4A] = (value >> 16) & 0xFF;
    p_header[0x4B] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM2610 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM2610
 */
void set_ym2610_clock(uint8_t *p_header, uint32_t value) {
    // YM2610 clock is at 0x4C
    p_header[0x4C] = (value) & 0xFF;
    p_header[0x4D] = (value >> 8) & 0xFF;
    p_header[0x4E] = (value >> 16) & 0xFF;
    p_header[0x4F] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM3812 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM3812
 */
void set_ym3812_clock(uint8_t *p_header, uint32_t value) {
    // YM3812 clock is at 0x50
    p_header[0x50] = (value) & 0xFF;
    p_header[0x51] = (value >> 8) & 0xFF;
    p_header[0x52] = (value >> 16) & 0xFF;
    p_header[0x53] = (value >> 24) & 0xFF;
}

/**
 * Sets the YM3526 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YM3526
 */
void set_ym3526_clock(uint8_t *p_header, uint32_t value) {
    // YM3526 clock is at 0x54
    p_header[0x54] = (value) & 0xFF;
    p_header[0x55] = (value >> 8) & 0xFF;
    p_header[0x56] = (value >> 16) & 0xFF;
    p_header[0x57] = (value >> 24) & 0xFF;
}

/**
 * Sets the Y8950 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for Y8950
 */
void set_y8950_clock(uint8_t *p_header, uint32_t value) {
    // Y8950 clock is at 0x58
    p_header[0x58] = (value) & 0xFF;
    p_header[0x59] = (value >> 8) & 0xFF;
    p_header[0x5A] = (value >> 16) & 0xFF;
    p_header[0x5B] = (value >> 24) & 0xFF;
}


/**
 * Sets the YMF262 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF262
 */
void set_ymf262_clock(uint8_t *p_header, uint32_t value) {
    // YMF262 clock is at 0x60
    p_header[0x60] = (value) & 0xFF;
    p_header[0x61] = (value >> 8) & 0xFF;
    p_header[0x62] = (value >> 16) & 0xFF;
    p_header[0x63] = (value >> 24) & 0xFF;
}


/**
 * Sets the YMF278B clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF278B
 */
void set_ymf278b_clock(uint8_t *p_header, uint32_t value) {
    // YMF278B clock is at 0x60
    p_header[0x60] = (value) & 0xFF;
    p_header[0x61] = (value >> 8) & 0xFF;
    p_header[0x62] = (value >> 16) & 0xFF;
    p_header[0x63] = (value >> 24) & 0xFF;
}


/**
 * Sets the YMF271 clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMF271
 */
void set_ymf271_clock(uint8_t *p_header, uint32_t value) {
    // YMF271 clock is at 0x64
    p_header[0x64] = (value) & 0xFF;
    p_header[0x65] = (value >> 8) & 0xFF;
    p_header[0x66] = (value >> 16) & 0xFF;
    p_header[0x67] = (value >> 24) & 0xFF;
}


/**
 * Sets the YMZ280B clock value in the VGM header
 * @param p_header Pointer to the VGM header
 * @param value The clock value for YMZ280B
 */
void set_ymz280b_clock(uint8_t *p_header, uint32_t value) {
    // YMZ280B clock is at 0x68
    p_header[0x68] = (value) & 0xFF;
    p_header[0x69] = (value >> 8) & 0xFF;
    p_header[0x6A] = (value >> 16) & 0xFF;
    p_header[0x6B] = (value >> 24) & 0xFF;
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