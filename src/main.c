#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "compat_string.h"
#include "compat_bool.h"
#include "vgm/vgm_helpers.h"
#include "vgm/vgm_header.h"
#include "opl3/opl3_convert.h"
#include "opl3/opl3_debug_util.h"
#include "opll/opll_to_opl3_wrapper.h"
#include "vgm/gd3_util.h"

// Default values for command options
#define DEFAULT_DETUNE        1.0
#define DEFAULT_WAIT          0
#define DEFAULT_CH_PANNING    0
#define DEFAULT_VOLUME_RATIO0 1.0
#define DEFAULT_VOLUME_RATIO1 0.8
#define DEFAULT_CARRIER_TL_CLAMP_ENABLED 0
#define DEFAULT_CARRIER_TL_CLAMP 63

int verbose = 0;

// DebugOpts g_dbg = {0}; ← deleted

/** Fixed command lengths for multi-byte VGM commands (for safe copying) */
typedef struct {
    uint8_t code;
    uint8_t length; // code + params total length
} VGMFixedCmdLen;

static const VGMFixedCmdLen kKnownFixedCmds[] = {
    {0xA0, 3}, // AY8910
    {0xD2, 4}, // K051649
    // Add others if necessary
};

/** Find command specification by code */
static const VGMFixedCmdLen* find_fixed_cmd(uint8_t code) {
    for (size_t i = 0; i < sizeof(kKnownFixedCmds)/sizeof(kKnownFixedCmds[0]); ++i) {
        if (kKnownFixedCmds[i].code == code) return &kKnownFixedCmds[i];
    }
    return NULL;
}

/** Parse command line for OPL chip conversion flags and debug options */
static void parse_chip_conversion_flags(int argc, char *argv[], VGMChipClockFlags *chip_flags, DebugOpts *debug) {
    chip_flags->opl_group_autodetect = true;
    chip_flags->convert_ym2413 = false;
    chip_flags->convert_ym3812 = false;
    chip_flags->convert_ym3526 = false;
    chip_flags->convert_y8950  = false;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--convert-ym2413") == 0) {
            chip_flags->convert_ym2413 = true;
            chip_flags->opl_group_autodetect = false;
        } else if (strcmp(argv[i], "--convert-ym3812") == 0) {
            chip_flags->convert_ym3812 = true;
            chip_flags->opl_group_autodetect = false;
        } else if (strcmp(argv[i], "--convert-ym3526") == 0) {
            chip_flags->convert_ym3526 = true;
            chip_flags->opl_group_autodetect = false;
        } else if (strcmp(argv[i], "--convert-y8950") == 0) {
            chip_flags->convert_y8950 = true;
            chip_flags->opl_group_autodetect = false;
        }
        // Debug/diagnostic options
        else if (strcmp(argv[i], "--strip-non-opl") == 0) debug->strip_non_opl = true;
        else if (strcmp(argv[i], "--test-tone") == 0) debug->test_tone = true;
        else if (strcmp(argv[i], "--fast-attack") == 0) debug->fast_attack = true;
        else if (strcmp(argv[i], "--no-post-keyon-tl") == 0) debug->no_post_keyon_tl = true;
        else if (strcmp(argv[i], "--single-port") == 0) debug->single_port = true;
        else if (strcmp(argv[i], "--audible-sanity") == 0) debug->audible_sanity = true;
        else if (strcmp(argv[i], "--debug-verbose") == 0) debug->verbose = true;
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-verbose") == 0) debug->verbose = true;
    }
}

/** Read a little-endian 32-bit integer from buffer */
static uint32_t read_le_uint32(const unsigned char *p_ptr) {
    return (uint32_t)p_ptr[0] |
           ((uint32_t)p_ptr[1] << 8) |
           ((uint32_t)p_ptr[2] << 16) |
           ((uint32_t)p_ptr[3] << 24);
}

/** Check file extension for .vgm or none (for vgz-uncompressed) */
static bool has_vgm_extension_or_none(const char *p_filename) {
    size_t len = strlen(p_filename);
    if (len > 4 && strcasecmp(p_filename + len - 4, ".vgm") == 0) return true;
    const char *p_basename = strrchr(p_filename, '/');
    p_basename = p_basename ? p_basename + 1 : p_filename;
    if (strchr(p_basename, '.') == NULL) return true;
    return false;
}

