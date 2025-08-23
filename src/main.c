#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "vgm_helpers.h"
#include "vgm_header.h"
#include "opl3_convert.h"
#include "gd3_util.h"

#define DEFAULT_DETUNE 1.0
#define DEFAULT_WAIT 1

// Check if file ends with ".vgm" or has no extension (for vgz-uncompressed)
static bool has_vgm_extension_or_none(const char *filename) {
    size_t len = strlen(filename);
    if (len > 4 && strcasecmp(filename + len - 4, ".vgm") == 0) return true;
    const char *basename = strrchr(filename, '/');
    basename = basename ? basename + 1 : filename;
    if (strchr(basename, '.') == NULL) return true;
    return false;
}

// Generate default output filename
static void make_default_output_name(const char *input, char *output, size_t outlen) {
    size_t len = strlen(input);
    if (len > 4 && strcmp(&input[len-4], ".vgm") == 0) len -= 4;
    snprintf(output, outlen, "%.*sOPL3.vgm", (int)len, input);
}

// Parse command line for -o output option
static const char* get_output_path(int argc, char *argv[], const char *input_path) {
    for (int i = 5; i < argc-1; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            return argv[i+1];
        }
    }
    static char default_out[256];
    make_default_output_name(input_path, default_out, sizeof(default_out));
    return default_out;
}

