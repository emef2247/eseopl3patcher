#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "vgm/vgm_helpers.h"
#include "vgm/vgm_header.h"
#include "opl3/opl3_convert.h"
#include "opl3/opl3_debug_util.h"
#include "vgm/gd3_util.h"

#define DEFAULT_DETUNE 1.0
#define DEFAULT_WAIT 0
#define DEFAULT_CH_PANNING 0
#define DEFAULT_VOLUME_RATIO0 1.0
#define DEFAULT_VOLUME_RATIO1 0.8

// Global verbose flag (used for debug print control)
int verbose = 0;

// Read little-endian 32-bit integer from buffer
static uint32_t read_le_uint32(const unsigned char *p_ptr) {
    return (uint32_t)p_ptr[0] | ((uint32_t)p_ptr[1] << 8) | ((uint32_t)p_ptr[2] << 16) | ((uint32_t)p_ptr[3] << 24);
}

// Check if file ends with ".vgm" or has no extension (for vgz-uncompressed)
static bool has_vgm_extension_or_none(const char *p_filename) {
    size_t len = strlen(p_filename);
    if (len > 4 && strcasecmp(p_filename + len - 4, ".vgm") == 0) return true;
    const char *p_basename = strrchr(p_filename, '/');
    p_basename = p_basename ? p_basename + 1 : p_filename;
    if (strchr(p_basename, '.') == NULL) return true;
    return false;
}

// Generate default output filename based on input name
static void make_default_output_name(const char *p_input, char *p_output, size_t outlen) {
    size_t len = strlen(p_input);
    if (len > 4 && strcmp(&p_input[len-4], ".vgm") == 0) len -= 4;
    snprintf(p_output, outlen, "%.*sOPL3.vgm", (int)len, p_input);
}

