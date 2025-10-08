#include "vgm_header.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

/**
 * Write a 32-bit value as little-endian into the given buffer.
 */
static void write_le32(uint8_t *buf, uint32_t value) {
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)((value >> 8) & 0xFF);
    buf[2] = (uint8_t)((value >> 16) & 0xFF);
    buf[3] = (uint8_t)((value >> 24) & 0xFF);
}

/**
 * Read a 32-bit little-endian value from the given buffer.
 */
static uint32_t read_le32(const uint8_t *buf) {
    return (uint32_t)buf[0]
         | ((uint32_t)buf[1] << 8)
         | ((uint32_t)buf[2] << 16)
         | ((uint32_t)buf[3] << 24);
}

/**
 * Calculate the new loop offset for the output VGM.
 * Adjusts for header size changes, inserted bytes, and Port1 data as needed.
 */
static uint32_t calculate_new_loop_offset(
    uint32_t orig_loop_offset,
    uint32_t orig_header_size,
    uint32_t new_header_size,
    uint32_t additional_data_bytes,
    bool is_adding_port1_bytes,
    uint32_t port1_bytes
) {
    // No loop
    if (orig_loop_offset == 0xFFFFFFFF) {
        return 0xFFFFFFFF;
    }
    uint32_t new_loop_offset = orig_loop_offset;
    // Adjust for header size difference
    if (new_header_size > orig_header_size) {
        new_loop_offset += (new_header_size - orig_header_size);
    } else if (new_header_size < orig_header_size) {
        new_loop_offset -= (orig_header_size - new_header_size);
    }
    // Add additional data bytes before loop
    if (additional_data_bytes > 0) {
        new_loop_offset += additional_data_bytes;
    }
    // Add Port1 bytes if flagged
    if (is_adding_port1_bytes && port1_bytes > 0) {
        new_loop_offset += port1_bytes;
    }
    return new_loop_offset;
}

/**
 * Build a VGM header for OPL3/OPL2 output, preserving as much of the original as possible.
 * The header and offsets are configured per the given parameters and VGM format specification.
 *
 * @param p_header                Output buffer for new VGM header.
 * @param p_orig_vgm_header       Original VGM header. Can be NULL.
 * @param total_samples           Total samples for VGM header (0x18).
 * @param eof_offset              EOF offset for VGM header (0x04).
 * @param gd3_offset              GD3 offset for VGM header (0x14).
 * @param data_offset             Data offset for VGM header (0x34).
 * @param version                 VGM version (0x08).
 * @param additional_data_bytes   Extra bytes inserted before data (e.g., instrument blocks).
 * @param is_adding_port1_bytes   Whether Port1 copy bytes should be included in loop offset.
 * @param port1_bytes             Number of bytes to add for Port1 copy, if applicable.
 */
