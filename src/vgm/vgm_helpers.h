#ifndef VGM_HELPERS_H
#define VGM_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Enumeration for FM sound chip types supported in VGM.
 */
typedef enum {
    FMCHIP_NONE = 0,    /**< Undefined / not set */
    FMCHIP_YM2413,
    FMCHIP_YM2612,
    FMCHIP_YM2151,
    FMCHIP_YM2203,
    FMCHIP_YM2608,
    FMCHIP_YM2610,      /**< YM2610 and YM2610B (treat as same) */
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
    FMCHIP_2xYM2610,      /**< YM2610 and YM2610B (treat as same) */
    FMCHIP_2xYM3812,
    FMCHIP_2xYM3526,
    FMCHIP_2xY8950,
    FMCHIP_2xYMF262,
    FMCHIP_2xYMF278B,
    FMCHIP_2xYMF271,
    FMCHIP_2xYMZ280B,
    FMCHIP_MAX
} FMChipType;


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

/**
 * VGM status (for compatibility or future extension).
 */
/**
 * VGM status (for compatibility or future extension).
 */
typedef struct {
    uint32_t total_samples;   /**< Total samples written so far */
} VGMStatus;

/**
 * VGM header information.
 */
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
    VGMStatus     status;          /**< Status (total samples etc.) */
    VGMHeaderInfo header;          /**< VGM header (raw + parsed info) */
    VGMGD3Tag     gd3;             /**< GD3 tag (raw/parsed) */
    FMChipType    source_fmchip;   /**< The source FM chip type for conversion */
    double        source_fm_clock; /**< The source FM clock frequency */
    double        target_fm_clock; /**< The target FM clock frequency */
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

    bool has_ym2413;
    bool has_ym3812;
    bool has_ym3526;
    bool has_y8950;

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
 * @param p_buf Pointer to VGMBuffer.
 * @param p_data Pointer to data to append.
 * @param len Length of data to append.
 */
void vgm_buffer_append(VGMBuffer *p_buf, const void *p_data, size_t len);

/**
 * Release memory allocated for a VGMBuffer.
 * @param p_buf Pointer to VGMBuffer to free.
 */
void vgm_buffer_free(VGMBuffer *p_buf);

/**
 * Append a single byte to the buffer.
 * @param p_buf Pointer to VGMBuffer.
 * @param value Byte value to append.
 */
void vgm_append_byte(VGMBuffer *p_buf, uint8_t value);

/**
 * Write an OPL3 register command (0x5E/0x5F) to the buffer.
 * @param p_buf Pointer to VGMBuffer.
 * @param port Port index (0 for port 0, 1 for port 1).
 * @param reg Register address.
 * @param val Value to write.
 */
void forward_write(VGMBuffer *p_buf, int port, uint8_t reg, uint8_t val);

/**
 * Write a short wait command (0x70-0x7F) and update status.
 * @param p_buf Pointer to VGMBuffer.
 * @param p_vstat Pointer to VGMStatus.
 * @param cmd Command byte (0x70-0x7F).
 */
void vgm_wait_short(VGMBuffer *p_buf, VGMStatus *p_vstat, uint8_t cmd);

/**
 * Write a wait n samples command (0x61) and update status.
 * @param p_buf Pointer to VGMBuffer.
 * @param p_vstat Pointer to VGMStatus.
 * @param samples Number of samples to wait.
 */
void vgm_wait_samples(VGMBuffer *p_buf, VGMStatus *p_vstat, uint16_t samples);

/**
 * Write a wait 1/60s command (0x62) and update status.
 * @param p_buf Pointer to VGMBuffer.
 * @param p_vstat Pointer to VGMStatus.
 */
void vgm_wait_60hz(VGMBuffer *p_buf, VGMStatus *p_vstat);

/**
 * Write a wait 1/50s command (0x63) and update status.
 * @param p_buf Pointer to VGMBuffer.
 * @param p_vstat Pointer to VGMStatus.
 */
void vgm_wait_50hz(VGMBuffer *p_buf, VGMStatus *p_vstat);

// Timestamp utility functions
static inline void vgm_timestamp_init(VGMTimeStamp* ts, double sample_rate) {
    ts->current_sample = 0;
    ts->last_sample = 0;
    ts->sample_rate = sample_rate;
}
static inline void vgm_timestamp_advance(VGMTimeStamp* ts, uint32_t samples) {
    ts->last_sample = ts->current_sample;
    ts->current_sample += samples;
}
static inline double vgm_timestamp_sec(const VGMTimeStamp* ts) {
    return ts->current_sample / ts->sample_rate;
}
static inline double vgm_timestamp_last_sec(const VGMTimeStamp* ts) {
    return ts->last_sample / ts->sample_rate;
}
static inline double vgm_timestamp_delta_sec(const VGMTimeStamp* ts) {
    return (ts->current_sample - ts->last_sample) / ts->sample_rate;
}
static inline uint32_t vgm_timestamp_delta_samples(const VGMTimeStamp* ts) {
    return ts->current_sample - ts->last_sample;
}

/**
 * Wait for (cmd & 0x0F) + 1 samples (short wait) using VGMContext.
 * @param ctx Pointer to VGMContext.
 * @param cmd Command byte (0x70-0x7F).
 */
void vgm_wait_short_ctx(VGMContext *ctx, uint8_t cmd);

/**
 * Wait for a specific number of samples using VGMContext.
 * @param ctx Pointer to VGMContext.
 * @param samples Number of samples to wait.
 */
void vgm_wait_samples_ctx(VGMContext *ctx, uint16_t samples);

/**
 * Wait for 1/60 second (735 samples) using VGMContext.
 * @param ctx Pointer to VGMContext.
 */
void vgm_wait_60hz_ctx(VGMContext *ctx);

/**
 * Wait for 1/50 second (882 samples) using VGMContext.
 * @param ctx Pointer to VGMContext.
 */
void vgm_wait_50hz_ctx(VGMContext *ctx);

/**
 * Parse the VGM header for FM chip clock values and flags.
 * Returns true if parsing was successful.
 * @param vgm_data  Pointer to the start of the VGM file
 * @param filesize  Size of the VGM file
 * @param out_flags Pointer to a VGMChipClockFlags struct to fill
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