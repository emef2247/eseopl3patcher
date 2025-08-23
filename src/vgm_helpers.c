#include "vgm_helpers.h"
#include <string.h>
#include <stdlib.h>

// Initialize dynamic buffer
void buffer_init(dynbuffer_t *buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Append bytes to dynamic buffer
void buffer_append(dynbuffer_t *buf, const void *data, size_t len) {
    if (buf->size + len > buf->capacity) {
        size_t new_capacity = (buf->capacity ? buf->capacity * 2 : 256);
        while (new_capacity < buf->size + len) new_capacity *= 2;
        buf->data = realloc(buf->data, new_capacity);
        buf->capacity = new_capacity;
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

// Free dynamic buffer
void buffer_free(dynbuffer_t *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Append a single byte
void vgm_append_byte(dynbuffer_t *buf, uint8_t value) {
    buffer_append(buf, &value, 1);
}

// Write a register to OPL3 port
void forward_write(dynbuffer_t *buf, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5E : 0x5F;
    uint8_t bytes[3] = {cmd, reg, val};
    buffer_append(buf, bytes, 3);
}

// Wait commands
void vgm_wait_short(dynbuffer_t *buf, vgm_status_t *vstat, uint8_t cmd) {
    vgm_append_byte(buf, cmd);
    vstat->new_total_samples += (cmd & 0x0F) + 1;
}

void vgm_wait_samples(dynbuffer_t *buf, vgm_status_t *vstat, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    buffer_append(buf, bytes, 3);
    vstat->new_total_samples += samples;
}

void vgm_wait_60hz(dynbuffer_t *buf, vgm_status_t *vstat) {
    vgm_append_byte(buf, 0x62);
    vstat->new_total_samples += 735;
}

void vgm_wait_50hz(dynbuffer_t *buf, vgm_status_t *vstat) {
    vgm_append_byte(buf, 0x63);
    vstat->new_total_samples += 882;
}