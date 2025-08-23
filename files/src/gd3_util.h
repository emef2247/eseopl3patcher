#ifndef GD3_UTIL_H
#define GD3_UTIL_H

#include <stdint.h>
#include "vgm_helpers.h"

#define GD3_FIELDS 11

// Extract GD3 fields from VGM data (UTF-8 decoded)
int extract_gd3_fields(const unsigned char *vgm_data, long filesize,
                       char *gd3_fields[GD3_FIELDS],
                       uint32_t *out_ver, uint32_t *out_len);

// Build a new GD3 chunk from fields, creator, notes
void build_new_gd3_chunk(dynbuffer_t *gd3_buf,
                         char *gd3_fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *append_creator,
                         const char *append_notes);

#endif // GD3_UTIL_H