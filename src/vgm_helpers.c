#include "vgm_helpers.h"
#include <string.h>
#include <stdlib.h>

// Initialize dynamic buffer
void buffer_init(dynbuffer_t *pBuf) {
    pBuf->pData = NULL;
    pBuf->size = 0;
    pBuf->capacity = 0;
}

// Append bytes to dynamic buffer
void buffer_append(dynbuffer_t *pBuf, const void *data, size_t len) {
    if (pBuf->size + len > pBuf->capacity) {
        size_t new_capacity = (pBuf->capacity ? pBuf->capacity * 2 : 256);
        while (new_capacity < pBuf->size + len) new_capacity *= 2;
        pBuf->pData = realloc(pBuf->pData, new_capacity);
        pBuf->capacity = new_capacity;
    }
    memcpy(pBuf->pData + pBuf->size, data, len);
    pBuf->size += len;
}

// Free dynamic buffer
void buffer_free(dynbuffer_t *pBuf) {
    free(pBuf->pData);
    pBuf->pData = NULL;
    pBuf->size = 0;
    pBuf->capacity = 0;
}

// Append a single byte
void vgm_append_byte(dynbuffer_t *pBuf, uint8_t value) {
    buffer_append(pBuf, &value, 1);
}

// Write a register to OPL3 port
void forward_write(dynbuffer_t *pBuf, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5E : 0x5F;
    uint8_t bytes[3] = {cmd, reg, val};
    buffer_append(pBuf, bytes, 3);
}

// Wait commands
void vgm_wait_short(dynbuffer_t *pBuf, vgm_status_t *pVstat, uint8_t cmd) {
    vgm_append_byte(pBuf, cmd);
    pVstat->new_total_samples += (cmd & 0x0F) + 1;
}

void vgm_wait_samples(dynbuffer_t *pBuf, vgm_status_t *pVstat, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    buffer_append(pBuf, bytes, 3);
    pVstat->new_total_samples += samples;
}

void vgm_wait_60hz(dynbuffer_t *pBuf, vgm_status_t *pVstat) {
    vgm_append_byte(pBuf, 0x62);
    pVstat->new_total_samples += 735;
}

void vgm_wait_50hz(dynbuffer_t *pBuf, vgm_status_t *pVstat) {
    vgm_append_byte(pBuf, 0x63);
    pVstat->new_total_samples += 882;
}