#include "gd3_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Helper: read little-endian 32-bit integer from byte array
static uint32_t read_le_uint32(const unsigned char *p_ptr) {
    return (uint32_t)p_ptr[0] | ((uint32_t)p_ptr[1] << 8) | ((uint32_t)p_ptr[2] << 16) | ((uint32_t)p_ptr[3] << 24);
}

// Helper: write little-endian 32-bit integer into byte array
static void write_le_uint32(unsigned char *p_ptr, uint32_t val) {
    p_ptr[0] = (val & 0xFF);
    p_ptr[1] = ((val >> 8) & 0xFF);
    p_ptr[2] = ((val >> 16) & 0xFF);
    p_ptr[3] = ((val >> 24) & 0xFF);
}

// Helper: decode UTF-16LE string to newly allocated UTF-8 string
static char *utf16le_to_utf8(const uint8_t *p_utf16, size_t bytes) {
    // Worst case: each UTF-16 code unit becomes 3 UTF-8 bytes + null
    char *p_utf8 = (char*)malloc(bytes * 2 + 1);
    size_t out = 0;
    for (size_t in = 0; in + 1 < bytes; in += 2) {
        uint16_t w = p_utf16[in] | (p_utf16[in+1] << 8);
        if (w == 0) break;
        if (w < 0x80) {
            p_utf8[out++] = w;
        } else if (w < 0x800) {
            p_utf8[out++] = 0xC0 | (w >> 6);
            p_utf8[out++] = 0x80 | (w & 0x3F);
        } else {
            p_utf8[out++] = 0xE0 | (w >> 12);
            p_utf8[out++] = 0x80 | ((w >> 6) & 0x3F);
            p_utf8[out++] = 0x80 | (w & 0x3F);
        }
    }
    p_utf8[out] = 0;
    return p_utf8;
}

// Helper: encode UTF-8 string as UTF-16LE to buffer, return bytes written
static size_t utf8_to_utf16le(const char *p_utf8, uint8_t *p_out) {
    size_t bytes = 0;
    while (*p_utf8) {
        unsigned char c = (unsigned char)*p_utf8++;
        uint16_t w = 0;
        if (c < 0x80) {
            w = c;
        } else if ((c & 0xE0) == 0xC0) {
            w = (c & 0x1F) << 6;
            c = (unsigned char)*p_utf8++;
            w |= (c & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            w = (c & 0x0F) << 12;
            c = (unsigned char)*p_utf8++;
            w |= (c & 0x3F) << 6;
            c = (unsigned char)*p_utf8++;
            w |= (c & 0x3F);
        }
        if (p_out) {
            p_out[bytes++] = (w & 0xFF);
            p_out[bytes++] = (w >> 8);
        } else {
            bytes += 2;
        }
    }
    // Null-terminator
    if (p_out) {
        p_out[bytes++] = 0; p_out[bytes++] = 0;
    } else {
        bytes += 2;
    }
    return bytes;
}

// Extracts GD3 fields from input VGM data, outputs UTF-8 strings for each field
int extract_gd3_fields(const unsigned char *p_vgm_data, long filesize,
                       char *p_gd3_fields[GD3_FIELDS],
                       uint32_t *p_out_ver, uint32_t *p_out_len) {
    // Find GD3 offset in VGM header (0x14)
    if (filesize < 0x18)
        return 1;
    uint32_t gd3_offset = read_le_uint32(p_vgm_data + 0x14);
    if (gd3_offset == 0)
        return 1;
    long gd3_absolute = 0x14 + gd3_offset;
    if (gd3_absolute + 12 > filesize)
        return 1;
    if (memcmp(p_vgm_data + gd3_absolute, "Gd3 ", 4) != 0)
        return 1;

    *p_out_ver = read_le_uint32(p_vgm_data + gd3_absolute + 4);
    *p_out_len = read_le_uint32(p_vgm_data + gd3_absolute + 8);

    const uint8_t *p_gd3_ptr = p_vgm_data + gd3_absolute + 12;
    const uint8_t *p_gd3_end = p_gd3_ptr + *p_out_len;
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Find UTF-16LE null-terminated string
        const uint8_t *p_str_start = p_gd3_ptr;
        while (p_gd3_ptr + 1 < p_gd3_end && (p_gd3_ptr[0] != 0 || p_gd3_ptr[1] != 0))
            p_gd3_ptr += 2;
        // Decode to UTF-8
        p_gd3_fields[i] = utf16le_to_utf8(p_str_start, p_gd3_ptr - p_str_start);
        p_gd3_ptr += 2; // skip null terminator
    }
    return 0;
}

// Build a new GD3 chunk from fields, creator, notes.
// Field indices:
//  0: TrackNameE
//  1: TrackNameJ
//  2: GameNameE
//  3: GameNameJ
//  4: SystemE
//  5: SystemJ
//  6: AuthorE
//  7: AuthorJ
//  8: ReleaseDate
//  9: Creator (ConvertedBy)
// 10: Notes
void build_new_gd3_chunk(dynbuffer_t *p_gd3_buf,
                         char *p_gd3_fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *p_append_creator,
                         const char *p_append_notes) {
    // Compose new fields, append creator and notes
    char *p_new_fields[GD3_FIELDS];
    for (int i = 0; i < GD3_FIELDS; ++i) {
        if (i == 9 && p_append_creator) {
            // Append creator to Creator field
            size_t len1 = strlen(p_gd3_fields[i]);
            size_t len2 = strlen(p_append_creator);
            p_new_fields[i] = (char*)malloc(len1 + len2 + 2);
            strcpy(p_new_fields[i], p_gd3_fields[i]);
            strcat(p_new_fields[i], p_append_creator);
        } else if (i == 10 && p_append_notes) {
            // Append notes to Notes field
            size_t len1 = strlen(p_gd3_fields[i]);
            size_t len2 = strlen(p_append_notes);
            p_new_fields[i] = (char*)malloc(len1 + len2 + 2);
            strcpy(p_new_fields[i], p_gd3_fields[i]);
            strcat(p_new_fields[i], p_append_notes);
        } else {
            p_new_fields[i] = strdup(p_gd3_fields[i]);
        }
    }

    // Calculate UTF-16LE field length
    size_t total_utf16 = 0;
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Each field is null-terminated in UTF-16LE
        total_utf16 += utf8_to_utf16le(p_new_fields[i], NULL);
    }

    // GD3 chunk header
    uint8_t header[12] = { 'G', 'd', '3', ' ',
        0,0,0,0, // version
        0,0,0,0  // length
    };
    write_le_uint32(header + 4, orig_ver ? orig_ver : 0x00000100);
    write_le_uint32(header + 8, (uint32_t)total_utf16);
    buffer_append(p_gd3_buf, header, 12);

    // GD3 fields (UTF-16LE, null-terminated)
    for (int i = 0; i < GD3_FIELDS; ++i) {
        // Two passes: first with NULL to get length, then actual write
        size_t len = utf8_to_utf16le(p_new_fields[i], NULL);
        uint8_t *p_tmp = (uint8_t*)malloc(len);
        utf8_to_utf16le(p_new_fields[i], p_tmp);
        buffer_append(p_gd3_buf, p_tmp, len);
        free(p_tmp);
        free(p_new_fields[i]);
    }
}