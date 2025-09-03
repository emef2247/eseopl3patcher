#ifndef OPL3_CONVERT_H
#define OPL3_CONVERT_H

#include <stdint.h>
#include <stdbool.h>
#include "../vgm/vgm_helpers.h"
#include "opl3_voice.h"
#include "opl3_event.h"

#define OPL3_NUM_CHANNELS 18
#define OPL3_REGISTER_SIZE 0x200 // 0x100 registers per port, 2 ports

/**
 * OPL3State structure holds all OPL3 emulation state, including voice DB and event DB.
 */
typedef struct OPL3State {
    uint8_t reg[OPL3_REGISTER_SIZE]; // Full OPL3 register mirror (0x000-0x1FF)
    uint8_t reg_stamp[OPL3_REGISTER_SIZE]; // Full OPL3 register mirror (0x000-0x1FF)
    bool rhythm_mode;                // Rhythm mode flag, updated on BD writes
    bool opl3_mode_initialized;      // OPL3 mode flag, updated on 0x105 writes
    OPL3VoiceDB voice_db;            // Voice database (see opl3_voice.h for definition)
    OPL3EventList event_list;        // Event database (KeyOn/KeyOff etc.)
    OPL3KeyOnStatus keyon_status[OPL3_NUM_CHANNELS]; // Per-channel KeyOn state
    uint32_t timestamp;              // Current timestamp in samples or ticks
    FMChipType source_fmchip;        // Default FM chip type for the conversion session
    // Add more fields as needed (e.g., other aggregate state)
} OPL3State;

/**
 * OPL3 register type for conversion logic (meaningful, not just A/B/C/T)
 */
typedef enum {
    OPL3_REGTYPE_LSI_TEST,   /**< 0x01 : LST TEST  (Only port 0) */
    OPL3_REGTYPE_TIMER1,     /**< 0x02 : TIMER1 (Only port 0) */
    OPL3_REGTYPE_TIMER2,     /**< 0x03 : TIMER2 (Only port 0) */
    OPL3_REGTYPE_RST_MT_ST,  /**< 0x04 : RST/MT1/MT2/ST2/ST1 (Only port 0) */
    OPL3_REGTYPE_MODE,       /**< 0x05 : NEW (Only port 1) */
    OPL3_REGTYPE_NTS,        /**< 0x08 : NTS (Only port 0) */
    OPL3_REGTYPE_FREQ_LSB,   /**< 0xA0-0xA8 : Frequency LSB (FNUM low) */
    OPL3_REGTYPE_FREQ_MSB,   /**< 0xB0-0xB8 : Frequency MSB (FNUM high, KEYON, BLOCK) */
    OPL3_REGTYPE_CH_CTRL,    /**< 0xC0-0xC8 : Channel control (Feedback, Algorithm, Panning) */
    OPL3_REGTYPE_OP_TL,      /**< 0x40-0x55 : Operator TL (Total Level) */
    OPL3_REGTYPE_RHYTHM_BD,  /**< 0xBD      : Rhythm mode/BD */
    OPL3_REGTYPE_WB,         /**< 0xE0-$F5  : Wave Select */
    OPL3_REGTYPE_OTHER       /**< All other registers */
} opl3_regtype_t;

/**
 * Conversion context for register write (not OPL3-specific, for conversion logic)
 */
typedef struct {
    VGMBuffer *p_music_data;   // Pointer to VGM dynamic buffer
    VGMStatus *p_vstat;        // Pointer to VGM status (sample count)
    OPL3State *p_state;        // Pointer to OPL3 state
    uint8_t reg;               // Actual register offset (e.g. 0xA0.., 0xB0.., etc)
    uint8_t val;               // Register value
    double detune;             // Detune value (for chorus/detune effect)
    int opl3_keyon_wait;       // KeyOn/Off wait (in samples)
    int ch_panning;            // Channel panning mode
    double v_ratio0;           // Volume ratio for port 0
    double v_ratio1;           // Volume ratio for port 1
    // Optionally: add fields for source_fmchip/patch_no if context needed
} opl3_convert_ctx_t;

/**
 * Write a value to the OPL3 register mirror and update internal state flags.
 * This always writes to the register mirror (reg[]).
 *
 * @param p_state Pointer to OPL3State structure.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param port Port index (0 or 1).
 * @param reg Register address.
 * @param value Data value to write.
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value);

/**
 * Detune helper for FM channels (used for frequency detune effects).
 *
 * @param p_state Pointer to OPL3State structure.
 * @param ch Channel index.
 * @param regA FNUM low byte.
 * @param regB FNUM high byte.
 * @param detune Detune amount.
 * @param p_outA Pointer to detuned FNUM low output.
 * @param p_outB Pointer to detuned FNUM high output.
 */
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB);

/**
 * Judge OPL3 register type based on YM2413 register address.
 * Used to map YM2413 register writes to OPL3 register conversion logic.
 *
 * @param ym2413_reg YM2413 register address.
 * @return OPL3 register type (enum value).
 */
opl3_regtype_t opl3_judge_regtype_from_ym2413(uint8_t ym2413_reg);

/**
 * Main OPL3 register write handler for YM2413 register mapping (OPLL to OPL3).
 * This uses opl3_judge_regtype_from_ym2413 to convert YM2413 reg space to OPL3 regtype.
 */
int duplicate_write_opl3_ym2413(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1);

/**
 * Judge OPL3 register type based on register offset.
 *
 * @param reg Register address.
 * @return OPL3 register type (enum value).
 */
opl3_regtype_t opl3_judge_regtype(uint8_t reg);

/**
 * Rhythm mode register handler.
 *
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_state Pointer to OPL3State structure.
 * @param val Register value.
 */
void handle_bd(VGMBuffer *p_music_data, OPL3State *p_state, uint8_t val);

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * Returns: bytes written to port 1.
 *
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus struct.
 * @param p_state Pointer to OPL3State structure.
 * @param reg Register address.
 * @param val Register value.
 * @param detune Detune value (for chorus/detune effect).
 * @param opl3_keyon_wait KeyOn/Off wait (in samples).
 * @param ch_panning Channel panning mode.
 * @param v_ratio0 Volume ratio for port 0.
 * @param v_ratio1 Volume ratio for port 1.
 * @return Number of bytes written to port 1.
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t reg, uint8_t val,
    double detune, int opl3_keyon_wait, int ch_panning,
    double v_ratio0, double v_ratio1
);

/**
 * OPL3 initialization sequence for both ports.
 * Sets FM chip type in OPL3State and initializes register mirror.
 *
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param stereo_mode Stereo mode flag.
 * @param p_state Pointer to OPL3State structure.
 * @param source_fmchip FM chip type for conversion session.
 */
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip);


#endif // OPL3_CONVERT_H