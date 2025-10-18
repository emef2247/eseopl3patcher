#ifndef VGM_HELPERS_H
#define VGM_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // for size_t
#include "../opl3/opl3_state.h"
#include "../opll/opll_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- FMChipType / CommandOptions guard to avoid redefinition ---- */
#ifndef ESEOPL3PATCHER_FMCHIPTYPE_DEFINED
#define ESEOPL3PATCHER_FMCHIPTYPE_DEFINED
typedef enum {
    FMCHIP_NONE = 0,
    FMCHIP_YM2413,
    FMCHIP_YM2612,
    FMCHIP_YM2151,
    FMCHIP_YM2203,
    FMCHIP_YM2608,
    FMCHIP_YM2610,
    FMCHIP_YM3812,
    FMCHIP_YM3526,
    FMCHIP_Y8950,
    FMCHIP_YMF262,
    FMCHIP_YMF278B,
    FMCHIP_YMF271,
    FMCHIP_YMZ280B,
    FMCHIP_2xYM2413,
    FMCHIP_2xYM2612,
    FMCHIP_2xYM2151,
    FMCHIP_2xYM2203,
    FMCHIP_2xYM2608,
    FMCHIP_2xYM2610,
    FMCHIP_2xYM3812,
    FMCHIP_2xYM3526,
    FMCHIP_2xY8950,
    FMCHIP_2xYMF262,
    FMCHIP_2xYMF278B,
    FMCHIP_2xYMF271,
    FMCHIP_2xYMZ280B,
    FMCHIP_MAX
} FMChipType;

typedef enum {
    VGMCommandType_RegWrite,
    VGMCommandType_Wait,
    VGMCommandType_End,
    VGMCommandType_Unkown,
} VGMCommandType;

typedef enum {
    FM_MappingStyle_classic,
    FM_MappingStyle_modern,
} FM_MappingStyle;

/** Global debug / diagnostic options */
typedef struct {
    bool strip_non_opl;       /* Remove AY8910/K051649 etc. from output */
    bool test_tone;           /* Inject a simple test tone sequence */
    bool fast_attack;         /* Force fast envelope (AR=15 etc.) */
    bool no_post_keyon_tl;    /* Suppress TL changes right after KeyOn */
    bool single_port;         /* Emit only port0 writes (suppress port1) */
    bool audible_sanity;   /* “鳴らすため” の安全調整 */
    bool verbose;
} DebugOpts;

typedef struct {
    double detune;
    int    opl3_keyon_wait;
    int    ch_panning;
    double v_ratio0;
    double v_ratio1;
    int      carrier_tl_clamp_enabled;
    uint8_t  carrier_tl_clamp;      /* 0..63: clamp (小さいほど音量大) */
    int      emergency_boost_steps; /* >0 なら最終キャリアTLを更に減算 */
    bool force_retrigger_each_note;
    // 追加: audible-sanity ランタイム値（0 なら未指定でビルド時デフォルトを使用）
    uint16_t min_gate_samples;          // OPLL_MIN_GATE_SAMPLES 相当
    uint16_t pre_keyon_wait_samples;    // OPLL_PRE_KEYON_WAIT_SAMPLES 相当
    uint16_t min_off_on_wait_samples;   // OPLL_MIN_OFF_TO_ON_WAIT_SAMPLES 相当
    // 追加: ヘッダ整形
    bool strip_unused_chip_clocks;      // 未使用チップのクロックを0化
    uint32_t override_opl3_clock;       // 0 以外なら OPL3 clock を上書き
    double detune_limit; // detuneの絶対値
    FM_MappingStyle fm_mapping_style;
    bool is_port1_enabled;
    bool is_voice_zero_clear;
    bool is_a0_b0_aligned;
    DebugOpts debug;
} CommandOptions;
#endif /* ESEOPL3PATCHER_FMCHIPTYPE_DEFINED */

/**
 * Dynamic buffer for VGM data stream.
 */
typedef struct {
    uint8_t *data;     /**< Pointer to the buffer data */
    size_t size;       /**< Current valid byte count */
    size_t capacity;   /**< Allocated capacity in bytes */
} VGMBuffer;

/**
 * VGM sample-based timestamp management.
 */
/**
 * VGM sample-based timestamp management.
 */
typedef struct {
    uint32_t current_sample;  /**< Current VGM sample count (absolute) */
    uint32_t last_sample;     /**< Previous update sample count (for delta calculation) */
    double sample_rate;       /**< VGM sample rate (typically 44100.0) */
} VGMTimeStamp;

typedef struct {
    uint32_t ym2413_write_count;
    uint32_t ym3812_write_count;
    uint32_t ym3526_write_count;
    uint32_t y8950_write_count;
    uint32_t ay8910_write_count;
    uint32_t sn76489_write_count;
} VGMStats;

/**
 * VGM status (for compatibility or future extension).
 */
/**
 * VGM status (for compatibility or future extension).
 */
typedef struct {
    uint32_t  total_samples;   /**< Total samples written so far */
    VGMStats  stats;
    bool is_adding_port1_bytes; // Flag to indicate whether to add Port1 bytes in the loop offset calculation
} VGMStatus;

/**
 * VGM header information.
 */