/** Generate output file name based on input name */
static void make_default_output_name(const char *p_input, char *p_output, size_t outlen) {
    size_t len = strlen(p_input);
    if (len > 4 && strcmp(&p_input[len - 4], ".vgm") == 0) len -= 4;
    snprintf(p_output, outlen, "%.*sOPL3.vgm", (int)len, p_input);
}

/** Safely copy multi-byte command to output buffer */
static int copy_bytes_checked(VGMBuffer *dst, const unsigned char *src, long filesize,
                              long current_offset, int length) {
    if (current_offset + length > filesize) {
        fprintf(stderr, "[ERROR] Truncated command at EOF (need %d bytes, remain %ld)\n",
                length, filesize - current_offset);
        return 0;
    }
    for (int i = 0; i < length; ++i) {
        vgm_append_byte(dst, src[current_offset + i]);
    }
    return 1;
}

/** Print usage and help message (fully English) */
static void print_usage(const char *progname) {
    printf(
        "Usage: %s <input.vgm> <detune> [wait] [creator]\n"
        "          [-o <output.vgm>] [-ch_panning <val>] [-vr0 <val>] [-vr1 <val>] [-v | -verbose]\n"
        "          [--convert-ymXXXX ...] [--override <overrides.json>]\n"
        "          [--strip-non-opl] [--test-tone] [--fast-attack]\n"
        "          [--no-post-keyon-tl] [--single-port]\n"
        "          [--carrier-tl-clamp <val>] [--emergency-boost <val>] [--force-retrigger-each-note]\n"
        "          [--audible-sanity] [--debug-verbose]\n"
        "          [--min-gate-samples <val>] [--pre-keyon-wait <val>] [--min-off-on-wait <val>]\n"
        "          [--strip-unused-chips] [--opl3-clock <val>]\n"
        "\n"
        "Options:\n"
        "  --convert-ymXXXX           Explicit chip selection (YM2413, YM3812, YM3526, Y8950).\n"
        "                             (Default: OPL group auto-detection; first OPL chip is converted unless specified)\n"
        "  --strip-non-opl            Remove AY8910/K051649 (and similar) commands from output.\n"
        "  --test-tone                Inject a simple OPL3 test tone at start for audibility check.\n"
        "  --fast-attack              Force fast envelope (AR=15, DR>=4, Carrier TL=0).\n"
        "  --no-post-keyon-tl         Suppress TL changes immediately after KeyOn.\n"
        "  --single-port              Emit only port0 writes (suppress port1 duplicates).\n"
        "  --carrier-tl-clamp <val>   Clamp final Carrier TL value (range: 0..63 or 0x00..0x3F).\n"
        "  --emergency-boost <val>    Force Carrier TL even lower (increase volume for test/audibility).\n"
        "  --force-retrigger-each-note  Retrigger attack for every note (forces key-on for each note event).\n"
        "  --audible-sanity           Force fast envelope & audible TL for debug purposes.\n"
        "  --debug-verbose            Print verbose information for detailed debug.\n"
        "  -o <output.vgm>            Output file name (otherwise auto-generated).\n"
        "  -ch_panning <val>          Channel panning mode.\n"
        "  -vr0 <val>, -vr1 <val>     Port0/Port1 volume ratios.\n"
        "  -v, -verbose               Print verbose information.\n"
        "  --override <overrides.json>  Apply override settings from overrides.json.\n"
        "  --min-gate-samples <val>   Minimum gate duration in samples per note event (OPLL_MIN_GATE_SAMPLES).\n"
        "                             This ensures the key-on (gate) signal is held for at least <val> samples, guaranteeing proper note triggering in OPLL emulation.\n"
        "  --pre-keyon-wait <val>     Number of samples to wait before key-on event (OPLL_PRE_KEYON_WAIT_SAMPLES).\n"
        "                             Allows internal chip state stabilization before key-on.\n"
        "  --min-off-on-wait <val>    Minimum samples to wait between key-off and key-on (OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES).\n"
        "                             Ensures reliable note retriggering in emulation.\n"
        "  --strip-unused-chips       Set unused chip clocks (YM2413/AY/etc.) to zero in output.\n"
        "  --opl3-clock <val>         Override YMF262 (OPL3) clock value (e.g., 14318180).\n"
        "  -h, --help                 Show this help message.\n"
        "\n"
        "Example:\n"
        "  %s music.vgm 1.0 --convert-ym2413 --strip-non-opl --fast-attack --carrier-tl-clamp 58 --audible-sanity --debug-verbose -o out.vgm\n",
        progname, progname
    );
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // Parse main arguments
    const char *p_input_vgm = argv[1];
    double detune = atof(argv[2]);
    int opl3_keyon_wait = DEFAULT_WAIT;
    const char *p_creator = "eseopl3patcher";
    const char *p_output_path = NULL;
    int ch_panning = DEFAULT_CH_PANNING;
    double v_ratio0 = DEFAULT_VOLUME_RATIO0;
    double v_ratio1 = DEFAULT_VOLUME_RATIO1;
    int carrier_tl_clamp_enabled = DEFAULT_CARRIER_TL_CLAMP_ENABLED;
    uint8_t carrier_tl_clamp = DEFAULT_CARRIER_TL_CLAMP;
    int emergency_boost_steps= 0; /* Default is disabled */
    bool force_retrigger_each_note = false;
    // Added: audible-sanity runtime value (if 0, use build-time default)
    uint16_t min_gate_samples        = 8196; // Equivalent to OPLL_MIN_GATE_SAMPLES
    uint16_t pre_keyon_wait_samples  = 16;   // Equivalent to OPLL_PRE_KEYON_WAIT_SAMPLES
    uint16_t min_off_on_wait_samples = 16;   // Equivalent to OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES

    // Added: Header formatting
    bool strip_unused_chip_clocks = false;   // Zero unused chip clocks
    uint32_t override_opl3_clock  = 0;       // If not 0, override OPL3 clock
    DebugOpts debug_opts = {0};

    // Parse optional args
    for (int i = 3; i < argc; ++i) {
        // Helper for strtoul
        char *endptr;

        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            p_output_path = argv[++i];
        } else if (strcmp(argv[i], "-ch_panning") == 0 && i + 1 < argc) {
            ch_panning = (int)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "-vr0") == 0 && i + 1 < argc) {
            v_ratio0 = atof(argv[++i]);
        } else if (strcmp(argv[i], "-vr1") == 0 && i + 1 < argc) {
            v_ratio1 = atof(argv[++i]);
        } else if (strcmp(argv[i], "--carrier-tl-clamp") == 0 && i + 1 < argc) {
            carrier_tl_clamp_enabled = 1;
            carrier_tl_clamp = (uint8_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--emergency-boost") == 0 && i + 1 < argc) {
            emergency_boost_steps = (int)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--force-retrigger-each-note") == 0) {
            force_retrigger_each_note = true;
        } else if (strcmp(argv[i], "--audible-sanity") == 0) {
            debug_opts.audible_sanity = true;
        } else if (strcmp(argv[i], "--debug-verbose") == 0) {
            debug_opts.verbose = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-verbose") == 0) {
            debug_opts.verbose = true;
        } else if (strcmp(argv[i], "--strip-non-opl") == 0) {
            debug_opts.strip_non_opl = true;
        } else if (strcmp(argv[i], "--test-tone") == 0) {
            debug_opts.test_tone = true;
        } else if (strcmp(argv[i], "--fast-attack") == 0) {
            debug_opts.fast_attack = true;
        } else if (strcmp(argv[i], "--no-post-keyon-tl") == 0) {
            debug_opts.no_post_keyon_tl = true;
        } else if (strcmp(argv[i], "--single-port") == 0) {
            debug_opts.single_port = true;
        } else if (strcmp(argv[i], "--min-gate-samples") == 0 && i + 1 < argc) {
            min_gate_samples = (uint16_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--pre-keyon-wait") == 0 && i + 1 < argc) {
            pre_keyon_wait_samples = (uint16_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--min-off-on-wait") == 0 && i + 1 < argc) {
            min_off_on_wait_samples = (uint16_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--strip-unused-chips") == 0) {
            strip_unused_chip_clocks = true;
        } else if (strcmp(argv[i], "--opl3-clock") == 0 && i + 1 < argc) {
            override_opl3_clock = (uint32_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            int val = (int)strtoul(argv[i], &endptr, 10);
            if (*endptr == '\0' && opl3_keyon_wait == DEFAULT_WAIT) {
                opl3_keyon_wait = val;
            } else if (p_creator == NULL || strcmp(p_creator, "eseopl3patcher") == 0) {
                p_creator = argv[i];
            }
        }
    }

    // Parse chip flags and debug options
    VGMChipClockFlags chip_flags = {0};
    parse_chip_conversion_flags(argc, argv, &chip_flags, &debug_opts);

    CommandOptions cmd_opts = {
        .detune = detune,
        .opl3_keyon_wait = opl3_keyon_wait,
        .ch_panning = ch_panning,
        .v_ratio0 = v_ratio0,
        .v_ratio1 = v_ratio1,
        .carrier_tl_clamp_enabled = carrier_tl_clamp_enabled,
        .carrier_tl_clamp = carrier_tl_clamp,
        .emergency_boost_steps = emergency_boost_steps,
        .force_retrigger_each_note = force_retrigger_each_note,
        .min_gate_samples = min_gate_samples,
        .pre_keyon_wait_samples = pre_keyon_wait_samples,
        .min_off_on_wait_samples = min_off_on_wait_samples,
        .strip_unused_chip_clocks = strip_unused_chip_clocks,
        .override_opl3_clock = override_opl3_clock,
        .debug = debug_opts
    };

    // Output file name
    char default_out[256];
    if (!p_output_path) {
        make_default_output_name(p_input_vgm, default_out, sizeof(default_out));
        p_output_path = default_out;
    }

    // File extension check
    if (!has_vgm_extension_or_none(p_input_vgm)) {
        fprintf(stderr, "Input file must have .vgm extension or no extension.\n");
        return 1;
    }

    // Open input file
    FILE *p_fp = fopen(p_input_vgm, "rb");
    if (!p_fp) {
        fprintf(stderr, "Cannot open input file: %s\n", p_input_vgm);
        return 1;
    }
    fseek(p_fp, 0, SEEK_END);
    long filesize = ftell(p_fp);
    fseek(p_fp, 0, SEEK_SET);

    unsigned char *p_vgm_data = (unsigned char*)malloc(filesize);
    if (!p_vgm_data || fread(p_vgm_data, 1, filesize, p_fp) != (size_t)filesize) {
        fprintf(stderr, "Failed to read entire file!\n");
        free(p_vgm_data);
        fclose(p_fp);
        return 1;
    }
    fclose(p_fp);

    if (memcmp(p_vgm_data, "Vgm ", 4) != 0) {
        fprintf(stderr, "Not a valid VGM file.\n");
        free(p_vgm_data);
        return 1;
    }

    // Header/data offsets
    uint32_t vgm_data_offset = (filesize >= 0x34) ? read_le_uint32(p_vgm_data + 0x34) : 0;
    uint32_t orig_header_size = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;
    long data_start = 0x34 + (vgm_data_offset ? vgm_data_offset : 0x0C);
    if (data_start >= filesize) {
        fprintf(stderr, "Invalid VGM data offset.\n");
        free(p_vgm_data);
        return 1;
    }
    uint32_t orig_loop_offset = read_le_uint32(p_vgm_data + 0x1C);
    uint32_t orig_loop_address = (orig_loop_offset != 0xFFFFFFFF) ? (orig_loop_offset + 0x04) : 0;

    // VGMContext setup
    VGMContext vgmctx;
    vgm_buffer_init(&vgmctx.buffer);
    vgm_timestamp_init(&vgmctx.timestamp, 44100.0);
    vgmctx.status.total_samples = 0;
    memset(&vgmctx.header, 0, sizeof(vgmctx.header));
    vgmctx.gd3.data = NULL;
    vgmctx.gd3.size = 0;
    vgmctx.status.stats.ym2413_write_count = 0;
    vgmctx.status.stats.ym3812_write_count = 0;
    vgmctx.status.stats.ym3526_write_count = 0;
    vgmctx.status.stats.y8950_write_count = 0;
    // Parse chip clocks
    if (!vgm_parse_chip_clocks(p_vgm_data, filesize, &chip_flags)) {
        fprintf(stderr, "Failed to parse VGM header for chip clocks.\n");
        free(p_vgm_data);
        return 1;
    }

    // FM clock setup (source chip selection)
    if (chip_flags.convert_ym2413 && chip_flags.has_ym2413) {
        vgmctx.source_fmchip = FMCHIP_YM2413;
        vgmctx.source_fm_clock = (double)chip_flags.ym2413_clock;
    } else if (chip_flags.convert_ym3812 && chip_flags.has_ym3812) {
        vgmctx.source_fmchip = FMCHIP_YM3812;
        vgmctx.source_fm_clock = (double)chip_flags.ym3812_clock;
    } else if (chip_flags.convert_ym3526 && chip_flags.has_ym3526) {
        vgmctx.source_fmchip = FMCHIP_YM3526;
        vgmctx.source_fm_clock = (double)chip_flags.ym3526_clock;
    } else if (chip_flags.convert_y8950 && chip_flags.has_y8950) {
        vgmctx.source_fmchip = FMCHIP_Y8950;
        vgmctx.source_fm_clock = (double)chip_flags.y8950_clock;
    } else {
        vgmctx.source_fm_clock = -1.0;
    }
    vgmctx.target_fm_clock = OPL3_CLOCK;
    
    if (cmd_opts.debug.verbose) {
        printf("[VGM] FM chip usage:\n");
        printf(" YM2413:%s clock=%u\n", chip_flags.has_ym2413?"Y":"N", chip_flags.ym2413_clock);
        printf(" YM3812:%s clock=%u\n", chip_flags.has_ym3812?"Y":"N", chip_flags.ym3812_clock);
        printf(" YM3526:%s clock=%u\n", chip_flags.has_ym3526?"Y":"N", chip_flags.ym3526_clock);
        printf(" Y8950 :%s clock=%u\n", chip_flags.has_y8950 ?"Y":"N", chip_flags.y8950_clock);
    }

    OPL3State state;
    memset(&state, 0, sizeof(state));
    state.rhythm_mode = false;
    state.opl3_mode_initialized = false;

    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YMF262);
    opll_set_program_args(argc, argv);
    opll_init(&state, &cmd_opts);

    long read_done_byte = data_start;
    long loop_start_in_buffer = -1;
    uint32_t additional_bytes = 0;

    while (read_done_byte < filesize) {
        if (orig_loop_offset != 0xFFFFFFFF && read_done_byte == orig_loop_address) {
            loop_start_in_buffer = vgmctx.buffer.size;
        }
        uint8_t cmd = p_vgm_data[read_done_byte];

        /* OPL-family autodetect */
        if (chip_flags.opl_group_autodetect) {
            if (cmd == 0x51 && !chip_flags.convert_ym2413 && !chip_flags.convert_ym3812 &&
                !chip_flags.convert_ym3526 && !chip_flags.convert_y8950) {
                chip_flags.convert_ym2413 = true;
                chip_flags.opl_group_autodetect = false;
                chip_flags.opl_group_first_cmd = 0x51;
                vgmctx.source_fmchip = FMCHIP_YM2413;
                vgmctx.source_fm_clock = (double)chip_flags.ym2413_clock;
            } else if (cmd == 0x5A && !chip_flags.convert_ym2413 && !chip_flags.convert_ym3812 &&
                       !chip_flags.convert_ym3526 && !chip_flags.convert_y8950) {
                chip_flags.convert_ym3812 = true;
                chip_flags.opl_group_autodetect = false;
                chip_flags.opl_group_first_cmd = 0x5A;
                vgmctx.source_fmchip = FMCHIP_YM3812;
                vgmctx.source_fm_clock = (double)chip_flags.ym3812_clock;
            } else if (cmd == 0x5B && !chip_flags.convert_ym2413 && !chip_flags.convert_ym3812 &&
                       !chip_flags.convert_ym3526 && !chip_flags.convert_y8950) {
                chip_flags.convert_ym3526 = true;
                chip_flags.opl_group_autodetect = false;
                chip_flags.opl_group_first_cmd = 0x5B;
                vgmctx.source_fmchip = FMCHIP_YM3526;
                vgmctx.source_fm_clock = (double)chip_flags.ym3526_clock;
            } else if (cmd == 0x5C && !chip_flags.convert_ym2413 && !chip_flags.convert_ym3812 &&
                       !chip_flags.convert_ym3526 && !chip_flags.convert_y8950) {
                chip_flags.convert_y8950 = true;
                chip_flags.opl_group_autodetect = false;
                chip_flags.opl_group_first_cmd = 0x5C;
                vgmctx.source_fmchip = FMCHIP_Y8950;
                vgmctx.source_fm_clock = (double)chip_flags.y8950_clock;
            }
        }

        /* YM2413 */
        if (cmd == 0x51) {
            // Updates the stats
            vgmctx.status.stats.ym2413_write_count++;

            if (read_done_byte + 2 >= filesize) {
                fprintf(stderr, "Truncated YM2413 command.\n");
                break;
            }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;

            if (cmd_opts.debug.verbose)
                printf("YM2413 write: reg=0x%02X val=0x%02X (pos=%ld/%ld)\n",
                       reg, val, read_done_byte, filesize);

            uint16_t wait_samples = 0;
            if (read_done_byte < filesize) {
                uint8_t peek = p_vgm_data[read_done_byte];
                if (peek >= 0x70 && peek <= 0x7F) {
                    wait_samples = (peek & 0x0F) + 1;
                    read_done_byte += 1;
                } else if (peek == 0x61 && read_done_byte + 2 < filesize) {
                    uint8_t lo = p_vgm_data[read_done_byte + 1];
                    uint8_t hi = p_vgm_data[read_done_byte + 2];
                    wait_samples = lo | (hi << 8);
                    read_done_byte += 3;
                } else if (peek == 0x62) {
                    wait_samples = 735; read_done_byte += 1;
                } else if (peek == 0x63) {
                    wait_samples = 882; read_done_byte += 1;
                }
            }

            if (chip_flags.convert_ym2413) {
                if (!state.opl3_mode_initialized) {
                    if (cmd_opts.debug.verbose) printf("Initializing OPL3 mode for YM2413...\n");
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM2413);
                    state.opl3_mode_initialized = true;
                }
                opll_write_register(&vgmctx.buffer, &vgmctx, &state,
                                    reg, val, wait_samples, &cmd_opts);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
                if (wait_samples) {
                    vgm_wait_samples(&vgmctx.buffer, &vgmctx.status, wait_samples);
                }
            }
            continue;
        }

        /* YM3812 */
        if (cmd == 0x5A) {
            // Updates the stats
            vgmctx.status.stats.ym3812_write_count++;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc YM3812\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_ym3812) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM3812);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, &cmd_opts, 0);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }

        /* YM3526 */
        if (cmd == 0x5B) {
            // Updates the stats
            vgmctx.status.stats.ym3526_write_count++;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc YM3526\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_ym3526) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_YM3526);
                    state.opl3_mode_initialized = true;
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, &cmd_opts, 0);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }

        /* Y8950 */
        if (cmd == 0x5C) {
            // Updates the stats
            vgmctx.status.stats.y8950_write_count++;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc Y8950\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_y8950) {
                if (!state.opl3_mode_initialized) {
                    opl3_init(&vgmctx.buffer, ch_panning, &state, FMCHIP_Y8950);
                    state.opl3_mode_initialized = true;
                    if (cmd_opts.debug.test_tone) {
                        // Simple additive test tone: mod muted, carrier AR=15 etc.
                        // Port0 only
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0x05, 0x01, &cmd_opts, 0); // ensure OPL3 mode
                        // Operator settings (slot 0 carrier path simplified)
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0x20, 0x01, &cmd_opts, 0); // mul=1
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0x40, 0x00, &cmd_opts, 0); // TL=0
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0x60, 0xF4, &cmd_opts, 0); // AR=F DR=4
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0x80, 0x02, &cmd_opts, 0); // SL=0 RR=2
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0xE0, 0x00, &cmd_opts, 0);
                        // Set algorithm=1 (additive)
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0xC0, 0xC1, &cmd_opts, 0); // FB=0 Alg=1
                        // FNUM for ~A440 (example fnum=0x15B oct=4) => LSB
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0xA0, 0x5B, &cmd_opts, 0);
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0xB0, 0x20 | (4<<2) | 0x01, &cmd_opts, 0); // MSB=1, block=4, keyon
                        vgm_wait_samples(&vgmctx.buffer,&vgmctx.status,4410); // 100ms
                        duplicate_write_opl3(&vgmctx.buffer,&vgmctx.status,&state, 0xB0, 0x20 | (4<<2) | 0x00, &cmd_opts, 0); // key off
                    }
                }
                additional_bytes += duplicate_write_opl3(&vgmctx.buffer, &vgmctx.status, &state, reg, val, &cmd_opts, 0);
            } else {
                forward_write(&vgmctx.buffer, 0, reg, val);
            }
            continue;
        }

        /* Other OPN-family passthrough */
        if (cmd == 0x52 || cmd == 0x54 || cmd == 0x55 || cmd == 0x56 || cmd == 0x57) {
            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc OPN-like\n"); break; }
            forward_write(&vgmctx.buffer, 0, p_vgm_data[read_done_byte + 1], p_vgm_data[read_done_byte + 2]);
            read_done_byte += 3;
            continue;
        }

        /* Wait process */
        if (cmd >= 0x70 && cmd <= 0x7F) {
            vgm_wait_short(&vgmctx.buffer, &vgmctx.status, cmd);
            read_done_byte += 1;
            continue;
        }
        if (cmd == 0x61) {
            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc wait 0x61\n"); break; }
            uint16_t ws = p_vgm_data[read_done_byte + 1] | (p_vgm_data[read_done_byte + 2] << 8);
            vgm_wait_samples(&vgmctx.buffer, &vgmctx.status, ws);
            read_done_byte += 3;
            continue;
        }
        if (cmd == 0x62) { vgm_wait_60hz(&vgmctx.buffer, &vgmctx.status); read_done_byte += 1; continue; }
        if (cmd == 0x63) { vgm_wait_50hz(&vgmctx.buffer, &vgmctx.status); read_done_byte += 1; continue; }

        /* End */
        if (cmd == 0x66) {
            vgm_append_byte(&vgmctx.buffer, 0x66);
            read_done_byte += 1;
            break; /* End reached */
        }

        /* --- New: Safe copy of other chips (AY8910 / K051649) --- */
        const VGMFixedCmdLen *spec = find_fixed_cmd(cmd);
        if (spec) {
            if (cmd_opts.debug.strip_non_opl) {
                // Skip this command entirely
                read_done_byte += spec->length;
                continue;
            } else {
                if (!copy_bytes_checked(&vgmctx.buffer, p_vgm_data, filesize, read_done_byte, spec->length)) {
                    break;
                }
                read_done_byte += spec->length;
                continue;
            }
        }

        /* Unknown command: For easier analysis, copy only 1 byte and emit warning */
        if (cmd_opts.debug.verbose) {
            fprintf(stderr, "[WARN] Unknown VGM command 0x%02X at offset 0x%lX (forward as raw)\n",
                    cmd, read_done_byte);
        }
        vgm_append_byte(&vgmctx.buffer, cmd);
        read_done_byte += 1;
    }

    /* GD3 Rebuild */
    char *p_gd3_fields[GD3_FIELDS] = {0};
    uint32_t orig_gd3_ver = 0, orig_gd3_len = 0;
    if (extract_gd3_fields(p_vgm_data, filesize, p_gd3_fields, &orig_gd3_ver, &orig_gd3_len) != 0) {
        for (int i = 0; i < GD3_FIELDS; ++i) p_gd3_fields[i] = strdup("");
        orig_gd3_ver = 0x00000100;
    }
    char creator_append[128];
    snprintf(creator_append, sizeof(creator_append), ",%s", p_creator);
    char note_append[512];
    
    if (vgmctx.source_fmchip == FMCHIP_YM2413) {
        snprintf(note_append, sizeof(note_append),
            "Converted from YM2413 to OPL3. "
            "Detune:%.2f%% "
            "audible_sanity:%s "
            "min_gate:%u "
            "pre_on:%u "
            "off_on:%u "
            "boost:%d "
            "clamp:%s(%u)",
            detune,
            cmd_opts.debug.audible_sanity ? "ON":"OFF",
            (unsigned)(cmd_opts.min_gate_samples ? cmd_opts.min_gate_samples : 0),
            (unsigned)(cmd_opts.pre_keyon_wait_samples ? cmd_opts.pre_keyon_wait_samples : 0),
            (unsigned)(cmd_opts.min_off_on_wait_samples ? cmd_opts.min_off_on_wait_samples : 0),
            cmd_opts.emergency_boost_steps,
            cmd_opts.carrier_tl_clamp_enabled ? "ON":"OFF",
            cmd_opts.carrier_tl_clamp);
    } else {
        snprintf(note_append, sizeof(note_append),
                ", Converted from %s to OPL3. Detune:%.2f%% KEY ON/OFF wait:%d "
                "Ch Panning mode:%d port0 volume:%.2f%% port1 volume:%.2f%% carrier_tl_clamp:%s(%u) audible_sanity:%s debug_verbose:%s",
                get_converted_opl_chip_name(&chip_flags), detune, opl3_keyon_wait,
                ch_panning, v_ratio0 * 100, v_ratio1 * 100,
                carrier_tl_clamp_enabled ? "ON" : "OFF", carrier_tl_clamp,
                debug_opts.audible_sanity ? "ON" : "OFF",
                debug_opts.verbose ? "ON" : "OFF"
        );
    }



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
    uint32_t new_loop_offset = (loop_start_in_buffer >= 0)
                               ? (header_size + (uint32_t)loop_start_in_buffer)
                               : 0xFFFFFFFF;

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

    if (new_loop_offset != 0xFFFFFFFF) {
        p_header_buf[0x1C] = (uint8_t)(new_loop_offset & 0xFF);
        p_header_buf[0x1D] = (uint8_t)((new_loop_offset >> 8) & 0xFF);
        p_header_buf[0x1E] = (uint8_t)((new_loop_offset >> 16) & 0xFF);
        p_header_buf[0x1F] = (uint8_t)((new_loop_offset >> 24) & 0xFF);
    }

    /** Update the clock information in new header */
    vgm_header_postprocess(p_header_buf, &vgmctx, &cmd_opts);

   
    FILE *p_wf = fopen(p_output_path, "wb");
    if (!p_wf) {
        fprintf(stderr, "Failed to open output file: %s\n", p_output_path);
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
    printf("[OPL3] Detune value: %g%%\n", detune);
    printf("[OPL3] Wait value: %d\n", opl3_keyon_wait);
    printf("[OPL3] Creator: %s\n", p_creator);
    printf("[OPL3] Channel Panning Mode: %d\n", ch_panning);
    printf("[OPL3] Port0 Volume: %.2f%%\n", v_ratio0 * 100);
    printf("[OPL3] Port1 Volume: %.2f%%\n", v_ratio1 * 100);

    if (vgmctx.source_fmchip == FMCHIP_YM2413) {
        printf("[YM2413] Debug Verbose: %s\n", debug_opts.verbose ? "ON" : "OFF");
        printf("[YM2413] Audible Sanity: %s\n", debug_opts.audible_sanity ? "ON" : "OFF");
        printf("[YM2413] Emergency Boost: %d \n",  emergency_boost_steps);
        printf("[YM2413] Force retrigger each note: %s \n",  force_retrigger_each_note? "ON" : "OFF" );
        printf("[YM2413] Carrier TL Clamp: %s (%u)\n", carrier_tl_clamp_enabled ? "ON" : "OFF", carrier_tl_clamp);
        printf("[YM2413] Minimum gate duration [samples]:%u \n",min_gate_samples);
        printf("[YM2413] Minimum gate :%u \n", pre_keyon_wait_samples);
        printf("[YM2413] Min weight samples:%u \n", min_off_on_wait_samples);
    }

    if (cmd_opts.debug.verbose) {
        printf("[OPL3] Total voices in DB: %d\n", state.voice_db.count);
    }

    vgm_buffer_free(&vgmctx.buffer);
    vgm_buffer_free(&gd3);
    free(p_vgm_data);
    free(p_header_buf);
    return 0;
}