int main(int argc, char *argv[]) {
    // Usage: <input.vgm> <detune> [wait] [creator] [-o output.vgm] [-ch_panning n] [-vr0 f] [-vr1 f] [-verbose]
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.vgm> <detune> [wait] [creator] [-o output.vgm] [-ch_panning n] [-vr0 f] [-vr1 f] [-verbose]\n", argv[0]);
        return 1;
    }
    // Parse required arguments
    const char *p_input_vgm = argv[1];
    double detune = atof(argv[2]);

    // Wait is optional, default is DEFAULT_WAIT
    int opl3_keyon_wait = DEFAULT_WAIT;

    const char *p_creator = "eseopl3patcher";
    const char *p_output_path = NULL;

    // New argument defaults
    int ch_panning = DEFAULT_CH_PANNING;
    double v_ratio0 = DEFAULT_VOLUME_RATIO0;
    double v_ratio1 = DEFAULT_VOLUME_RATIO1;

    // Parse optional arguments
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            p_output_path = argv[i + 1];
            i++;
            continue;
        }
        if (strcmp(argv[i], "-ch_panning") == 0 && i + 1 < argc) {
            ch_panning = atoi(argv[i + 1]);
            i++;
            continue;
        }
        if (strcmp(argv[i], "-vr0") == 0 && i + 1 < argc) {
            v_ratio0 = atof(argv[i + 1]);
            i++;
            continue;
        }
        if (strcmp(argv[i], "-vr1") == 0 && i + 1 < argc) {
            v_ratio1 = atof(argv[i + 1]);
            i++;
            continue;
        }
        if (strcmp(argv[i], "-verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (argv[i][0] == '-') continue;
        char *endptr;
        int val = (int)strtol(argv[i], &endptr, 10);
        if (*endptr == '\0' && opl3_keyon_wait == DEFAULT_WAIT) {
            opl3_keyon_wait = val;
            continue;
        }
        if (p_creator == NULL || strcmp(p_creator, "eseopl3patcher") == 0) {
            p_creator = argv[i];
            continue;
        }
    }

    if (!p_output_path) {
        static char default_out[256];
        make_default_output_name(p_input_vgm, default_out, sizeof(default_out));
        p_output_path = default_out;
    }

    if (!has_vgm_extension_or_none(p_input_vgm)) {
        fprintf(stderr, "Input file must have .vgm extension or no extension (for vgz-uncompressed files)\n");
        return 1;
    }

    // Open and read input VGM file
    FILE *p_fp = fopen(p_input_vgm, "rb");
    if (!p_fp) {
        fprintf(stderr, "Cannot open input file: %s\n", p_input_vgm);
        return 1;
    }
    fseek(p_fp, 0, SEEK_END);
    long filesize = ftell(p_fp);
    fseek(p_fp, 0, SEEK_SET);

    unsigned char *p_vgm_data = (unsigned char*)malloc(filesize);
    if (fread(p_vgm_data, 1, filesize, p_fp) != (size_t)filesize) {
        fprintf(stderr, "Failed to read entire file!\n");
        free(p_vgm_data);
        fclose(p_fp);
        return 1;
    }
    fclose(p_fp);

    // Verify VGM file signature
    if (memcmp(p_vgm_data, "Vgm ", 4) != 0) {
        fprintf(stderr, "Not a valid VGM file.\n");
        free(p_vgm_data);
        return 1;
    }

    // Parse VGM header fields
    uint32_t vgm_data_offset = (filesize >= 0x34) ? read_le_uint32(p_vgm_data + 0x34) : 0;
    uint32_t orig_header_size = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;
    long data_start = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (data_start >= filesize) {
        fprintf(stderr, "Invalid VGM data offset.\n");
        free(p_vgm_data);
        return 1;
    }

    // Get original loop offset and calculate loop address
    uint32_t orig_loop_offset = read_le_uint32(p_vgm_data + 0x1C);
    uint32_t orig_loop_address = 0;
    if (orig_loop_offset != 0xFFFFFFFF) {
        orig_loop_address = orig_loop_offset + 0x04;
    }

    // --- Setup VGMContext for conversion ---
    VGMContext vgmctx;
    vgm_buffer_init(&vgmctx.buffer);
    vgm_timestamp_init(&vgmctx.timestamp, 44100.0);
    vgmctx.status.total_samples = 0;
    memset(&vgmctx.header, 0, sizeof(vgmctx.header));
    vgmctx.gd3.data = NULL;
    vgmctx.gd3.size = 0;

    OPL3State state = {0};
    state.rhythm_mode = false;
    state.opl3_mode_initialized = false;

    bool is_replicate_reg_ymf262 = true;
    long read_done_byte = data_start;
    uint32_t additional_bytes = 0;
    long loop_start_in_buffer = -1;

    // --- Main VGM data conversion loop ---
    while (read_done_byte < filesize) {
        // Detect loop start: when reaching the original loop address, record buffer.size
        if (orig_loop_offset != 0xFFFFFFFF && read_done_byte == orig_loop_address) {
            loop_start_in_buffer = vgmctx.buffer.size;
        }

        unsigned char cmd = p_vgm_data[read_done_byte];

        // YM2413 register write (0x51)
        if (cmd == 0x51) {
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM2413);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }
        // YM3812 register write (0x5A)
        else if (cmd == 0x5A) {
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM3812);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }
        // YM3526 register write (0x5B)
        else if (cmd == 0x5B) {
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM3526);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }
        // Y8950 register write (0x5C)
        else if (cmd == 0x5C) {
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_Y8950);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }
        // Short wait command (0x70-0x7F)
        else if (cmd >= 0x70 && cmd <= 0x7F) {
            vgm_wait_short(&vgmctx.buffer, &vgmctx.status, cmd);
            read_done_byte++;
            continue;
        }
        // Wait n samples (0x61)
        else if (cmd == 0x61) {
            if (read_done_byte + 2 >= filesize) {
                fprintf(stderr, "Truncated 0x61 at end of file\n");
                break;
            }
            uint8_t lo = p_vgm_data[read_done_byte + 1];
            uint8_t hi = p_vgm_data[read_done_byte + 2];
            uint16_t samples = lo | (hi << 8);
            vgm_wait_samples(&vgmctx.buffer, &vgmctx.status, samples);
            read_done_byte += 3;
            continue;
        }
        // Wait 60Hz (0x62)
        else if (cmd == 0x62) {
            vgm_wait_60hz(&vgmctx.buffer, &vgmctx.status);
            read_done_byte++;
            continue;
        }
        // Wait 50Hz (0x63)
        else if (cmd == 0x63) {
            vgm_wait_50hz(&vgmctx.buffer, &vgmctx.status);
            read_done_byte++;
            continue;
        }
        // End of sound data (0x66)
        else if (cmd == 0x66) {
            vgm_append_byte(&vgmctx.buffer, cmd);
            read_done_byte++;
            break;
        }
        // Other commands: copy as-is
        else {
            vgm_append_byte(&vgmctx.buffer, cmd);
            read_done_byte++;
            continue;
        }
    }

    // GD3 tag handling
    char *p_gd3_fields[GD3_FIELDS] = {NULL};
    uint32_t orig_gd3_ver = 0, orig_gd3_len = 0;
    if (extract_gd3_fields(p_vgm_data, filesize, p_gd3_fields, &orig_gd3_ver, &orig_gd3_len) != 0) {
        fprintf(stderr, "Original GD3 not found, will use empty fields.\n");
        for (int i = 0; i < GD3_FIELDS; ++i) p_gd3_fields[i] = strdup("");
        orig_gd3_ver = 0x00000100;
    }
    char creator_append[128];
    snprintf(creator_append, sizeof(creator_append), ",%s", p_creator);
    char note_append[512];
    snprintf(note_append, sizeof(note_append),
        ", Converted from YM3812 to OPL3. Port 0 (ch0-8): original, Port 1 (ch9-17): detuned for chorus. Detune:%.2f%% KEY ON/OFF wait:%d Ch Panning mode:%d port0 volume:%.2f%% port1 volume:%.2f%%",
        detune, opl3_keyon_wait, ch_panning, v_ratio0 * 100, v_ratio1 * 100);

    VGMBuffer gd3;
    vgm_buffer_init(&gd3);
    build_new_gd3_chunk(&gd3, p_gd3_fields, orig_gd3_ver, creator_append, note_append);
    for (int i = 0; i < GD3_FIELDS; ++i) free(p_gd3_fields[i]);

    uint32_t music_data_size = (uint32_t)vgmctx.buffer.size;
    uint32_t gd3_size = (uint32_t)gd3.size;
    uint32_t header_size = (orig_header_size > VGM_HEADER_SIZE) ? orig_header_size : VGM_HEADER_SIZE;
    uint32_t new_eof_offset = music_data_size + header_size + gd3_size - 1;
    uint32_t vgm_eof_offset_field = new_eof_offset - 0x04;
    uint32_t gd3_offset_field_value = header_size + music_data_size - 0x14;
    uint32_t data_offset = header_size - 0x34;
    uint32_t new_loop_offset = 0xFFFFFFFF;
    if (loop_start_in_buffer >= 0) {
        new_loop_offset = header_size + (uint32_t)loop_start_in_buffer;
    }

    uint8_t *p_header_buf = (uint8_t*)calloc(1, header_size);

    build_vgm_header(
        p_header_buf,
        p_vgm_data,
        vgmctx.status.total_samples,
        vgm_eof_offset_field,
        gd3_offset_field_value,
        data_offset,
        0x00000171,
        additional_bytes
    );

    // Set loop offset in header if applicable
    if (new_loop_offset != 0xFFFFFFFF) {
        p_header_buf[0x1C] = (uint8_t)(new_loop_offset & 0xFF);
        p_header_buf[0x1D] = (uint8_t)((new_loop_offset >> 8) & 0xFF);
        p_header_buf[0x1E] = (uint8_t)((new_loop_offset >> 16) & 0xFF);
        p_header_buf[0x1F] = (uint8_t)((new_loop_offset >> 24) & 0xFF);
    }

    // Set OPL3 clock and clear YM3812 clock in header
    set_ymf262_clock(p_header_buf, OPL3_CLOCK);
    set_ym3812_clock(p_header_buf, 0);

    // Write output file
    FILE *p_wf = fopen(p_output_path, "wb");
    if (!p_wf) {
        fprintf(stderr, "Failed to open output file for writing: %s\n", p_output_path);
        vgm_buffer_free(&vgmctx.buffer);
        vgm_buffer_free(&gd3);
        free(p_vgm_data);
        free(p_header_buf);
        return 1;
    }
    fwrite(p_header_buf, 1, header_size, p_wf);
    fwrite(vgmctx.buffer.data, 1, vgmctx.buffer.size, p_wf);
    fwrite(gd3.data, 1, gd3.size, p_wf);
    fclose(p_wf);

    printf("[OPL3] Converted VGM written to: %s\n", p_output_path);
    printf("[OPL3] <detune f> Detune value: %g%%\n", detune);
    printf("[OPL3] Wait value: %d\n", opl3_keyon_wait);
    printf("[OPL3] Creator: %s\n", p_creator);
    printf("[OPL3] Channel Panning Mode: %d\n", ch_panning);
    printf("[OPL3] Port0 Volume: %.2f%%\n", v_ratio0 * 100);
    printf("[OPL3] Port1 Volume: %.2f%%\n", v_ratio1 * 100);

    if (verbose) {
        printf("[OPL3] Total number of detected voices: %d\n", state.voice_db.count);

        //for (int i = 0; i < state.voice_db.count; ++i) {
        //    print_opl3_voice_param(&state.voice_db.p_voices[i]);
        //}
    }
    printf("\n");
    vgm_buffer_free(&vgmctx.buffer);
    vgm_buffer_free(&gd3);
    free(p_vgm_data);
    free(p_header_buf);

    return 0;
}