// Read little-endian 32-bit integer from buffer
static uint32_t read_le_uint32(const unsigned char *ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <input.vgm> <detune> <wait> <creator> [-o output.vgm]\n", argv[0]);
        return 1;
    }

    const char *input_vgm = argv[1];
    double detune = atof(argv[2]);
    int opl3_keyon_wait = atoi(argv[3]);
    const char *creator = argv[4];
    const char *output_path = get_output_path(argc, argv, input_vgm);

    if (!has_vgm_extension_or_none(input_vgm)) {
        fprintf(stderr, "Input file must have .vgm extension or no extension (for vgz-uncompressed files)\n");
        return 1;
    }

    FILE *fp = fopen(input_vgm, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open input file: %s\n", input_vgm);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *vgm_data = (unsigned char*)malloc(filesize);
    if (fread(vgm_data, 1, filesize, fp) != (size_t)filesize) {
        fprintf(stderr, "Failed to read entire file!\n");
        free(vgm_data);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    // Parse VGM header fields
    if (memcmp(vgm_data, "Vgm ", 4) != 0) {
        fprintf(stderr, "Not a valid VGM file.\n");
        free(vgm_data);
        return 1;
    }

    uint32_t orig_header_size = (filesize >= 0x08) ? read_le_uint32(vgm_data + 0x08) : 0x100;
    if (orig_header_size < 0x40) orig_header_size = 0x100;
    uint32_t vgm_data_offset = (filesize >= 0x34) ? read_le_uint32(vgm_data + 0x34) : 0;
    long data_start = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (data_start >= filesize) {
        fprintf(stderr, "Invalid VGM data offset.\n");
        free(vgm_data);
        return 1;
    }

    // Read original loop offset, loop samples, and rate from input header
    uint32_t orig_loop_offset = (filesize >= 0x1C + 4) ? read_le_uint32(vgm_data + 0x1C) : 0;
    uint32_t orig_loop_samples = (filesize >= 0x20 + 4) ? read_le_uint32(vgm_data + 0x20) : 0;
    uint32_t orig_rate = (filesize >= 0x24 + 4) ? read_le_uint32(vgm_data + 0x24) : 0;

    // Prepare output music data buffer and status
    dynbuffer_t music_data;
    buffer_init(&music_data);
    vgm_status_t vstat = {0};
    OPL3State state = {0};
    state.rhythm_mode = false;
    state.opl3_mode_initialized = false;

    bool is_replicate_reg_ymf262 = true;
    long read_done_byte = data_start;

    uint32_t additional_bytes = 0; // number of bytes added for Port 1 output

    // Main VGM data conversion loop
    while (read_done_byte < filesize) {
        unsigned char cmd = vgm_data[read_done_byte];

        if (cmd == 0x5A) {
            // YM3812 register write
            uint8_t reg = vgm_data[read_done_byte + 1];
            uint8_t val = vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&music_data);
                    state.opl3_mode_initialized = true;
                }
                // duplicate_write_opl3 returns additional bytes written for Port 1
                additional_bytes += duplicate_write_opl3(&music_data, &vstat, &state, reg, val, detune, opl3_keyon_wait);
            } else {
                forward_write(&music_data, 0, reg, val);
            }
            continue;
        }
        else if (cmd >= 0x70 && cmd <= 0x7F) {
            vgm_wait_short(&music_data, &vstat, cmd);
            read_done_byte++;
            continue;
        }
        else if (cmd == 0x61) {
            if (read_done_byte + 2 >= filesize) {
                fprintf(stderr, "Truncated 0x61 at end of file\n");
                break;
            }
            uint8_t lo = vgm_data[read_done_byte + 1];
            uint8_t hi = vgm_data[read_done_byte + 2];
            uint16_t samples = lo | (hi << 8);
            vgm_wait_samples(&music_data, &vstat, samples);
            read_done_byte += 3;
            continue;
        }
        else if (cmd == 0x62) {
            vgm_wait_60hz(&music_data, &vstat);
            read_done_byte++;
            continue;
        }
        else if (cmd == 0x63) {
            vgm_wait_50hz(&music_data, &vstat);
            read_done_byte++;
            continue;
        }
        else if (cmd == 0x66) {
            vgm_append_byte(&music_data, cmd);
            read_done_byte++;
            break;
        }
        else {
            vgm_append_byte(&music_data, cmd);
            read_done_byte++;
            continue;
        }
    }

    // GD3 tag handling: read, modify, rebuild
    char *gd3_fields[GD3_FIELDS] = {NULL};
    uint32_t orig_gd3_ver = 0, orig_gd3_len = 0;
    if (extract_gd3_fields(vgm_data, filesize, gd3_fields, &orig_gd3_ver, &orig_gd3_len) != 0) {
        fprintf(stderr, "Original GD3 not found, will use empty fields.\n");
        for (int i = 0; i < GD3_FIELDS; ++i) gd3_fields[i] = strdup("");
        orig_gd3_ver = 0x00000100;
    }
    char creator_append[128];
    snprintf(creator_append, sizeof(creator_append), ",modified by %s", creator);

    char note_append[512];
    snprintf(note_append, sizeof(note_append),
        " -- This data is converted from YM3812 to YMF262 (OPL3) format. The YM3812 commands are directly mapped to Port 0 (ch0-ch8). At the same time, the same commands are mapped to Port 1 (ch9-ch17), with their F-NUMBER values detuned by %.2f%%. This configuration utilizes all OPL3 channels to achieve a chorus effect. -- Channels 9-17 (Port1) F-NUMBERs are detuned by %.2f%%. KEY ON/OFF wait %d --",
        detune, detune, opl3_keyon_wait);

    dynbuffer_t gd3;
    buffer_init(&gd3);
    build_new_gd3_chunk(&gd3, gd3_fields, orig_gd3_ver, creator_append, note_append);

    for (int i = 0; i < GD3_FIELDS; ++i) free(gd3_fields[i]);

    // Build new VGM header
    uint32_t header_size = VGM_HEADER_SIZE;
    uint32_t music_data_size = (uint32_t)music_data.size;
    uint32_t gd3_size = (uint32_t)gd3.size;

    // Calculate new EOF offset (last byte index, 0-origin)
    uint32_t new_eof_offset = music_data_size + header_size + gd3_size - 1;
    uint32_t vgm_eof_offset_field = new_eof_offset - 0x04;

    // GD3 offset is relative to 0x14
    uint32_t gd3_file_pos = header_size + music_data_size;
    uint32_t gd3_offset_field_value = gd3_file_pos - 0x14;

    // Data offset is relative to 0x34
    uint32_t data_offset = header_size - 0x34;

    uint8_t header_buf[VGM_HEADER_SIZE];
    build_vgm_header(
        header_buf,
        vstat.new_total_samples,            // 0x18: total samples (newly measured)
        vgm_eof_offset_field,               // 0x04: EOF offset
        gd3_offset_field_value,             // 0x14: GD3 offset
        data_offset,                        // 0x34: Data offset
        0x00000171,                         // 0x08: Version (always 0x00000171)
        OPL3_CLOCK,                         // 0x38: YMF262 clock
        orig_loop_offset,                   // 0x1C: original loop offset
        orig_loop_samples,                  // 0x20: original loop samples
        orig_rate,                          // 0x24: original rate
        orig_header_size,                   // original header size
        VGM_HEADER_SIZE,                    // new header size
        additional_bytes                    // additional bytes for port 1
    );

    FILE *wf = fopen(output_path, "wb");
    if (!wf) {
        fprintf(stderr, "Failed to open output file for writing: %s\n", output_path);
        buffer_free(&music_data);
        buffer_free(&gd3);
        free(vgm_data);
        return 1;
    }
    fwrite(header_buf, 1, VGM_HEADER_SIZE, wf);
    fwrite(music_data.data, 1, music_data.size, wf);
    fwrite(gd3.data, 1, gd3.size, wf);
    fclose(wf);

    printf("Converted VGM written to: %s\n", output_path);

    buffer_free(&music_data);
    buffer_free(&gd3);
    free(vgm_data);
    return 0;
}