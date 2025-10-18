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
#include "opll/opll2opl3_conv.h"
#include "vgm/gd3_util.h"

// Default values for command options
#define DEFAULT_DETUNE        1.0
#define DEFAULT_WAIT          0
#define DEFAULT_CH_PANNING    0
#define DEFAULT_VOLUME_RATIO0 1.0
#define DEFAULT_VOLUME_RATIO1 0.8
#define DEFAULT_DETUNE_LIMIT 4
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
static void print_usage(const char *progname, DebugOpts *debug) {
    if (debug->verbose){
        printf(
            "Usage: %s <input.vgm> <detune> [wait] [creator]\n"
            "          [-o <output.vgm>] [--ch_panning <val>] [--vr0 <val>] [--vr1 <val>] [--detune <val>] [--detune_limit <val>] [--wait <val>]\n"
            "          [--convert-ymXXXX ...] [--keep_source_vgm] [--override <overrides.json>]\n"
            "          [--strip-non-opl] [--test-tone] [--fast-attack]\n"
            "          [--no-post-keyon-tl] [--single-port]\n"
            "          [--carrier-tl-clamp <val>] [--emergency-boost <val>] [--force-retrigger-each-note]\n"
            "          [--audible-sanity] [--debug-verbose]\n"
            "          [--min-gate-samples <val>] [--pre-keyon-wait <val>] [--min-off-on-wait <val>]\n"
            "          [--strip-unused-chips] [--opl3-clock <val>]\n"
            "\n"
            "Options:\n"
            "  --detune <val>             Detune percentage (can also specify as 2nd arg for backward compatibility).\n"
            "  --detune_limit <val>       Maximum detune absolute value (default: 4.0).\n"
            "  --wait <val>               KeyOn/Off wait samples.\n"
            "  --ch_panning <val>         Channel panning mode (0=mono, 1=alternate L/R, ...).\n"
            "  --vr0 <val>                Port0 volume ratio (default: 1.0).\n"
            "  --vr1 <val>                Port1 volume ratio (default: 0.8).\n"
            "  --keep_source_vgm          Output original vgm command \n"
            "  -o, --output <file>        Output file name (otherwise auto-generated).\n"
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
            "Examples:\n"
            "  %s music.vgm --detune 1.0 --convert-ym2413 --strip-non-opl --fast-attack --carrier-tl-clamp 58 --audible-sanity --debug-verbose -o out.vgm\n"
            "  %s music.vgm 1.0 --ch_panning 1 --vr0 1.0 --vr1 0.8 --detune_limit 2.5\n"
            ,
            progname, progname, progname
        );
    } else {
        printf(
            "Usage: %s <input.vgm> <detune> [wait] [creator]\n"
            "          [-o <output.vgm>] [--ch_panning <val>] [--vr0 <val>] [--vr1 <val>] [--detune <val>] [--detune_limit <val>] [--wait <val>]\n"
            "          [other options, see --help]\n"
            "\n"
            "Most commonly-used options:\n"
            "  --detune <val>             Detune percentage.\n"
            "  --detune_limit <val>       Maximum detune value.\n"
            "  --ch_panning <val>         Channel panning mode.\n"
            "  --vr0 <val>, --vr1 <val>   Port0/Port1 volume ratios.\n"
            "  -o <output.vgm>            Output file name.\n"
            "  -h, --help                 Show this help message.\n"
            "\n"
            "Example:\n"
            "  %s music.vgm --detune 1.0 -o out.vgm --ch_panning 1\n",
            progname,progname
        );
    }
}

/**
 * Decode preset string to OPLL_PresetType enum.
 * Supported values: "YM2413", "VRC7", "YMF281B"
 * Returns OPLL_PresetType_YM2413 for unknown input.
 */
static OPLL_PresetType decode_preset_type(const char *str) {
    if (!str) return OPLL_PresetType_YM2413;
    if (strcasecmp(str, "YM2413") == 0) return OPLL_PresetType_YM2413;
    if (strcasecmp(str, "VRC7") == 0)   return OPLL_PresetType_VRC7;
    if (strcasecmp(str, "YMF281B") == 0) return OPLL_PresetType_YMF281B;
    return OPLL_PresetType_YM2413; // default fallback
}

static int update_is_adding_bytes(VGMContext *vgmctx, uint32_t orig_loop_offset, uint32_t current_addr) {
    if (orig_loop_offset != 0xFFFFFFFF && current_addr < orig_loop_offset) {
        vgmctx->status.is_adding_port1_bytes = 1;
        return 1;
    } else {
        vgmctx->status.is_adding_port1_bytes = 0;
        return 0;
    }
}

