#ifndef VGM_HELPERS_H
#define VGM_HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

// Enumeration for FM sound chip types supported in VGM
typedef enum {
    FMCHIP_NONE = 0,    // Undefined / not set
    FMCHIP_YM2413,
    FMCHIP_YM2612,
    FMCHIP_YM2151,
    FMCHIP_YM2203,
    FMCHIP_YM2608,
    FMCHIP_YM2610,      // YM2610 and YM2610B (treat as same)
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
    FMCHIP_2xYM2610,      // YM2610 and YM2610B (treat as same)
    FMCHIP_2xYM3812,
    FMCHIP_2xYM3526,
    FMCHIP_2xY8950,
    FMCHIP_2xYMF262,
    FMCHIP_2xYMF278B,
    FMCHIP_2xYMF271,
    FMCHIP_2xYMZ280B,
    FMCHIP_MAX
} FMChipType;

// Dynamic buffer for VGM data stream
typedef struct {
    uint8_t *data;     // Pointer to the buffer data
    size_t size;       // Current valid byte count
    size_t capacity;   // Allocated capacity in bytes
} VGMBuffer;

// VGM sample-based timestamp management
typedef struct {
    uint32_t current_sample;  // Current VGM sample count (absolute)
    uint32_t last_sample;     // Previous update sample count (for delta calculation)
    double sample_rate;       // VGM sample rate (typically 44100.0)
} VGMTimeStamp;

// VGM status (for compatibility or future extension)
typedef struct {
    uint32_t total_samples;   // Total samples written so far
} VGMStatus;

// VGM header information
typedef struct {
    uint8_t raw[0x100];    // Raw VGM header data (0x100 bytes)
    uint32_t version;      // Parsed VGM version (0x08)
    uint32_t data_offset;  // Data offset (0x34)
    uint32_t gd3_offset;   // GD3 offset (0x14)
    uint32_t loop_offset;  // Loop offset (0x1C)
    uint32_t loop_samples; // Loop samples (0x20)
    uint32_t total_samples;// Total samples (0x18)
    uint32_t eof_offset;   // EOF offset (0x04)
    // Additional header fields can be added as needed
} VGMHeaderInfo;

// GD3 tag (Unicode text, for track info, can be parsed further if needed)
typedef struct {
    uint8_t *data;       // GD3 raw data block (allocated, can be NULL)
    size_t   size;       // Size of GD3 data block in bytes
    // Optionally: parsed title/artist/etc. fields can be added
} VGMGD3Tag;

// VGM context: super-structure to manage all VGM stream state and metadata
typedef struct {
    VGMBuffer buffer;          // Data buffer for the VGM stream
    VGMTimeStamp timestamp;    // Sample clock/timestamp
    VGMStatus status;          // Status (total samples etc.)
    VGMHeaderInfo header;      // VGM header (raw + parsed info)
    VGMGD3Tag    gd3;          // GD3 tag (raw/parsed)
    // The source FM chip type for conversion (i.e., the type of FM chip in the input VGM)
    FMChipType source_fmchip;
} VGMContext;

// Buffer utility functions
void vgm_buffer_init(VGMBuffer *p_buf);
void vgm_buffer_append(VGMBuffer *p_buf, const void *p_data, size_t len);
void vgm_buffer_free(VGMBuffer *p_buf);
void vgm_append_byte(VGMBuffer *p_buf, uint8_t value);

// VGM/OPL3 register/command helpers
void forward_write(VGMBuffer *p_buf, int port, uint8_t reg, uint8_t val);

// Wait commands for legacy VGMBuffer/VGMStatus
void vgm_wait_short(VGMBuffer *p_buf, VGMStatus *p_vstat, uint8_t cmd);
void vgm_wait_samples(VGMBuffer *p_buf, VGMStatus *p_vstat, uint16_t samples);
void vgm_wait_60hz(VGMBuffer *p_buf, VGMStatus *p_vstat);
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

// Wait commands operating on VGMContext
// Wait for (cmd & 0x0F) + 1 samples (short wait)
void vgm_wait_short_ctx(VGMContext *ctx, uint8_t cmd);

// Wait for a specific number of samples
void vgm_wait_samples_ctx(VGMContext *ctx, uint16_t samples);

// Wait for 1/60 second (735 samples)
void vgm_wait_60hz_ctx(VGMContext *ctx);

// Wait for 1/50 second (882 samples)
void vgm_wait_50hz_ctx(VGMContext *ctx);

#ifdef __cplusplus
}
#endif

#endif // VGM_HELPERS_H