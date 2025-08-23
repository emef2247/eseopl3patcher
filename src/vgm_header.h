#pragma once
#include <stdint.h>

#define VGM_HEADER_SIZE 0x100
#define OPL3_CLOCK 3579545
#define VGM_VERSION 0x00000171

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
);