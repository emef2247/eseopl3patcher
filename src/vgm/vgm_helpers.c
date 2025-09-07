#include "vgm_helpers.h"
#include <string.h>
#include <stdlib.h>

static uint32_t read_le_uint32(const unsigned char *p_ptr) {
    return (uint32_t)p_ptr[0] | ((uint32_t)p_ptr[1] << 8) | ((uint32_t)p_ptr[2] << 16) | ((uint32_t)p_ptr[3] << 24);
}


/**
 * Initialize a VGMBuffer structure.
 */
void vgm_buffer_init(VGMBuffer *p_buf) {
    p_buf->data = NULL;
    p_buf->size = 0;
    p_buf->capacity = 0;
}

/**
 * Append arbitrary bytes to a dynamic VGMBuffer.
 */
/**
 * Append arbitrary bytes to a dynamic VGMBuffer.
 */
void vgm_buffer_append(VGMBuffer *p_buf, const void *p_data, size_t len) {
    if (p_buf->size + len > p_buf->capacity) {
        size_t new_capacity = (p_buf->capacity ? p_buf->capacity * 2 : 256);
        while (new_capacity < p_buf->size + len) new_capacity *= 2;
        p_buf->data = realloc(p_buf->data, new_capacity);
        p_buf->capacity = new_capacity;
    }
    memcpy(p_buf->data + p_buf->size, p_data, len);
    p_buf->size += len;
}

/**
 * Release memory allocated for a VGMBuffer.
 */
/**
 * Release memory allocated for a VGMBuffer.
 */
void vgm_buffer_free(VGMBuffer *p_buf) {
    free(p_buf->data);
    p_buf->data = NULL;
    p_buf->size = 0;
    p_buf->capacity = 0;
}

/**
 * Append a single byte to the buffer.
 */
/**
 * Append a single byte to the buffer.
 */
void vgm_append_byte(VGMBuffer *p_buf, uint8_t value) {
    vgm_buffer_append(p_buf, &value, 1);
}

/**
 * Write an OPL3 register command (0x5E/0x5F) to the buffer.
 * port: 0 for port 0 (0x5E), 1 for port 1 (0x5F)
 */
/**
 * Write an OPL3 register command (0x5E/0x5F) to the buffer.
 * port: 0 for port 0 (0x5E), 1 for port 1 (0x5F)
 */
void forward_write(VGMBuffer *p_buf, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5E : 0x5F;
    uint8_t bytes[3] = {cmd, reg, val};
    vgm_buffer_append(p_buf, bytes, 3);
}

/**
 * Write a short wait command (0x70-0x7F) and update status.
 */
/**
 * Write a short wait command (0x70-0x7F) and update status.
 */
void vgm_wait_short(VGMBuffer *p_buf, VGMStatus *p_vstat, uint8_t cmd) {
    vgm_append_byte(p_buf, cmd);
    if (p_vstat) p_vstat->total_samples += (cmd & 0x0F) + 1;
}

/**
 * Write a wait n samples command (0x61) and update status.
 */
/**
 * Write a wait n samples command (0x61) and update status.
 */
void vgm_wait_samples(VGMBuffer *p_buf, VGMStatus *p_vstat, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    vgm_buffer_append(p_buf, bytes, 3);
    if (p_vstat) p_vstat->total_samples += samples;
}

/**
 * Write a wait 1/60s command (0x62) and update status.
 */
/**
 * Write a wait 1/60s command (0x62) and update status.
 */
void vgm_wait_60hz(VGMBuffer *p_buf, VGMStatus *p_vstat) {
    vgm_append_byte(p_buf, 0x62);
    if (p_vstat) p_vstat->total_samples += 735;
}

/**
 * Write a wait 1/50s command (0x63) and update status.
 */
/**
 * Write a wait 1/50s command (0x63) and update status.
 */
void vgm_wait_50hz(VGMBuffer *p_buf, VGMStatus *p_vstat) {
    vgm_append_byte(p_buf, 0x63);
    if (p_vstat) p_vstat->total_samples += 882;
}

/**
 * Wait for (cmd & 0x0F) + 1 samples (short wait) using VGMContext.
 */
/**
 * Wait for (cmd & 0x0F) + 1 samples (short wait) using VGMContext.
 */
void vgm_wait_short_ctx(VGMContext *ctx, uint8_t cmd) {
    vgm_append_byte(&ctx->buffer, cmd);
    uint32_t wait_samples = (cmd & 0x0F) + 1;
    vgm_timestamp_advance(&ctx->timestamp, wait_samples);
    ctx->status.total_samples += wait_samples;
}

/**
 * Wait for a specific number of samples using VGMContext.
 */
/**
 * Wait for a specific number of samples using VGMContext.
 */
void vgm_wait_samples_ctx(VGMContext *ctx, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    vgm_buffer_append(&ctx->buffer, bytes, 3);
    vgm_timestamp_advance(&ctx->timestamp, samples);
    ctx->status.total_samples += samples;
}

/**
 * Wait for 1/60 second (735 samples) using VGMContext.
 */
/**
 * Wait for 1/60 second (735 samples) using VGMContext.
 */
void vgm_wait_60hz_ctx(VGMContext *ctx) {
    vgm_append_byte(&ctx->buffer, 0x62);
    vgm_timestamp_advance(&ctx->timestamp, 735);
    ctx->status.total_samples += 735;
}

/**
 * Wait for 1/50 second (882 samples) using VGMContext.
 */
/**
 * Wait for 1/50 second (882 samples) using VGMContext.
 */
void vgm_wait_50hz_ctx(VGMContext *ctx) {
    vgm_append_byte(&ctx->buffer, 0x63);
    vgm_timestamp_advance(&ctx->timestamp, 882);
    ctx->status.total_samples += 882;
}


/**
 * Parse VGM header for FM chip clock values and flags.
 * Returns true if parsing is successful (header must be at least 0x70 bytes).
 */
bool vgm_parse_chip_clocks(const uint8_t *vgm_data, long filesize, VGMChipClockFlags *out_flags) {
    if (filesize < 0x70 || !vgm_data || !out_flags)
        return false;

    // VGM spec offsets
    out_flags->ym2413_clock  = read_le_uint32(vgm_data + 0x10);
    out_flags->ym3812_clock  = read_le_uint32(vgm_data + 0x50);
    out_flags->ym3526_clock  = read_le_uint32(vgm_data + 0x54);
    out_flags->y8950_clock   = read_le_uint32(vgm_data + 0x58);

    // Set bool flags if clock is nonzero
    out_flags->has_ym2413   = (out_flags->ym2413_clock  != 0);
    out_flags->has_ym3812   = (out_flags->ym3812_clock  != 0);
    out_flags->has_ym3526   = (out_flags->ym3526_clock  != 0);
    out_flags->has_y8950    = (out_flags->y8950_clock   != 0);

    return true;
}

/**
 * Returns the name of the FM chip selected for conversion in chip_flags.
 * Only one chip should be selected for conversion; if multiple are selected, returns the first found.
 * If none is selected, returns "UNKNOWN".
 */
const char* get_converted_opl_chip_name(const VGMChipClockFlags* chip_flags) {
    if (chip_flags->has_ym2413)  return "YM2413";
    if (chip_flags->has_ym3812)  return "YM3812";
    if (chip_flags->has_ym3526)  return "YM3526";
    if (chip_flags->has_y8950)   return "Y8950";
    return "UNKNOWN";
}
