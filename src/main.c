#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "vgm_helpers.h"
#include "vgm_header.h"
#include "opl3_convert.h"
#include "gd3_util.h"

#define DEFAULT_DETUNE 1.0
#define DEFAULT_WAIT 0
#define DEFAULT_CH_PANNING 0 
#define DEFAULT_VOLUME_RATIO0 1.0
#define DEFAULT_VOLUME_RATIO1 0.6

// Read little-endian 32-bit integer from buffer
static uint32_t read_le_uint32(const unsigned char *ptr) {
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

// Check if file ends with ".vgm" or has no extension (for vgz-uncompressed)
static bool has_vgm_extension_or_none(const char *pFilename) {
    size_t len = strlen(pFilename);
    if (len > 4 && strcasecmp(pFilename + len - 4, ".vgm") == 0) return true;
    const char *basename = strrchr(pFilename, '/');
    basename = basename ? basename + 1 : pFilename;
    if (strchr(basename, '.') == NULL) return true;
    return false;
}

// Generate default output filename based on input name
static void make_default_output_name(const char *pInput, char *pOutput, size_t outlen) {
    size_t len = strlen(pInput);
    if (len > 4 && strcmp(&pInput[len-4], ".vgm") == 0) len -= 4;
    snprintf(pOutput, outlen, "%.*sOPL3.vgm", (int)len, pInput);
}

int main(int argc, char *argv[]) {
    // Usage: <input.vgm> <detune> [wait] [creator] [-o output.vgm] [-ch_panning n] [-vr0 f] [-vr1 f]
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.vgm> <detune> [wait] [creator] [-o output.vgm] [-ch_panning n] [-vr0 f] [-vr1 f]\n", argv[0]);
        return 1;
    }
    // Parse required arguments
    const char *pInputVgm = argv[1];
    double detune = atof(argv[2]);

    // Wait is optional, default is DEFAULT_WAIT
    int opl3_keyon_wait = DEFAULT_WAIT;

    const char *pCreator = "eseopl3patcher";
    const char *pOutputPath = NULL;

    // --- New argument defaults ---
    int ch_panning = DEFAULT_CH_PANNING;   // Default: Channel panning mode: ON
    double v_ratio0 = DEFAULT_VOLUME_RATIO0; // Default: 100% volume
    double v_ratio1 = DEFAULT_VOLUME_RATIO1; // Default: 60% volume

    // Parse optional arguments
    for (int i = 3; i < argc; ++i) {
        // Handle -o option
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            pOutputPath = argv[i + 1];
            i++; // Skip output filename
            continue;
        }
        // Handle -ch_panning option
        if (strcmp(argv[i], "-ch_panning") == 0 && i + 1 < argc) {
            ch_panning = atoi(argv[i + 1]);
            i++;
            continue;
        }
        // Handle -vr0 option
        if (strcmp(argv[i], "-vr0") == 0 && i + 1 < argc) {
            v_ratio0 = atof(argv[i + 1]);
            i++;
            continue;
        }
        // Handle -vr1 option
        if (strcmp(argv[i], "-vr1") == 0 && i + 1 < argc) {
            v_ratio1 = atof(argv[i + 1]);
            i++;
            continue;
        }
        // If this argument starts with '-', skip (unknown option)
        if (argv[i][0] == '-') continue;
        // If this argument is a number and wait is not yet set, use as wait
        char *pEndptr;
        int val = (int)strtol(argv[i], &pEndptr, 10);
        if (*pEndptr == '\0' && opl3_keyon_wait == DEFAULT_WAIT) {
            opl3_keyon_wait = val;
            continue;
        }
        // If creator not set (other than default), use this argument
        if (pCreator == NULL || strcmp(pCreator, "eseopl3patcher") == 0) {
            pCreator = argv[i];
            continue;
        }
        // Otherwise, ignore
    }

    // If pOutputPath not set, generate default
    if (!pOutputPath) {
        static char default_out[256];
        make_default_output_name(pInputVgm, default_out, sizeof(default_out));
        pOutputPath = default_out;
    }

    // Check file extension
    if (!has_vgm_extension_or_none(pInputVgm)) {
        fprintf(stderr, "Input file must have .vgm extension or no extension (for vgz-uncompressed files)\n");
        return 1;
    }

    // Open and read input VGM file
    FILE *fp = fopen(pInputVgm, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open input file: %s\n", pInputVgm);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char *pVgmData = (unsigned char*)malloc(filesize);
    if (fread(pVgmData, 1, filesize, fp) != (size_t)filesize) {
        fprintf(stderr, "Failed to read entire file!\n");
        free(pVgmData);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    // Verify VGM file signature
    if (memcmp(pVgmData, "Vgm ", 4) != 0) {
        fprintf(stderr, "Not a valid VGM file.\n");
        free(pVgmData);
        return 1;
    }

    // Parse VGM header fields
    uint32_t vgm_data_offset = (filesize >= 0x34) ? read_le_uint32(pVgmData + 0x34) : 0;
    uint32_t orig_header_size = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (orig_header_size < 0x40) orig_header_size = 0x100; // fallbac   k for broken files
    long data_start = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (data_start >= filesize) {
        fprintf(stderr, "Invalid VGM data offset.\n");
        free(pVgmData);
        return 1;
    }

    // Get original loop offset and calculate loop address
    uint32_t orig_loop_offset = read_le_uint32(pVgmData  + 0x1C);
    uint32_t orig_loop_address = 0;
    if (orig_loop_offset != 0xFFFFFFFF) {
        orig_loop_address = orig_loop_offset + 0x04;
    }

    // Prepare output music data buffer and status
    dynbuffer_t music_data;
    buffer_init(&music_data);
    vgm_status_t vstat = {0};
    OPL3State state = {0};
    state.rhythm_mode = false;
    state.opl3_mode_initialized = false;

    bool is_replicate_reg_ymf262 = true;
    long read_done_byte = data_start;
    uint32_t additional_bytes = 0;

    // Track loop start position in generated music_data
    long loop_start_in_music_data = -1;

    // Main VGM data conversion loop
    while (read_done_byte < filesize) {
        // Detect loop start: when reaching the original loop address, record music_data.size
        if (orig_loop_offset != 0xFFFFFFFF && read_done_byte == orig_loop_address) {
            loop_start_in_music_data = music_data.size;
        }

        unsigned char cmd = pVgmData[read_done_byte];

        // YM3812 register write (0x5A)
        if (cmd == 0x5A) {
            uint8_t reg = pVgmData[read_done_byte + 1];
            uint8_t val = pVgmData[read_done_byte + 2];
            read_done_byte += 3;

            if (is_replicate_reg_ymf262) {
                if (!state.opl3_mode_initialized) {
                    // Initialize OPL3 registers (music_data will grow here)
                    opl3_init(&music_data, ch_panning);
                    state.opl3_mode_initialized = true;
                }
                // duplicate_write_opl3 returns additional bytes written for Port 1
                additional_bytes += duplicate_write_opl3(&music_data, &vstat, &state, reg, val, detune, opl3_keyon_wait, ch_panning, v_ratio0, v_ratio1);
            } else {
                forward_write(&music_data, 0, reg, val);
            }
            continue;
        }
        // Short wait command (0x70-0x7F)
        else if (cmd >= 0x70 && cmd <= 0x7F) {
            vgm_wait_short(&music_data, &vstat, cmd);
            read_done_byte++;
            continue;
        }
        // Wait n samples (0x61)
        else if (cmd == 0x61) {
            if (read_done_byte + 2 >= filesize) {
                fprintf(stderr, "Truncated 0x61 at end of file\n");
                break;
            }
            uint8_t lo = pVgmData[read_done_byte + 1];
            uint8_t hi = pVgmData[read_done_byte + 2];
            uint16_t samples = lo | (hi << 8);
            vgm_wait_samples(&music_data, &vstat, samples);
            read_done_byte += 3;
            continue;
        }
        // Wait 60Hz (0x62)
        else if (cmd == 0x62) {
            vgm_wait_60hz(&music_data, &vstat);
            read_done_byte++;
            continue;
        }
        // Wait 50Hz (0x63)
        else if (cmd == 0x63) {
            vgm_wait_50hz(&music_data, &vstat);
            read_done_byte++;
            continue;
        }
        // End of sound data (0x66)
        else if (cmd == 0x66) {
            vgm_append_byte(&music_data, cmd);
            read_done_byte++;
            break;
        }
        // Other commands: copy as-is
        else {
            vgm_append_byte(&music_data, cmd);
            read_done_byte++;
            continue;
        }
    }

    // GD3 tag handling: read, modify, rebuild
    char *gd3_fields[GD3_FIELDS] = {NULL};
    uint32_t orig_gd3_ver = 0, orig_gd3_len = 0;
    if (extract_gd3_fields(pVgmData, filesize, gd3_fields, &orig_gd3_ver, &orig_gd3_len) != 0) {
        fprintf(stderr, "Original GD3 not found, will use empty fields.\n");
        for (int i = 0; i < GD3_FIELDS; ++i) gd3_fields[i] = strdup("");
        orig_gd3_ver = 0x00000100;
    }
    char creator_append[128];
    snprintf(creator_append, sizeof(creator_append), ",%s", pCreator);

    char note_append[512];
    snprintf(note_append, sizeof(note_append),
        ", Converted from YM3812 to OPL3. Port 0 (ch0-8): original, Port 1 (ch9-17): detuned for chorus. Detune:%.2f%% KEY ON/OFF wait:%d Ch Panning mode:%d port0 volume:%.2f%% port1 volume:%.2f%%",
        detune, opl3_keyon_wait, ch_panning, v_ratio0 * 100, v_ratio1 * 100);

    dynbuffer_t gd3;
    buffer_init(&gd3);
    build_new_gd3_chunk(&gd3, gd3_fields, orig_gd3_ver, creator_append, note_append);

    for (int i = 0; i < GD3_FIELDS; ++i) free(gd3_fields[i]);

    // Calculate output header and offsets
    uint32_t music_data_size = (uint32_t)music_data.size;
    uint32_t gd3_size = (uint32_t)gd3.size;

    // Use larger of original header size or 0x100 (VGM_HEADER_SIZE)
    uint32_t header_size = (orig_header_size > 0x100) ? orig_header_size : 0x100;
    uint32_t new_eof_offset = music_data_size + header_size + gd3_size - 1;
    uint32_t vgm_eof_offset_field = new_eof_offset - 0x04;
    uint32_t gd3_offset_field_value = header_size + music_data_size - 0x14;
    uint32_t data_offset = header_size - 0x34;

    // Calculate new loop offset: set to header_size + music_data offset (if loop exists)
    uint32_t new_loop_offset = 0xFFFFFFFF;
    if (loop_start_in_music_data >= 0) {
        new_loop_offset = header_size + (uint32_t)loop_start_in_music_data;
    }

    // Allocate header buffer
    uint8_t *header_buf = (uint8_t*)calloc(1, header_size);

    // Build new VGM header
    build_vgm_header(
        header_buf,
        pVgmData,
        vstat.new_total_samples,
        vgm_eof_offset_field,
        gd3_offset_field_value,
        data_offset,
        0x00000171,
        additional_bytes
    );

    // Overwrite loop offset field with correct value
    if (new_loop_offset != 0xFFFFFFFF) {
        header_buf[0x1C] = (uint8_t)(new_loop_offset & 0xFF);
        header_buf[0x1D] = (uint8_t)((new_loop_offset >> 8) & 0xFF);
        header_buf[0x1E] = (uint8_t)((new_loop_offset >> 16) & 0xFF);
        header_buf[0x1F] = (uint8_t)((new_loop_offset >> 24) & 0xFF);
    }

    // Set OPL3 clock and zero YM3812 clock
    set_opl3_clock(header_buf, OPL3_CLOCK);
    set_ym3812_clock(header_buf, 0);

    // Write output VGM file: header, music data, GD3 chunk
    FILE *wf = fopen(pOutputPath, "wb");
    if (!wf) {
        fprintf(stderr, "Failed to open output file for writing: %s\n", pOutputPath);
        buffer_free(&music_data);
        buffer_free(&gd3);
        free(pVgmData);
        free(header_buf);
        return 1;
    }
    fwrite(header_buf, 1, header_size, wf);
    fwrite(music_data.pData, 1, music_data.size, wf);
    fwrite(gd3.pData, 1, gd3.size, wf);
    fclose(wf);

    printf("Converted VGM written to: %s\n", pOutputPath);
    printf("Detune value: %g%%\n", detune);
    printf("Wait value: %d\n", opl3_keyon_wait);
    printf("Creator: %s\n", pCreator);
    printf("Channel Panning Mode: %d\n", ch_panning);
    printf("Port0 Volume: %.2f%%\n", v_ratio0 * 100);
    printf("Port1 Volume: %.2f%%\n", v_ratio1 * 100);

    // Free resources
    buffer_free(&music_data);
    buffer_free(&gd3);
    free(pVgmData);
    free(header_buf);

    return 0;
}