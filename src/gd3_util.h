#ifndef GD3_UTIL_H
#define GD3_UTIL_H

#include <stdint.h>
#include "vgm_helpers.h"

#define GD3_FIELDS 11

// Extract GD3 fields from VGM data (UTF-8 decoded)
int extract_gd3_fields(const unsigned char *p_vgm_data, long filesize,
                       char *p_gd3_fields[GD3_FIELDS],
                       uint32_t *p_out_ver, uint32_t *p_out_len);

// Build a new GD3 chunk from fields, creator, notes
void build_new_gd3_chunk(dynbuffer_t *p_gd3_buf,
                         char *p_gd3_fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *p_append_creator,
                         const char *p_append_notes);

#endif // GD3_UTIL_H