void build_vgm_header(
    uint8_t *p_header,
    const uint8_t *p_orig_vgm_header,
    uint32_t total_samples,
    uint32_t eof_offset,
    uint32_t gd3_offset,
    uint32_t data_offset,
    uint32_t version,
    uint32_t additional_data_bytes,
    bool is_adding_port1_bytes,
    uint32_t port1_bytes
) {
    uint32_t orig_data_offset = 0;
    uint32_t orig_header_size = VGM_HEADER_SIZE;
    if (p_orig_vgm_header) {
        orig_data_offset = read_le32(p_orig_vgm_header + 0x34);
        orig_header_size = 0x34 + orig_data_offset;
        if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;
    }
    uint32_t new_header_size = (orig_header_size > VGM_HEADER_SIZE) ? orig_header_size : VGM_HEADER_SIZE;
    uint32_t actual_data_offset = new_header_size - 0x34;

    memset(p_header, 0, new_header_size);

    // Copy the original header if available
    if (p_orig_vgm_header) {
        memcpy(p_header, p_orig_vgm_header, orig_header_size);
    }

    // Set mandatory header fields
    p_header[0] = 'V'; p_header[1] = 'g'; p_header[2] = 'm'; p_header[3] = ' ';
    write_le32(p_header + 0x04, eof_offset); // EOF offset
    write_le32(p_header + 0x08, version);    // Version
    write_le32(p_header + 0x34, actual_data_offset); // Data offset

    // Adjust GD3 offset (0x14): relative to 0x14
    uint32_t actual_gd3_offset = gd3_offset;
    if (new_header_size > VGM_HEADER_SIZE) {
        actual_gd3_offset += (new_header_size - VGM_HEADER_SIZE);
    }
    write_le32(p_header + 0x14, actual_gd3_offset);

    write_le32(p_header + 0x18, total_samples); // Total samples

    // Read original loop offset, loop samples, and rate
    uint32_t loop_offset_orig = 0xFFFFFFFF;
    uint32_t loop_samples_orig = 0;
    uint32_t rate_orig = 0;
    if (p_orig_vgm_header) {
        loop_offset_orig  = read_le32(p_orig_vgm_header + 0x1C);
        loop_samples_orig = read_le32(p_orig_vgm_header + 0x20);
        rate_orig         = read_le32(p_orig_vgm_header + 0x24);
    }

    // Calculate new loop offset using the modular function
    uint32_t new_loop_offset = calculate_new_loop_offset(
        loop_offset_orig,
        orig_header_size,
        new_header_size,
        additional_data_bytes,
        is_adding_port1_bytes,
        port1_bytes
    );

    write_le32(p_header + 0x1C, new_loop_offset);    // Loop offset
    write_le32(p_header + 0x20, loop_samples_orig);  // Loop samples
    write_le32(p_header + 0x24, rate_orig);          // Rate
}

/**
 * Set the YM2413 clock value in the VGM header.
 */
void set_ym2413_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x10, value);
}

/**
 * Set the YM3812 clock value in the VGM header.
 */
void set_ym3812_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x50, value);
}

/**
 * Set the YM2151 clock value in the VGM header.
 */
void set_ym2151_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x30, value);
}

/**
 * Set the YM2612 clock value in the VGM header.
 */
void set_ym2612_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x2C, value);
}

/**
 * Set the YM2203 clock value in the VGM header.
 */
void set_ym2203_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x44, value);
}

/**
 * Set the YM2608 clock value in the VGM header.
 */
void set_ym2608_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x48, value);
}

/**
 * Set the YM2610 clock value in the VGM header.
 */
void set_ym2610_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x4C, value);
}

/**
 * Set the YM3526 clock value in the VGM header.
 */
void set_ym3526_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x54, value);
}

/**
 * Set the Y8950 clock value in the VGM header.
 */
void set_y8950_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x58, value);
}

/**
 * Set the YMF262 (OPL3) clock value in the VGM header.
 */
void set_ymf262_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x5C, value);
}

/**
 * Set the YMF278B clock value in the VGM header.
 */
void set_ymf278b_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x60, value);
}

/**
 * Set the YMF271 clock value in the VGM header.
 */
void set_ymf271_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x64, value);
}

/**
 * Set the YMZ280B clock value in the VGM header.
 */
void set_ymz280b_clock(uint8_t *p_header, uint32_t value) {
    write_le32(p_header + 0x68, value);
}

/**
 * Export the header and GD3 information from VGMContext into the output buffer.
 * Copies the header and GD3 block in order.
 */
void vgm_export_header_and_gd3(const VGMContext *ctx, VGMBuffer *out_buf) {
    // Write header (0x100 bytes)
    vgm_buffer_append(out_buf, ctx->header.raw, VGM_HEADER_SIZE);

    // Write GD3 tag if present and size > 0
    if (ctx->gd3.data && ctx->gd3.size > 0) {
        vgm_buffer_append(out_buf, ctx->gd3.data, ctx->gd3.size);
    }
}

/**
 * Returns the FM chip name string for the given FMChipType enum value.
 */
