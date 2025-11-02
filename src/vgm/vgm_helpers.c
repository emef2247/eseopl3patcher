#include "vgm_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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
void vgm_buffer_append(VGMBuffer *p_buf, const void *p_data, size_t len) {
    if (p_buf->size + len > p_buf->capacity) {
        size_t new_capacity = (p_buf->capacity ? p_buf->capacity * 2 : 256);
        while (new_capacity < p_buf->size + len) new_capacity *= 2;
        uint8_t *new_data = realloc(p_buf->data, new_capacity);
        if (!new_data) {
            // メモリ確保失敗
            fprintf(stderr, "vgm_buffer_append: realloc failed (request %zu bytes)\n", new_capacity);
            abort();
        }
        p_buf->data = new_data;
        p_buf->capacity = new_capacity;
    }
    memcpy(p_buf->data + p_buf->size, p_data, len);
    p_buf->size += len;
}

/**
 * Release memory allocated for a VGMBuffer.
 */
void vgm_buffer_free(VGMBuffer *p_buf) {
    if (p_buf && p_buf->data) {
        free(p_buf->data);
        p_buf->data = NULL;
    }
    p_buf->size = 0;
    p_buf->capacity = 0;
}

/**
 * Append a single byte to the buffer.
 */
int vgm_append_byte(VGMBuffer *p_buf, uint8_t value) {
    int add_bytes = 1;
    vgm_buffer_append(p_buf, &value, 1);
    return add_bytes;
}

/**
 * Write an OPL3 register command (0x5E/0x5F) to the buffer.
 * port: 0 for port 0 (0x5E), 1 for port 1 (0x5F)
 */
int forward_write(VGMContext *p_vgmctx, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5E : 0x5F;
    uint8_t bytes[3] = {cmd, reg, val};
    int add_bytes = 3;
    vgm_buffer_append(&(p_vgmctx->buffer), bytes, 3);
    return add_bytes;
}

/**
 * Write a short wait command (0x70-0x7F) and update status.
 */
/**
 * Write a short wait command (0x70-0x7F) and update status.
 */
int vgm_wait_short(VGMContext *p_vgmctx, uint8_t cmd) {
    int add_bytes = 0;
    add_bytes = vgm_append_byte(&(p_vgmctx->buffer), cmd);
    if (p_vgmctx) {
        p_vgmctx->timestamp.last_sample = p_vgmctx->timestamp.current_sample;
        p_vgmctx->timestamp.current_sample += (cmd & 0x0F) + 1;
        p_vgmctx->status.total_samples += (cmd & 0x0F) + 1;
    }
    return add_bytes;
}

/**
 * Write a wait n samples command (0x61) and update status.
 * Zero-length waits are skipped (not written to stream) as they are unnecessary.
 */
int vgm_wait_samples(VGMContext *p_vgmctx, uint16_t samples) {
    int add_bytes = 0;
    // Skip zero-length waits entirely (verified on real hardware)
    if (samples == 0) {
        return add_bytes;
    }
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    vgm_buffer_append(&(p_vgmctx->buffer), bytes, 3);
    add_bytes = 3;
    if (p_vgmctx) {
        p_vgmctx->timestamp.last_sample = p_vgmctx->timestamp.current_sample;
        p_vgmctx->timestamp.current_sample += samples;
        p_vgmctx->status.total_samples += samples;
    } 
    return add_bytes;
}

/**
 * Write a wait 1/60s command (0x62) and update status.
 */
int vgm_wait_60hz(VGMContext *p_vgmctx) {
    int add_bytes = 0;
    add_bytes = vgm_append_byte(&(p_vgmctx->buffer), 0x62);
    if (p_vgmctx) {
        p_vgmctx->timestamp.last_sample = p_vgmctx->timestamp.current_sample;
        p_vgmctx->timestamp.current_sample += 735;
        p_vgmctx->status.total_samples += 735;
    }
    return add_bytes;
}

/**
 * Write a wait 1/50s command (0x63) and update status.
 */
int vgm_wait_50hz(VGMContext *p_vgmctx) {
    int add_bytes = 0;
    add_bytes = vgm_append_byte(&(p_vgmctx->buffer), 0x63);
    if (p_vgmctx) {
        p_vgmctx->timestamp.last_sample = p_vgmctx->timestamp.current_sample;
        p_vgmctx->timestamp.current_sample += 882;
        p_vgmctx->status.total_samples += 882;
    }
    return add_bytes;
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

const char* get_opll_preset_type(const OPLL_PresetType type) {
    if (type == OPLL_PresetType_YM2413)  return "YM2413";
    if (type == OPLL_PresetType_VRC7)  return "VRC7";
    if (type == OPLL_PresetType_YMF281B)  return "YMF281B";
    if (type == OPLL_PresetType_YM2423)  return "YM2423";
    return "UNKNOWN";
}

const char* get_opll_preset_source(const OPLL_PresetSource source) {
    if (source == OPLL_PresetSource_YMVOICE)  return "YM-VOICE";
    if (source == OPLL_PresetSource_YMFM)  return "YMFM";
    if (source == OPLL_PresetSource_EXPERIMENT)  return "EXPERIMENT";
    return "UNKNOWN";
}

const char* get_opll_convert_method(const OPLL_ConvertMethod type) {
    if (type == OPLL_ConvertMethod_VGMCONV)  return "VGM-CONV";
    if (type == OPLL_ConvertMethod_COMMANDBUFFER)  return "COMMAND_BUFFER";
    return "UNKNOWN";
}

