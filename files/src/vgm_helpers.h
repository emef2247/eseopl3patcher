#ifndef VGM_HELPERS_H
#define VGM_HELPERS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Generic dynamic buffer structure
typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} dynbuffer_t;

// Status structure for VGM conversion (expand as needed)
typedef struct {
    uint32_t new_total_samples;
} vgm_status_t;

// Buffer operations
void buffer_init(dynbuffer_t *buf);
void buffer_free(dynbuffer_t *buf);

// Append data to dynamic buffer
void buffer_append(dynbuffer_t *buf, const void *data, size_t len);
void buffer_append_le32(dynbuffer_t *buf, uint32_t value);
void buffer_append_utf16le_nullterm(dynbuffer_t *buf, const char *s);

// Build GD3 tag according to VGM GD3 specification
void build_gd3_tag(dynbuffer_t *buf, const char **fields, size_t field_count);

// VGM output helpers
void forward_write(dynbuffer_t *buf, int port, uint8_t reg, uint8_t val);
void vgm_wait_samples(dynbuffer_t *buf, vgm_status_t *vstat, uint16_t samples);

// Convenient VGM command helpers (inline)
static inline void vgm_append_byte(dynbuffer_t *buf, uint8_t val) {
    buffer_append(buf, &val, 1);
}

static inline void vgm_wait_short(dynbuffer_t *buf, vgm_status_t *vstat, uint8_t cmd) {
    vgm_append_byte(buf, cmd);
    if (vstat) vstat->new_total_samples += (cmd & 0x0F) + 1;
}

static inline void vgm_wait_60hz(dynbuffer_t *buf, vgm_status_t *vstat) {
    vgm_append_byte(buf, 0x62);
    if (vstat) vstat->new_total_samples += 735;
}

static inline void vgm_wait_50hz(dynbuffer_t *buf, vgm_status_t *vstat) {
    vgm_append_byte(buf, 0x63);
    if (vstat) vstat->new_total_samples += 882;
}

#endif // VGM_HELPERS_H