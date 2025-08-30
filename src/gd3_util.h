#ifndef GD3_UTIL_H
#define GD3_UTIL_H

#include <stdint.h>
#include "vgm_helpers.h"

#define GD3_FIELDS 11

// Extract GD3 fields from VGM data (UTF-8 decoded)
int extract_gd3_fields(const unsigned char *pVgmData, long filesize,
                       char *pGd3Fields[GD3_FIELDS],
                       uint32_t *pOutVer, uint32_t *pOutLen);

// Build a new GD3 chunk from fields, creator, notes
void build_new_gd3_chunk(dynbuffer_t *pGd3Buf,
                         char *pGd3Fields[GD3_FIELDS],
                         uint32_t orig_ver,
                         const char *pAppendCreator,
                         const char *pAppendNotes);

#endif // GD3_UTIL_H