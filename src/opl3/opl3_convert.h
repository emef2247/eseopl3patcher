#ifndef OPL3_CONVERT_H
#define OPL3_CONVERT_H

#include <stdint.h>
#include <stdbool.h>
#include "../vgm/vgm_helpers.h"
#include "../vgm/vgm_header.h"
#include "opl3_voice.h"
#include "opl3_event.h"
#include "../opll/opll_to_opl3_wrapper.h"

#define OPL3_CLOCK 14318182
#define OPL3_NUM_CHANNELS 18
#define OPL3_REGISTER_SIZE 0x200 // 0x100 registers per port, 2 ports
#define OPL3_KEYON_WAIT_AFTER_ON_DEFAULT 4410 // Wait after KeyOn=1 (in samples, can be tuned)

/**
 * OPL3State structure holds all OPL3 emulation state, including voice DB and event DB.
 */
typedef struct OPL3State {
    uint8_t reg[OPL3_REGISTER_SIZE];        // Full OPL3 register mirror (0x000-0x1FF)
    uint8_t reg_stamp[OPL3_REGISTER_SIZE];  // Register value before last write
    bool rhythm_mode;                       // Rhythm mode flag, updated on BD writes
    bool opl3_mode_initialized;             // OPL3 mode flag, updated on 0x105 writes
    OPL3VoiceDB voice_db;                   // Voice database
    OPL3EventList event_list;               // Event database (KeyOn/KeyOff etc.)
    OPL3KeyOnStatus keyon_status[OPL3_NUM_CHANNELS]; // Per-channel KeyOn state
    uint32_t timestamp;                     // Current timestamp in samples or ticks
    FMChipType source_fmchip;               // Source FM chip type for this conversion session
    // More fields can be added here
} OPL3State;

typedef enum {
    OPL3_REGTYPE_LSI_TEST,
    OPL3_REGTYPE_TIMER1,
    OPL3_REGTYPE_TIMER2,
    OPL3_REGTYPE_RST_MT_ST,
    OPL3_REGTYPE_MODE,
    OPL3_REGTYPE_NTS,
    OPL3_REGTYPE_FREQ_LSB,
    OPL3_REGTYPE_FREQ_MSB,
    OPL3_REGTYPE_CH_CTRL,
    OPL3_REGTYPE_OP_TL,
    OPL3_REGTYPE_RHYTHM_BD,
    OPL3_REGTYPE_WB,
    OPL3_REGTYPE_OTHER,
} opl3_regtype_t;

/**
 * Conversion context for register write (not OPL3-specific, for conversion logic)
 */
typedef struct {
    VGMBuffer *p_music_data;
    VGMStatus *p_vstat;
    OPL3State *p_state;
    uint8_t reg;
    uint8_t val;
    double detune;
    int opl3_keyon_wait;
    int ch_panning;
    double v_ratio0;
    double v_ratio1;
} opl3_convert_ctx_t;

/**
 * Frame buffering structure for batch YM2413→OPL3 conversion.
 */
typedef struct YM2413FrameBuffer {
    uint8_t fnum_lsb[9];
    uint8_t keyon_msb[9];
    uint8_t instrument[9];
    uint8_t volume[9];
    bool instrument_changed[9];
    bool volume_changed[9];
} YM2413FrameBuffer;


/**
 * Get the FM source chip clock frequency (in Hz).
 * @return The source clock frequency in Hz.
 */
double get_fm_source_clock(void);

/**
 * Set the FM source chip clock frequency (in Hz).
 * @param hz The clock frequency in Hz.
 */
void set_fm_source_clock(double hz);

/**
 * Get the FM target chip clock frequency (in Hz).
 * @return The target clock frequency in Hz.
 */
double get_fm_target_clock(void);

/**
 * Set the FM target chip clock frequency (in Hz).
 * @param hz The clock frequency in Hz.
 */
void set_fm_target_clock(double hz);

/**
 * Calculate OPL3 output frequency from block and FNUM.
 * @param clock OPL3/target chip clock (Hz)
 * @param block Block value (0-7)
 * @param fnum FNUM value (0-1023)
 * @return Calculated frequency in Hz
 */
double calc_opl3_frequency (double clock, unsigned char block, unsigned short fnum);

/**
 * Calculate OPL3 FNUM and block for a target frequency.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] out_block Pointer to output block value
 * @param[out] out_fnum Pointer to output FNUM value
 */
void opl3_calc_fnum_block_from_freq(double freq, double clock, unsigned char *out_block, unsigned short *out_fnum);

/**   
 * Calculate OPL3 FNUM and block for a target frequency using ldexp for better precision.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] out_block Pointer to output block value
 * @param[out] out_fnum Pointer to output FNUM value
 * @param[out] out_err Pointer to output frequency error (in Hz)
 */
void opl3_calc_fnum_block_from_freq_ldexp(double freq, double clock,unsigned char *out_block,unsigned short *out_fnum, double *out_err);