const char* fmchip_type_name(FMChipType type) {
    switch (type) {
        case FMCHIP_YM2413:   return "YM2413";
        case FMCHIP_YM2612:   return "YM2612";
        case FMCHIP_YM2151:   return "YM2151";
        case FMCHIP_YM2203:   return "YM2203";
        case FMCHIP_YM2608:   return "YM2608";
        case FMCHIP_YM2610:   return "YM2610";
        case FMCHIP_YM3812:   return "YM3812";
        case FMCHIP_YM3526:   return "YM3526";
        case FMCHIP_Y8950:    return "Y8950";
        case FMCHIP_YMF262:   return "YMF262";
        case FMCHIP_YMF278B:  return "YMF278B";
        case FMCHIP_YMF271:   return "YMF271";
        case FMCHIP_YMZ280B:  return "YMZ280B";
        case FMCHIP_2xYM2413:  return "2xYM2413";
        case FMCHIP_2xYM2612:  return "2xYM2612";
        case FMCHIP_2xYM2151:  return "2xYM2151";
        case FMCHIP_2xYM2203:  return "2xYM2203";
        case FMCHIP_2xYM2608:  return "2xYM2608";
        case FMCHIP_2xYM2610:  return "2xYM2610";
        case FMCHIP_2xYM3812:  return "2xYM3812";
        case FMCHIP_2xYM3526:  return "2xYM3526";
        case FMCHIP_2xY8950:   return "2xY8950";
        case FMCHIP_2xYMF262:  return "2xYMF262";
        case FMCHIP_2xYMF278B: return "2xYMF278B";
        case FMCHIP_2xYMF271:  return "2xYMF271";
        case FMCHIP_2xYMZ280B: return "2xYMZ280B";
        default: return "UNKNOWN";
    }
}

/**
 * Detect which FM chip is present in the VGM header and used in the input file.
 * Returns FMChipType value.
 */
FMChipType detect_fmchip_from_header(const unsigned char *p_vgm_data, long filesize) {
    if (filesize < 0x70) return FMCHIP_NONE;
    // Check clock values for known chips in VGM header (see vgm_header.h offsets)
    if (*(uint32_t*)(p_vgm_data + 0x40)) return FMCHIP_YM2413;
    if (*(uint32_t*)(p_vgm_data + 0x2C)) return FMCHIP_YM2612;
    if (*(uint32_t*)(p_vgm_data + 0x30)) return FMCHIP_YM2151;
    if (*(uint32_t*)(p_vgm_data + 0x44)) return FMCHIP_YM2203;
    if (*(uint32_t*)(p_vgm_data + 0x48)) return FMCHIP_YM2608;
    if (*(uint32_t*)(p_vgm_data + 0x4C)) return FMCHIP_YM2610;
    if (*(uint32_t*)(p_vgm_data + 0x50)) return FMCHIP_YM3812;
    if (*(uint32_t*)(p_vgm_data + 0x54)) return FMCHIP_YM3526;
    if (*(uint32_t*)(p_vgm_data + 0x58)) return FMCHIP_Y8950;
    if (*(uint32_t*)(p_vgm_data + 0x5C)) return FMCHIP_YMF262;
    if (*(uint32_t*)(p_vgm_data + 0x60)) return FMCHIP_YMF278B;
    if (*(uint32_t*)(p_vgm_data + 0x64)) return FMCHIP_YMF271;
    if (*(uint32_t*)(p_vgm_data + 0x68)) return FMCHIP_YMZ280B;
    return FMCHIP_NONE;
}

/**
 * Post-process the VGM header buffer to update clock fields for various chips.
 * This function sets clock values to zero for unused chips, and applies
 * overrides (such as OPL3 clock) if specified in cmd_opts.
 *
 * @param p_header_buf   Pointer to the VGM header buffer.
 * @param p_ctx          Pointer to the VGMContext.
 * @param p_stats        Pointer to chip write statistics.
 * @param p_cmd_opts     Pointer to command options (for clock overrides).
 */
