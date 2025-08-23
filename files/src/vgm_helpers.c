#include "vgm_helpers.h"
#include <stdlib.h>
#include <string.h>

// Initialize a dynamic buffer
void buffer_init(dynbuffer_t* buf) {
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Free the memory used by a dynamic buffer
void buffer_free(dynbuffer_t* buf) {
    if (buf->data) free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Append arbitrary data to the dynamic buffer, expanding its capacity as needed
void buffer_append(dynbuffer_t* buf, const void* data, size_t len) {
    if (buf->size + len > buf->capacity) {
        size_t new_capacity = (buf->capacity + len) * 2 + 64;
        buf->data = realloc(buf->data, new_capacity);
        buf->capacity = new_capacity;
    }
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

// Append a 32-bit little-endian integer to the buffer
void buffer_append_le32(dynbuffer_t* buf, uint32_t value) {
    uint8_t le[4] = {
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF),
        (uint8_t)((value >> 16) & 0xFF),
        (uint8_t)((value >> 24) & 0xFF)
    };
    buffer_append(buf, le, 4);
}

// Append a UTF-16LE null-terminated string to the buffer (additional null for GD3 separator)
void buffer_append_utf16le_nullterm(dynbuffer_t* buf, const char* s) {
    while (*s) {
        uint16_t wc = (uint8_t)(*s++);
        buffer_append(buf, &wc, 2);
    }
    uint16_t zero = 0;
    buffer_append(buf, &zero, 2);
}

// Build the GD3 tag according to the VGM GD3 specification
void build_gd3_tag(dynbuffer_t* buf, const char** fields, size_t field_count) {
    // "Gd3 "
    buffer_append(buf, "Gd3 ", 4);
    // Version 0x00000100
    buffer_append_le32(buf, 0x00000100);
    // Placeholder for body size
    size_t size_offset = buf->size;
    buffer_append_le32(buf, 0);

    // Prepare the GD3 body buffer
    dynbuffer_t gd3body = {0};
    buffer_init(&gd3body);

    for (size_t i = 0; i < field_count; ++i) {
        buffer_append_utf16le_nullterm(&gd3body, fields[i]);
        uint16_t zero = 0;
        buffer_append(&gd3body, &zero, 2); // Separator
    }
    uint16_t zero = 0;
    buffer_append(&gd3body, &zero, 2); // Final separator

    // Write the correct size
    uint32_t gd3_body_size = (uint32_t)gd3body.size;
    memcpy(buf->data + size_offset, &gd3_body_size, 4);

    // Append the GD3 body to the main buffer
    buffer_append(buf, gd3body.data, gd3body.size);

    buffer_free(&gd3body);
}

// Write a register to OPL3 port
void forward_write(dynbuffer_t *buf, int port, uint8_t reg, uint8_t val) {
    uint8_t cmd = (port == 0) ? 0x5A : 0x5E;
    uint8_t bytes[3] = {cmd, reg, val};
    buffer_append(buf, bytes, 3);
}

// Wait for N samples (0x61 command)
void vgm_wait_samples(dynbuffer_t *buf, vgm_status_t *vstat, uint16_t samples) {
    uint8_t bytes[3] = {0x61, samples & 0xFF, samples >> 8};
    buffer_append(buf, bytes, 3);
    if (vstat) vstat->new_total_samples += samples;
}