#include "gd3_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Helper: read little-endian 32-bit integer from byte array
static uint32_t read_le_uint32(const unsigned char *ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

// Helper: write little-endian 32-bit integer into byte array
static void write_le_uint32(unsigned char *ptr, uint32_t val) {
    ptr[0] = (val & 0xFF);
    ptr[1] = ((val >> 8) & 0xFF);
    ptr[2] = ((val >> 16) & 0xFF);
    ptr[3] = ((val >> 24) & 0xFF);
}

// Helper: decode UTF-16LE string to newly allocated UTF-8 string
static char *utf16le_to_utf8(const uint8_t *utf16, size_t bytes) {
    // Worst case: each UTF-16 code unit becomes 3 UTF-8 bytes + null
    char *utf8 = (char*)malloc(bytes * 2 + 1);
    size_t out = 0;
    for (size_t in = 0; in + 1 < bytes; in += 2) {
        uint16_t w = utf16[in] | (utf16[in+1] << 8);
        if (w == 0) break;
        if (w < 0x80) {
            utf8[out++] = w;
        } else if (w < 0x800) {
            utf8[out++] = 0xC0 | (w >> 6);
            utf8[out++] = 0x80 | (w & 0x3F);
        } else {
            utf8[out++] = 0xE0 | (w >> 12);
            utf8[out++] = 0x80 | ((w >> 6) & 0x3F);
            utf8[out++] = 0x80 | (w & 0x3F);
        }
    }
    utf8[out] = 0;
    return utf8;
}

// Helper: encode UTF-8 string as UTF-16LE to buffer, return bytes written
static size_t utf8_to_utf16le(const char *utf8, uint8_t *out) {
    size_t bytes = 0;
    while (*utf8) {
        unsigned char c = (unsigned char)*utf8++;
        uint16_t w = 0;
        if (c < 0x80) {
            w = c;
        } else if ((c & 0xE0) == 0xC0) {
            w = (c & 0x1F) << 6;
            c = (unsigned char)*utf8++;
            w |= (c & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            w = (c & 0x0F) << 12;
            c = (unsigned char)*utf8++;
            w |= (c & 0x3F) << 6;
            c = (unsigned char)*utf8++;
            w |= (c & 0x3F);
        }
        if (out) {
            out[bytes++] = (w & 0xFF);
            out[bytes++] = (w >> 8);
        } else {
            bytes += 2;
        }
    }
    // Null-terminator
    if (out) {
        out[bytes++] = 0; out[bytes++] = 0;
    } else {
        bytes += 2;
    }
    return bytes;
}

// Extracts GD3 fields from input VGM data, outputs UTF-8 strings for each field
int extract_gd3_fields(const unsigned char *vgm_data, long filesize,
                       char *gd3_fields[GD3_FIELDS],
                       uint32_t *out_ver, uint32_t *out_len) {
    // Find GD3 offset in VGM header (0x14)
    if (filesize < 0x18)
        return 1;
    uint32_t gd3_offset = read_le_uint32(vgm_data + 0x14);
    if (gd3_offset == 0)
        return 1;
    long gd3_absolute = 0x14 + gd3_offset;
    if (gd3_absolute + 12 > filesize)
        return 1;
    if (memcmp(vgm_data + gd3_absolute, "Gd3 ", 4) != 0)
        return 1;

    *out_ver = read_le_uint32(vgm_data + gd3_absolute + 4);
    *out_len = read_le_uint32(vgm_data + gd3_absolute + 8);

    const uint8_t *gd3_ptr = vgm_data + gd3_absolute + 12;
    const uint8_t *gd3_end = gd3_ptr + *out_len;
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Find UTF-16LE null-terminated string
        const uint8_t *str_start = gd3_ptr;
        while (gd3_ptr + 1 < gd3_end && (gd3_ptr[0] != 0 || gd3_ptr[1] != 0))
            gd3_ptr += 2;
        // Decode to UTF-8
        gd3_fields[i] = utf16le_to_utf8(str_start, gd3_ptr - str_start);
        gd3_ptr += 2; // skip null terminator
    }
    return 0;
}

// Build a new GD3 chunk from fields, creator, notes.
void build_new_gd3_chunk(dynbuffer_t *gd3_buf,
                         char *gd3_fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *append_creator,
                         const char *append_notes) {
    // Compose new fields, append creator and notes
    char *new_fields[GD3_FIELDS];
    for (int i = 0; i < GD3_FIELDS; ++i) {
        if (i == 6 && append_creator) {
            // Append creator to Original Creator field
            size_t len1 = strlen(gd3_fields[i]);
            size_t len2 = strlen(append_creator);
            new_fields[i] = (char*)malloc(len1 + len2 + 2);
            strcpy(new_fields[i], gd3_fields[i]);
            strcat(new_fields[i], append_creator);
        } else if (i == 7 && append_notes) {
            // Append notes to Original Notes field
            size_t len1 = strlen(gd3_fields[i]);
            size_t len2 = strlen(append_notes);
            new_fields[i] = (char*)malloc(len1 + len2 + 2);
            strcpy(new_fields[i], gd3_fields[i]);
            strcat(new_fields[i], append_notes);
        } else {
            new_fields[i] = strdup(gd3_fields[i]);
        }
    }

    // Calculate UTF-16LE field length
    size_t total_utf16 = 0;
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Each field is null-terminated in UTF-16LE
        total_utf16 += utf8_to_utf16le(new_fields[i], NULL);
    }

    // GD3 chunk header
    uint8_t header[12] = { 'G', 'd', '3', ' ',
        0,0,0,0, // version
        0,0,0,0  // length
    };
    write_le_uint32(header + 4, orig_ver ? orig_ver : 0x00000100);
    write_le_uint32(header + 8, (uint32_t)total_utf16);
    buffer_append(gd3_buf, header, 12);

    // GD3 fields (UTF-16LE, null-terminated)
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Two passes: first with NULL to get length, then actual write
        size_t len = utf8_to_utf16le(new_fields[i], NULL);
        uint8_t *tmp = (uint8_t*)malloc(len);
        utf8_to_utf16le(new_fields[i], tmp);
        buffer_append(gd3_buf, tmp, len);
        free(tmp);
        free(new_fields[i]);
    }
}