void vgm_header_postprocess(
    uint8_t *p_header_buf,
    const VGMContext *p_ctx, 
    const CommandOptions *p_cmd_opts
) {

    // YMF262 (OPL3)
    uint32_t opl3_clock = (p_cmd_opts && p_cmd_opts->override_opl3_clock != 0)
        ? p_cmd_opts->override_opl3_clock : OPL3_CLOCK;
    set_ymf262_clock(p_header_buf, opl3_clock);

    fprintf(stderr, "[VGM HEADER] Total Write Count YM2413:%d YM3812:%d YM3526:%d Y8950:%d \n", 
            p_ctx->status.stats.ym2413_write_count,p_ctx->status.stats.ym3812_write_count,p_ctx->status.stats.ym3526_write_count,p_ctx->status.stats.y8950_write_count);

    if (p_cmd_opts->strip_unused_chip_clocks == false) {
        fprintf(stderr, "[VGM HEADER] strip_unused_chip_clocks == %d: Skip setting unused clocks to zero on OPL-series chips.\n", 
        p_cmd_opts->strip_unused_chip_clocks );
    } else {
        // YM2413
        if (p_ctx->status.stats.ym2413_write_count == 0) {
            fprintf(stderr, "[VGM HEADER] Set YM2413 clock to zero in VGM Header since this chip is not used.\n" );
            set_ym2413_clock(p_header_buf, 0);
        }
        // YM3812
        if (p_ctx->status.stats.ym3812_write_count == 0) {
            fprintf(stderr, "[VGM HEADER] Set YM3812 clock to zero in VGM Header since this chip is not used.\n" );
            set_ym3812_clock(p_header_buf, 0);
        }
        // YM3526
        if (p_ctx->status.stats.ym3526_write_count == 0) {
            fprintf(stderr, "[VGM HEADER] Set YM3526 clock to zero in VGM Header since this chip is not used.\n" );
            set_ym3526_clock(p_header_buf, 0);
        }
        // Y8950
        if (p_ctx->status.stats.y8950_write_count == 0) {
            fprintf(stderr, "[VGM HEADER] Set Y8950 clock to zero in VGM Header since this chip is not used.\n" );
            set_y8950_clock(p_header_buf, 0);
        }
    }

    // YM2413
    if (p_ctx->source_fmchip == FMCHIP_YM2413) {
        fprintf(stderr, "[VGM HEADER] Set YM2413 clock to zero in VGM Header, as this is the source chip\n" );
        set_ym2413_clock(p_header_buf, 0);
    }
    // YM3812
   if (p_ctx->source_fmchip == FMCHIP_YM3812) {
        fprintf(stderr, "[VGM HEADER] Set YM3812 clock to zero in VGM Header, as this is the source chip\n" );
        set_ym3812_clock(p_header_buf, 0);
    }
    // YM3526
    if (p_ctx->source_fmchip == FMCHIP_YM3526) {
        fprintf(stderr, "[VGM HEADER] Set YM3526 clock to zero in VGM Header, as this is the source chip\n" );
        set_ym3526_clock(p_header_buf, 0);
    }
    // Y8950
    if (p_ctx->source_fmchip == FMCHIP_Y8950) {
        fprintf(stderr, "[VGM HEADER] Set Y8950 clock to zero in VGM Header, as this is the source chip\n" );
        set_y8950_clock(p_header_buf, 0);
    }

    // PSG/DCSG is not a target
    #ifdef  UPDATE_PSG_CLOCK_INFO
    // AY8910
    if (stats->:wq
        ay8910_write_count == 0) {
        set_ay8910_clock(p_header_buf, 0);
    }
    // SN76489
    if (stats->sn76489_write_count == 0) {
        set_sn76489_clock(p_header_buf, 0);
    }
    #endif

    // If needed, add more chip clock handling here.
}
