#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *pData;
    size_t size;
    size_t capacity;
} dynbuffer_t;

typedef struct {
    uint32_t new_total_samples;
} vgm_status_t;

// Dynamic buffer functions
void buffer_init(dynbuffer_t *pBuf);
void buffer_append(dynbuffer_t *pBuf, const void *data, size_t len);
void buffer_free(dynbuffer_t *pBuf);

// VGM helpers
void vgm_append_byte(dynbuffer_t *pBuf, uint8_t value);
void forward_write(dynbuffer_t *pBuf, int port, uint8_t reg, uint8_t val);
void vgm_wait_short(dynbuffer_t *pBuf, vgm_status_t *pVstat, uint8_t cmd);
void vgm_wait_samples(dynbuffer_t *pBuf, vgm_status_t *pVstat, uint16_t samples);
void vgm_wait_60hz(dynbuffer_t *pBuf, vgm_status_t *pVstat);
void vgm_wait_50hz(dynbuffer_t *pBuf, vgm_status_t *pVstat);