/**
 * Find the best OPL3 FNUM and block for a target frequency using a weighted approach.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] best_block Pointer to output best block value
 * @param[out] best_fnum Pointer to output best FNUM value
 * @param[out] best_err Pointer to output best frequency error (in Hz)
 * @param pref_block Preferred block value (for weighting)
 * @param mult_weight Multiplier weight (for weighting)
 */
void opl3_find_fnum_block_with_weight(double freq, double clock, unsigned char *best_block, unsigned short *best_fnum, double *best_err, int pref_block, double mult_weight);

/**
 * Find the best OPL3 FNUM and block for a target frequency using a machine learning approach.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] best_block Pointer to output best block value
 * @param[out] best_fnum Pointer to output best FNUM value
 * @param[out] best_err Pointer to output best frequency error (in Hz)
 * @param pref_block Preferred block value (for weighting)
 * @param mult0 Carrier multiplier (for weighting)
 * @param mult1 Modulator multiplier (for weighting) モジュレータ MULT (2opの場合は0でOK) 
 */
void opl3_find_fnum_block_with_ml(double freq, double clock,unsigned char *best_block, unsigned short *best_fnum, double *best_err,int pref_block, double mult0, double mult1);

/**
 * Find the best OPL3 FNUM and block for a target frequency using a machine learning approach with cents error.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] best_block Pointer to output best block value
 * @param[out] best_fnum Pointer to output best FNUM value
 * @param[out] best_err Pointer to output best frequency error (in cents)
 * @param pref_block Preferred block value (for weighting)
 * @param mult0 Carrier multiplier (for weighting)
 * @param mult1 Modulator multiplier (for weighting) モジュレータ MULT (2opの場合は0でOK) 
 */
void opl3_find_fnum_block_with_ml_cents(double freq, double clock,
                                        unsigned char *best_block, unsigned short *best_fnum,
                                        double *best_err,
                                        int pref_block,
                                        double mult0, double mult1);

/**
 * Calculate OPL3 FNUM and block for a target frequency.
 * @param freq Frequency in Hz
 * @param clock OPL3/target chip clock (Hz)
 * @param[out] block Pointer to int for resulting block value
 * @param[out] fnum Pointer to int for resulting FNUM value
 */
void calc_opl3_fnum_block(double freq, double clock, int* block, int* fnum);

/**
 * Write frequency and KeyOn/Off to OPL3 registers for a given channel.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param p_vstat Pointer to VGMStatus for tracking status.
 * @param p_state Pointer to OPL3State for OPL3 state.
 * @param ch OPL3 channel (0-17).
 * @param block Block value (0-7).
 * @param fnum FNUM value (0-1023).
 * @param keyon KeyOn flag (1=KeyOn, 0=KeyOff).
 * @param opts Pointer to CommandOptions for additional options.
 * @return Bytes written to port 1.
 */
int opl3_write_block_fnum_key (VGMBuffer *p_music_data,
    VGMStatus *p_vstat,
    OPL3State *p_state,
    uint8_t    ch,
    uint8_t    block,
    uint16_t   fnum,
    int       keyon,
    const CommandOptions *opts);

/**
 * Main OPL3/OPL2 register write handler (supports OPL3 chorus and register mirroring).
 * Now supports automatic frequency conversion if source and target chip/clock differ.
 * @return Bytes written to port 1.
 */
int duplicate_write_opl3(
    VGMBuffer *p_music_data, VGMStatus *p_vstat, OPL3State *p_state,
    uint8_t reg, uint8_t val, const CommandOptions *opts
   // double detune, int opl3_keyon_wait, int ch_panning, double v_ratio0, double v_ratio1
);

/**
 * Write value to OPL3 register and update internal state.
 * @param p_state Pointer to OPL3State.
 * @param p_music_data Pointer to VGMBuffer for music data.
 * @param port OPL3 port (0 or 1).
 * @param reg Register address.
 * @param value Value to write.
 */
void opl3_write_reg(OPL3State *p_state, VGMBuffer *p_music_data, int port, uint8_t reg, uint8_t value);

/**
 * Detune helper for FM channels (used for frequency detune effects).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @param regA Frequency LSB.
 * @param regB Frequency MSB.
 * @param detune Detune amount (percent).
 * @param p_outA Pointer to output detuned LSB.
 * @param p_outB Pointer to output detuned MSB.
 */
void detune_if_fm(OPL3State *p_state, int ch, uint8_t regA, uint8_t regB, double detune, uint8_t *p_outA, uint8_t *p_outB);

/**
 * Initialize OPL3 chip and voice database.
 */
void opl3_init(VGMBuffer *p_music_data, int stereo_mode, OPL3State *p_state, FMChipType source_fmchip);

/**
 * Find OPL3 voice parameters by voice ID from the voice database.
 */
OPL3VoiceParam* opl3_voice_db_find_by_voiceid(OPL3State *p_state, int voice_id);


#endif // OPL3_CONVERT_H