static void update_loop_start_in_buffer(long read_done_byte, uint32_t orig_loop_address, VGMContext *vgmctx, long *loop_start_in_buffer) {
    if (orig_loop_address != 0xFFFFFFFF && read_done_byte == orig_loop_address) {
        *loop_start_in_buffer = vgmctx->buffer.size;
    }
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        DebugOpts debug_opts = {0};
        print_usage(argv[0],&debug_opts);
        return 1;
    }

    // Parse main arguments
    const char *p_input_vgm = argv[1];
    double detune = atof(argv[2]);
    double detune_limit = DEFAULT_DETUNE_LIMIT;
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
    bool is_keep_source_vgm = false;
    OPLL_PresetType preset = OPLL_PresetType_YM2413;
    const char *preset_str = "YM2413";

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
        } else if ((strcmp(argv[i], "-detune") == 0 || strcmp(argv[i], "--detune") == 0) && i + 1 < argc) {
            detune = atof(argv[++i]);
        } else if ((strcmp(argv[i], "-detune_limit") == 0 || strcmp(argv[i], "--detune_limit") == 0) && i + 1 < argc) {
            detune_limit = atof(argv[++i]);
        } else if ((strcmp(argv[i], "-ch_panning") == 0 || strcmp(argv[i], "--ch_panning") == 0) && i + 1 < argc) {
            ch_panning = (int)strtoul(argv[++i], &endptr, 10);
        } else if ((strcmp(argv[i], "-vr0") == 0 || strcmp(argv[i], "--vr0") == 0) && i + 1 < argc) {
            v_ratio0 = atof(argv[++i]);
        } else if ((strcmp(argv[i], "-vr1") == 0 || strcmp(argv[i], "--vr1") == 0) && i + 1 < argc) {
            v_ratio1 = atof(argv[++i]);
        } else if (strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--keep_source_vgm") == 0) {
            is_keep_source_vgm = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0],&debug_opts);
            return 0;
        } else if (strcmp(argv[i], "-debug") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_opts.verbose = true;
        } else if (strcmp(argv[i], "--carrier-tl-clamp") == 0 && i + 1 < argc) {
            carrier_tl_clamp_enabled = 1;
            carrier_tl_clamp = (uint8_t)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--emergency-boost") == 0 && i + 1 < argc) {
            emergency_boost_steps = (int)strtoul(argv[++i], &endptr, 10);
        } else if (strcmp(argv[i], "--force-retrigger-each-note") == 0) {
            force_retrigger_each_note = true;
        } else if (strcmp(argv[i], "--audible-sanity") == 0) {
            debug_opts.audible_sanity = true;
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
        } else if (argv[i][0] != '-') {
            int val = (int)strtoul(argv[i], &endptr, 10);
            if (*endptr == '\0' && opl3_keyon_wait == DEFAULT_WAIT) {
                opl3_keyon_wait = val;
            } else if (p_creator == NULL || strcmp(p_creator, "eseopl3patcher") == 0) {
                p_creator = argv[i];
            }
        } else if ((strcmp(argv[i], "-preset") == 0 || strcmp(argv[i], "--preset") == 0) && i + 1 < argc) {
            preset_str = argv[++i];
            preset = decode_preset_type(preset_str);
            continue;
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
        .detune_limit = detune_limit,
        .fm_mapping_style = FM_MappingStyle_modern,
        .is_port1_enabled = true,
        .is_voice_zero_clear = false,
        .is_a0_b0_aligned = false,
        .is_keep_source_vgm = is_keep_source_vgm,
        .preset = preset,
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
    // Read DataOffset (0x34) from VGM header
    uint32_t vgm_data_offset = read_le_uint32(p_vgm_data + 0x34);

    // If DataOffset is 0, adjust to 0x0C (VGM 1.01/1.10 compatibility)
    // This ensures that actual data starts at 0x40 (0x34 + 0x0C)
    if (vgm_data_offset == 0) {
        vgm_data_offset = 0x0C;
    }

    // Calculate original header size using DataOffset
    uint32_t orig_header_size = 0x34 + vgm_data_offset;

    // Ensure header size is at least 0x40 bytes (VGM minimum header size)
    if (orig_header_size < 0x40) orig_header_size = VGM_HEADER_SIZE;

    // Print debug information about header size
    fprintf(stderr, "orig_header_size: 0x%0x(%d).\n", orig_header_size, orig_header_size);

    // Calculate data start position (where music data begins)
    long data_start = 0x34 + vgm_data_offset;

    // Print debug information about data start offset
    fprintf(stderr, "data_start: 0x%0x(%d).\n", data_start, data_start);

    // Validate that data start offset is within file size
    if (data_start >= filesize) {
        fprintf(stderr, "Invalid VGM data offset.\n");
        free(p_vgm_data);
        return 1;
    }
    // Loop Offset ($1C): Offset from the start of the file to the loop point (relative to $00). The loop point in the data is at ($1C + $04).
    uint32_t orig_loop_offset = read_le_uint32(p_vgm_data + 0x1C);
    uint32_t orig_loop_address = (orig_loop_offset != 0xFFFFFFFF) ? (orig_loop_offset + 0x04) : 0;
    long pre_loop_output_bytes = 0;
    long loop_start_in_buffer = -1;

    // VGMContext setup
    VGMContext vgmctx;
    vgm_buffer_init(&vgmctx.buffer);
    vgmctx.timestamp.current_sample = 0;
    vgmctx.timestamp.last_sample = 0;
    vgmctx.timestamp.sample_rate = 44100.0;
    vgmctx.status.total_samples = 0;
    vgmctx.cmd_type =  VGMCommandType_Unkown;
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

    memset(&vgmctx.opl3_state, 0, sizeof(OPL3State));
    vgmctx.opl3_state.rhythm_mode = false;
    vgmctx.opl3_state.opl3_mode_initialized = false;

    memset(&vgmctx.opll_state, 0, sizeof(OPLLState));
    memset(&vgmctx.opll_state.reg, 0, 0x200);
    memset(&vgmctx.opll_state.reg_stamp, 0x0, 0x200);
    vgmctx.opll_state.is_rhythm_mode = false;
    vgmctx.opll_state.is_initialized = false;

    memset(&vgmctx.ym2413_user_patch, 0, 8);

    {
        int written_bytes = opl3_init(&vgmctx, FMCHIP_YMF262,&cmd_opts);
        pre_loop_output_bytes += written_bytes;
        opll_set_program_args(argc, argv);
        opll_init(&vgmctx, &cmd_opts);
        opll2opl3_init_scheduler(&vgmctx, &cmd_opts);
    }

    long read_done_byte = data_start; 
    while (read_done_byte < filesize) {
        uint32_t current_addr = read_done_byte; // read_done_byteはdata_startから始まっていればファイル先頭からの位置
        update_is_adding_bytes(&vgmctx, orig_loop_offset, current_addr);
        update_loop_start_in_buffer(read_done_byte, orig_loop_address, &vgmctx, &loop_start_in_buffer);

        vgmctx.cmd_type = VGMCommandType_Unkown; 
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
            int written_bytes = 0;
            // Updates the stats
            vgmctx.status.stats.ym2413_write_count++;
            vgmctx.cmd_type = VGMCommandType_RegWrite;

            if (read_done_byte + 2 >= filesize) {
                fprintf(stderr, "Truncated YM2413 command.\n");
                break;
            }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            
            if (cmd_opts.is_keep_source_vgm) {
                // Inject Original Command
                written_bytes += vgm_append_byte(&vgmctx.buffer, cmd);
                written_bytes += vgm_append_byte(&vgmctx.buffer, reg);
                written_bytes += vgm_append_byte(&vgmctx.buffer, val);
            }
            
            #define NEW_VGM_OPLL2OPL3
            #ifndef NEW_VGM_OPLL2OPL3
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
                    additional_bytes += opl3_init(&vgmctx.buffer, &vgmctx.status, &state, FMCHIP_YM2413,&cmd_opts);
                    state.opl3_mode_initialized = true;
                }
                written_bytes += opll_write_register(&vgmctx.buffer, &vgmctx, &state,
                                    reg, val, wait_samples, &cmd_opts);
            } else {
                written_bytes += forward_write(&vgmctx.buffer, 0, reg, val);
                if (wait_samples) {
                    written_bytes += vgm_wait_samples(&vgmctx.buffer, &vgmctx, &vgmctx.status, wait_samples);
                }
            }
            #else
             if (chip_flags.convert_ym2413) {
                if (!vgmctx.opl3_state.opl3_mode_initialized) {
                    if (cmd_opts.debug.verbose) printf("Initializing OPL3 mode for YM2413...\n");
                    written_bytes += opl3_init(&vgmctx, FMCHIP_YM2413,&cmd_opts);
                    vgmctx.opl3_state.opl3_mode_initialized = true;
                }
                int wait_samples = 0;
                //fprintf(stderr, "[MAIN] call opll2opl3_command_handler: cmd=0x%02X type=%d reg=0x%02X val=0x%02X wait=%d\n", cmd, vgmctx.cmd_type, reg, val, wait_samples);
                written_bytes += opll2opl3_command_handler(&vgmctx, reg, val, wait_samples, &cmd_opts);
            }
            #endif
            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }

        /* YM3812 */
        if (cmd == 0x5A) {
            int written_bytes = 0;
            // Updates the stats
            vgmctx.status.stats.ym3812_write_count++;
            vgmctx.cmd_type = VGMCommandType_RegWrite;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc YM3812\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_ym3812) {
                if (!vgmctx.opl3_state.opl3_mode_initialized) {
                    written_bytes += opl3_init(&vgmctx, FMCHIP_YM3812,&cmd_opts);
                    vgmctx.opl3_state.opl3_mode_initialized = true;
                }
                written_bytes += duplicate_write_opl3(&vgmctx, reg, val, &cmd_opts);
            } else {
                written_bytes += forward_write(&vgmctx, 0, reg, val);
            }

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }

        /* YM3526 */
        if (cmd == 0x5B) {
            int written_bytes = 0;
            // Updates the stats
            vgmctx.status.stats.ym3526_write_count++;
            vgmctx.cmd_type = VGMCommandType_RegWrite;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc YM3526\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_ym3526) {
                if (!vgmctx.opl3_state.opl3_mode_initialized) {
                    written_bytes += opl3_init(&vgmctx, FMCHIP_YM3526,&cmd_opts);
                    vgmctx.opl3_state.opl3_mode_initialized = true;
                }
                written_bytes += duplicate_write_opl3(&vgmctx, reg, val, &cmd_opts);
            } else {
                written_bytes += forward_write(&vgmctx, 0, reg, val);
            }

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }

        /* Y8950 */
        if (cmd == 0x5C) {
            int written_bytes = 0;
            // Updates the stats
            vgmctx.status.stats.y8950_write_count++;
            vgmctx.cmd_type = VGMCommandType_RegWrite;

            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc Y8950\n"); break; }
            uint8_t reg = p_vgm_data[read_done_byte + 1];
            uint8_t val = p_vgm_data[read_done_byte + 2];
            read_done_byte += 3;
            if (chip_flags.convert_y8950) {
                if (!vgmctx.opl3_state.opl3_mode_initialized) {
                    written_bytes += opl3_init(&vgmctx, FMCHIP_Y8950,&cmd_opts);
                    vgmctx.opl3_state.opl3_mode_initialized = true;
                    if (cmd_opts.debug.test_tone) {
                        // Simple additive test tone: mod muted, carrier AR=15 etc.
                        // Port0 only
                        written_bytes += duplicate_write_opl3(&vgmctx, 0x05, 0x01, &cmd_opts); // ensure OPL3 mode
                        // Operator settings (slot 0 carrier path simplified)
                        written_bytes += duplicate_write_opl3(&vgmctx, 0x20, 0x01, &cmd_opts); // mul=1
                        written_bytes += duplicate_write_opl3(&vgmctx, 0x40, 0x00, &cmd_opts); // TL=0
                        written_bytes += duplicate_write_opl3(&vgmctx, 0x60, 0xF4, &cmd_opts); // AR=F DR=4
                        written_bytes += duplicate_write_opl3(&vgmctx, 0x80, 0x02, &cmd_opts); // SL=0 RR=2
                        written_bytes += duplicate_write_opl3(&vgmctx, 0xE0, 0x00, &cmd_opts);
                        // Set algorithm=1 (additive)
                        written_bytes += duplicate_write_opl3(&vgmctx, 0xC0, 0xC1, &cmd_opts); // FB=0 Alg=1
                        // FNUM for ~A440 (example fnum=0x15B oct=4) => LSB
                        written_bytes += duplicate_write_opl3(&vgmctx, 0xA0, 0x5B, &cmd_opts);
                        written_bytes += duplicate_write_opl3(&vgmctx, 0xB0, 0x20 | (4<<2) | 0x01, &cmd_opts); // MSB=1, block=4, keyon
                        written_bytes += vgm_wait_samples(&vgmctx,4410); // 100ms
                        written_bytes += duplicate_write_opl3(&vgmctx, 0xB0, 0x20 | (4<<2) | 0x00, &cmd_opts); // key off
                    }
                }
                written_bytes += duplicate_write_opl3(&vgmctx, reg, val, &cmd_opts);
            } else {
                written_bytes += forward_write(&vgmctx, 0, reg, val);
            }

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }

        /* Other OPN-family passthrough */
        if (cmd == 0x52 || cmd == 0x54 || cmd == 0x55 || cmd == 0x56 || cmd == 0x57) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_Wait;
            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc OPN-like\n"); break; }
            written_bytes += forward_write(&vgmctx, 0, p_vgm_data[read_done_byte + 1], p_vgm_data[read_done_byte + 2]);
            read_done_byte += 3;
            continue;
        }

        /* Wait process */
        if (cmd >= 0x70 && cmd <= 0x7F) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_Wait;
            #ifndef NEW_VGM_OPLL2OPL3

            vgm_wait_short(&vgmctx.buffer, &vgmctx.status, cmd);
            read_done_byte += 1;
            #else
            if (vgmctx.source_fmchip == FMCHIP_YM2413) {
                //  Write a short wait command (0x70-0x7F) and update status.
                int wait_samples = (cmd & 0x0F) + 1;
                uint8_t reg = 0;
                uint8_t val = 0;
                if (cmd_opts.debug.verbose) {
                    fprintf(stderr, "\n[MAIN] call opll2opl3_command_handler: cmd=0x%02X type=%d reg=0x%02X val=0x%02X wait=%d\n", cmd, vgmctx.cmd_type, reg, val, wait_samples);
                }
                written_bytes += opll2opl3_command_handler(&vgmctx, reg, val, wait_samples, &cmd_opts);

            } else {
                vgm_wait_short(&vgmctx, cmd);
            }
            read_done_byte += 1;
            #endif

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }
        if (cmd == 0x61) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_Wait;
            if (read_done_byte + 2 >= filesize) { fprintf(stderr,"Trunc wait 0x61\n"); break; }
            uint16_t ws = p_vgm_data[read_done_byte + 1] | (p_vgm_data[read_done_byte + 2] << 8);
        #ifndef NEW_VGM_OPLL2OPL3
            vgm_wait_samples(&vgmctx.buffer, &vgmctx, &vgmctx.status, ws);
        #else
            if (vgmctx.source_fmchip == FMCHIP_YM2413) {
                //  Write a short wait command (0x70-0x7F) and update status.
                int wait_samples = ws;
                uint8_t reg = 0;
                uint8_t val = 0;
                if (cmd_opts.debug.verbose) {
                    fprintf(stderr, "\n[MAIN] call opll2opl3_command_handler: cmd=0x%02X type=%d reg=0x%02X val=0x%02X wait=%d\n", cmd, vgmctx.cmd_type, reg, val, wait_samples);
                }
                written_bytes += opll2opl3_command_handler(&vgmctx, reg, val, wait_samples, &cmd_opts);

            } else {
                vgm_wait_samples(&vgmctx, ws);
            }
        #endif
            read_done_byte += 3;

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }
        if (cmd == 0x62) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_Wait;
            #ifndef NEW_VGM_OPLL2OPL3
            vgm_wait_60hz(&vgmctx.buffer, &vgmctx.status);
            #else
            if (vgmctx.source_fmchip == FMCHIP_YM2413) {
                // Write a wait 1/60s command (0x62) and update status.
                int wait_samples = 735;
                uint8_t reg = 0;
                uint8_t val = 0;
                if (cmd_opts.debug.verbose) {
                    fprintf(stderr, "\n[MAIN] call opll2opl3_command_handler: cmd=0x%02X type=%d reg=0x%02X val=0x%02X wait=%d\n", cmd, vgmctx.cmd_type, reg, val, wait_samples);
                }
                written_bytes += opll2opl3_command_handler(&vgmctx, reg, val, wait_samples, &cmd_opts);

            } else {
                written_bytes += vgm_wait_60hz(&vgmctx);
            }
            #endif
            read_done_byte += 1;

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }
        if (cmd == 0x63) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_Wait;
        #ifndef NEW_VGM_OPLL2OPL3
            written_bytes += vgm_wait_50hz(&vgmctx.buffer, &vgmctx.status);
        #else
            if (vgmctx.source_fmchip == FMCHIP_YM2413) {
                // Write a wait 1/50s command (0x63) and update status.
                int wait_samples = 882;
                uint8_t reg = 0;
                uint8_t val = 0;
                if (cmd_opts.debug.verbose) {
                    fprintf(stderr, "\n[MAIN] call opll2opl3_command_handler: cmd=0x%02X type=%d reg=0x%02X val=0x%02X wait=%d\n", cmd, vgmctx.cmd_type, reg, val, wait_samples);
                }
                written_bytes += opll2opl3_command_handler(&vgmctx, reg, val, wait_samples, &cmd_opts);

            } else {
                written_bytes += vgm_wait_50hz(&vgmctx);
            }
        #endif
            read_done_byte += 1;

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
            continue;
        }

        /* End */
        if (cmd == 0x66) {
            int written_bytes = 0;
            vgmctx.cmd_type = VGMCommandType_End;
            written_bytes += vgm_append_byte(&vgmctx.buffer, 0x66);
            read_done_byte += 1;

            if (vgmctx.status.is_adding_port1_bytes) {
                pre_loop_output_bytes += written_bytes;
            }
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
        vgmctx.cmd_type = VGMCommandType_Unkown;
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

 // Compute header and buffer sizes
    uint32_t music_data_size = (uint32_t)vgmctx.buffer.size;
    uint32_t gd3_size = (uint32_t)gd3.size;
    uint32_t header_size = (orig_header_size > VGM_HEADER_SIZE) ? orig_header_size : VGM_HEADER_SIZE;
    uint32_t new_eof_offset = music_data_size + header_size + gd3_size - 1;
    uint32_t vgm_eof_offset_field = new_eof_offset - 0x04;
    uint32_t gd3_offset_field_value = header_size + music_data_size - 0x14;
    uint32_t data_offset = header_size - 0x34;

    // Set port1_bytes according to your logic (e.g., actual port1 copy size)
    uint32_t port1_bytes = 0; // Set this appropriately for your program
    // Determine if port1 bytes should be included in loop offset (from status)
    bool is_adding_port1_bytes = vgmctx.status.is_adding_port1_bytes;

    uint8_t *p_header_buf = (uint8_t*)calloc(1, header_size);

    // Call the new build_vgm_header with the new parameters
    build_vgm_header(
        p_header_buf,
        p_vgm_data,
        vgmctx.status.total_samples,
        vgm_eof_offset_field,
        gd3_offset_field_value,
        data_offset,
        0x00000171,             // VGM version
        pre_loop_output_bytes  // additional_data_bytes
    );

    if (cmd_opts.is_keep_source_vgm) {
        cmd_opts.strip_unused_chip_clocks = false;
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

    printf("[GD3] Creator: %s\n", p_creator);
    printf("[OPL3] Converted VGM written to: %s\n", p_output_path);
    printf("[OPL3] Detune percentage (-detune <val>): %g%%\n", detune);
    printf("[OPL3] Detune limit (-detune_limit <val>): max +-%g\n", detune_limit);
    printf("[OPL3] Channel Panning Mode (-ch_panning <val>): %d\n", ch_panning);
    printf("[OPL3] Port0 Volume (-vr0 <val>): %.2f%%\n", v_ratio0 * 100);
    printf("[OPL3] Port1 Volume (-vr1 <val>): %.2f%%\n", v_ratio1 * 100);

    if (vgmctx.source_fmchip == FMCHIP_YM2413) {
        //printf("[YM2413] Debug Verbose: %s\n", debug_opts.verbose ? "ON" : "OFF");
        //printf("[YM2413] Audible Sanity: %s\n", debug_opts.audible_sanity ? "ON" : "OFF");
        //printf("[YM2413] Emergency Boost: %d \n",  emergency_boost_steps);
        //printf("[YM2413] Force retrigger each note: %s \n",  force_retrigger_each_note? "ON" : "OFF" );
        //printf("[YM2413] Carrier TL Clamp: %s (%u)\n", carrier_tl_clamp_enabled ? "ON" : "OFF", carrier_tl_clamp);
        //printf("[YM2413] Minimum gate duration [samples]:%u \n",min_gate_samples);
        //printf("[YM2413] Minimum gate :%u \n", pre_keyon_wait_samples);
        //printf("[YM2413] Min weight samples:%u \n", min_off_on_wait_samples);
        printf("[YM2413] Preset(-preset): %s\n",preset_str);

    }

    if (cmd_opts.debug.verbose) {
        printf("[OPL3] Total voices in DB: %d\n", vgmctx.opl3_state.voice_db.count);
    }

    vgm_buffer_free(&vgmctx.buffer);
    vgm_buffer_free(&gd3);
    free(p_vgm_data);
    free(p_header_buf);
    return 0;
}