typedef struct {
    uint8_t raw[0x100];    /**< Raw VGM header data (0x100 bytes) */
    uint32_t version;      /**< Parsed VGM version (0x08) */
    uint32_t data_offset;  /**< Data offset (0x34) */
    uint32_t gd3_offset;   /**< GD3 offset (0x14) */
    uint32_t loop_offset;  /**< Loop offset (0x1C) */
    uint32_t loop_samples; /**< Loop samples (0x20) */
    uint32_t total_samples;/**< Total samples (0x18) */
    uint32_t eof_offset;   /**< EOF offset (0x04) */
    // Additional header fields can be added as needed
} VGMHeaderInfo;

/**
 * GD3 tag (Unicode text, for track info, can be parsed further if needed).
 */
/**
 * GD3 tag (Unicode text, for track info, can be parsed further if needed).
 */
typedef struct {
    uint8_t *data;       /**< GD3 raw data block (allocated, can be NULL) */
    size_t   size;       /**< Size of GD3 data block in bytes */
    // Optionally: parsed title/artist/etc. fields can be added
} VGMGD3Tag;


/**
 * VGMContext
 * Super-structure to manage all VGM stream state and metadata.
 * Fields:
 *   - buffer: Dynamic data buffer for the VGM stream.
 *   - timestamp: Sample clock/timestamp management.
 *   - status: Status information (e.g., total samples written).
 *   - header: VGM header information (raw and parsed).
 *   - gd3: GD3 tag data (raw and/or parsed).
 *   - source_fmchip: The source FM chip type for conversion.
 */
typedef struct {
    VGMBuffer buffer;              /**< Data buffer for the VGM stream */
    VGMTimeStamp timestamp;        /**< Sample clock/timestamp */
    VGMCommandType cmd_type;
    VGMStatus     status;          /**< Status (total samples etc.) */
    VGMHeaderInfo header;          /**< VGM header (raw + parsed info) */
    VGMGD3Tag     gd3;             /**< GD3 tag (raw/parsed) */
    FMChipType    source_fmchip;   /**< The source FM chip type for conversion */
    double        source_fm_clock; /**< The source FM clock frequency */
    double        target_fm_clock; /**< The target FM clock frequency */
    OPL3State     opl3_state;
    OPLLState     opll_state;
    uint8_t       ym2413_user_patch[8]; // YM2413ユーザーパッチ用（0x00〜0x07）
} VGMContext;

/**
 * FM chip clocks and flags structure for VGM header analysis.
 * This allows checking which chips are present and flagging them.
 */
typedef struct {
    uint32_t ym2413_clock;
    uint32_t ym3812_clock;
    uint32_t ym3526_clock;
    uint32_t y8950_clock;
    uint32_t sn76489_clock;
    uint32_t ay8910_clock;

    bool has_ym2413;
    bool has_ym3812;
    bool has_ym3526;
    bool has_y8950;
    bool has_sn76489;
    bool has_ay8910;

    // Mark which chips are selected for conversion
    bool convert_ym2413;
    bool convert_ym3812;
    bool convert_ym3526;
    bool convert_y8950;

    bool opl_group_autodetect; // true if "auto" mode, false if explicit
    int  opl_group_first_cmd;  // 0x5A=3812, 0x5B=3526, 0x5C=Y8950
} VGMChipClockFlags;

// --- Function documentation comments ---
/**
 * Initialize a VGMBuffer structure.
 * @param p_buf Pointer to VGMBuffer to initialize.
 */
void vgm_buffer_init(VGMBuffer *p_buf);

/**
 * Append arbitrary bytes to a dynamic VGMBuffer.
 */
void vgm_buffer_append(VGMBuffer *p_buf, const void *p_data, size_t len);

/**
 * Release memory allocated for a VGMBuffer.
 */
void vgm_buffer_free(VGMBuffer *p_buf);

/**
 * Append a single byte to the buffer.
 */
void vgm_append_byte(VGMBuffer *p_buf, uint8_t value);

/**
 * Write an OPL3 register command (0x5E/0x5F) to the buffer.
 */
void forward_write(VGMContext *p_vgmctx, int port, uint8_t reg, uint8_t val);

/**
 * Write a short wait command (0x70-0x7F) and update status.
 */
void vgm_wait_short(VGMContext *p_vgmctx, uint8_t cmd);

/**
 * Write a wait n samples command (0x61) and update status.
 */
void vgm_wait_samples(VGMContext *p_vgmctx, uint16_t samples);

/**
 * Write a wait 1/60s command (0x62) and update status.
 */
void vgm_wait_60hz(VGMContext *p_vgmctx);

/**
 * Write a wait 1/50s command (0x63) and update status.
 */
void vgm_wait_50hz(VGMContext *p_vgmctx);

/**
 * Parse the VGM header for FM chip clock values and flags.
 * Returns true if parsing was successful.
 */
bool vgm_parse_chip_clocks(const uint8_t *vgm_data, long filesize, VGMChipClockFlags *out_flags);

/**
 * Returns the name of the FM chip selected for conversion in chip_flags.
 * Only one chip should be selected for conversion; if multiple are selected, returns the first found.
 * If none is selected, returns "UNKNOWN".
 */
const char* get_converted_opl_chip_name(const VGMChipClockFlags* chip_flags);

#ifdef __cplusplus
}
#endif

#endif // VGM_HELPERS_H