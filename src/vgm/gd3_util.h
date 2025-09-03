#ifndef GD3_UTIL_H
#define GD3_UTIL_H

#include <stdint.h>
#include "vgm_helpers.h"

#define GD3_FIELDS 11

/**
 * Extracts GD3 fields from input VGM data, outputs UTF-8 strings for each field.
 * Each field is dynamically allocated and must be freed by the caller.
 */
int extract_gd3_fields(const unsigned char *p_vgm_data, long filesize,
                       char *p_gd3_fields[GD3_FIELDS],
                       uint32_t *p_out_ver, uint32_t *p_out_len);

/**
 * Build a new GD3 chunk from fields, creator, and notes.
 * The result is appended to p_gd3_buf.
 */
void build_new_gd3_chunk(VGMBuffer *p_gd3_buf,
                         char *p_gd3_fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *p_append_creator,
                         const char *p_append_notes);

#endif // GD3_UTIL_H