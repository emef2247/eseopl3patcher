#include "vgm_header.h"
#include <string.h>

// Build a standard VGM header for OPL3 output
void build_vgm_header(
    uint8_t *header,
    uint32_t total_samples,
    uint32_t eof_offset,
    uint32_t gd3_offset,
    uint32_t data_offset,
    uint32_t version,
    uint32_t opl3_clock,
    uint32_t loop_offset_orig,
    uint32_t loop_samples_orig,
    uint32_t rate_orig,
    uint32_t header_size,
    uint32_t new_header_size,
    uint32_t additional_data_bytes
) {
    memset(header, 0, VGM_HEADER_SIZE);
    header[0] = 'V'; header[1] = 'g'; header[2] = 'm'; header[3] = ' ';
    // 0x04: EOF offset (relative to 0x04)
    *(uint32_t*)(header + 0x04) = eof_offset;
    // 0x08: Version
    *(uint32_t*)(header + 0x08) = version;
    // 0x14: GD3 offset (relative to 0x14)
    *(uint32_t*)(header + 0x14) = gd3_offset;
    // 0x18: total samples
    *(uint32_t*)(header + 0x18) = total_samples;
    // 0x1C: loop offset (adjusted)
    uint32_t new_loop_offset = loop_offset_orig;
    if (new_header_size > header_size) {
        new_loop_offset += (new_header_size - header_size);
    }
    if (additional_data_bytes > 0) {
        new_loop_offset += additional_data_bytes;
    }
    *(uint32_t*)(header + 0x1C) = new_loop_offset;
    // 0x20: loop samples (from input)
    *(uint32_t*)(header + 0x20) = loop_samples_orig;
    // 0x24: rate (from input)
    *(uint32_t*)(header + 0x24) = rate_orig;
    // 0x34: data offset (relative to 0x34)
    *(uint32_t*)(header + 0x34) = data_offset;
    // 0x38: opl3 clock
    *(uint32_t*)(header + 0x38) = opl3_clock;
}