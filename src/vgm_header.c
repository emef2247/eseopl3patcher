#include "vgm_helpers.h"
#include <string.h>
#include <stdlib.h>

// Initialize dynamic buffer
void buffer_init(dynbuffer_t *p_buf) {
    p_buf->p_data = NULL;
    p_buf->size = 0;
    p_buf->capacity = 0;
}

// Append bytes to dynamic buffer
void buffer_append(dynbuffer_t *p_buf, const void *p_data, size_t len) {
    if (p_buf->size + len > p_buf->capacity) {
        size_t new_capacity = (p_buf->capacity ? p_buf->capacity * 2 : 256);
        while (new_capacity < p_buf->size + len) new_capacity *= 2;
        p_buf->p_data = realloc(p_buf->p_data, new_capacity);
        p_buf->capacity = new_capacity;
    }
    memcpy(p_buf->p_data + p_buf->size, p_data, len);
    p_buf->size += len;
}

// Free dynamic buffer
void buffer_free(dynbuffer_t *p_buf) {
    free(p_buf->p_data);
    p_buf->p_data = NULL;
    p_buf->size = 0;
    p_buf->capacity = 0;
}

// Append a single byte
void vgm_append_byte(dynbuffer_t *p_buf, uint8_t value) {
    buffer_append(p_buf, &value, 1);
}

// Write a register to OPL3 port
void forward_write(dynbuffer_t *p_buf, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5E : 0x5F;
    uint8_t bytes[3] = {cmd, reg, val};
    buffer_append(p_buf, bytes, 3);
}

// Wait commands
void vgm_wait_short(dynbuffer_t *p_buf, vgm_status_t *p_vstat, uint8_t cmd) {
    vgm_append_byte(p_buf, cmd);
    p_vstat->new_total_samples += (cmd & 0x0F) + 1;
}

void vgm_wait_samples(dynbuffer_t *p_buf, vgm_status_t *p_vstat, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    buffer_append(p_buf, bytes, 3);
    p_vstat->new_total_samples += samples;
}

void vgm_wait_60hz(dynbuffer_t *p_buf, vgm_status_t *p_vstat) {
    vgm_append_byte(p_buf, 0x62);
    p_vstat->new_total_samples += 735;
}

void vgm_wait_50hz(dynbuffer_t *p_buf, vgm_status_t *p_vstat) {
    vgm_append_byte(p_buf, 0x63);
    p_vstat->new_total_samples += 882;
}