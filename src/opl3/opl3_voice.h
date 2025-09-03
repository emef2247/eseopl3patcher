#ifndef OPL3_VOICE_H
#define OPL3_VOICE_H

#include <stdint.h>
#include "../vgm/vgm_helpers.h" // for FMChipType

#define OPL3_MODE_2OP 0
#define OPL3_MODE_4OP 1
#define OPL3_DB_INITIAL_SIZE 16

/**
 * Forward declaration of OPL3State (actual definition is in opl3_convert.h)
 */
typedef struct OPL3State OPL3State;

/**
 * OPL3 operator parameter set (includes TL value).
 */
typedef struct {
    uint8_t am;    /**< Amplitude modulation enable */
    uint8_t vib;   /**< Vibrato enable */
    uint8_t egt;   /**< Envelope generator type */
    uint8_t ksr;   /**< Key scale rate */
    uint8_t mult;  /**< Frequency multiplier */
    uint8_t ksl;   /**< Key scale level */
    uint8_t tl;    /**< Total level (0-63) */
    uint8_t ar;    /**< Attack rate */
    uint8_t dr;    /**< Decay rate */
    uint8_t sl;    /**< Sustain level */
    uint8_t rr;    /**< Release rate */
    uint8_t ws;    /**< Waveform select */
} OPL3OperatorParam;

/**
 * OPL3 voice structure for 2op or 4op voice.
 * is_4op == 0: 2op voice, is_4op == 1: 4op voice
 */
typedef struct {
    int is_4op;                /**< 0: 2op, 1: 4op (YMF262 spec) */
    OPL3OperatorParam op[4];   /**< [0..1] for 2op, [0..3] for 4op */
    uint8_t fb[2];             /**< Feedback for each 2op pair */
    uint8_t cnt[2];            /**< Connection type for each 2op pair */
    FMChipType source_fmchip;  /**< Source FM chip type for this voice (e.g. YM2413/YM3812) */
    int voice_no;              /**< Unique voice ID assigned by the voice database */
} OPL3VoiceParam;

/**
 * Dynamic array for voice database.
 */
typedef struct {
    OPL3VoiceParam *p_voices;  /**< Array of voice parameter sets */
    int count;                 /**< Number of registered voices */
    int capacity;              /**< Allocated capacity */
} OPL3VoiceDB;

/**
 * Initialize the OPL3VoiceDB dynamic array.
 * @param p_db Pointer to OPL3VoiceDB to initialize.
 */
void opl3_voice_db_init(OPL3VoiceDB *p_db);

/**
 * Free memory used by the OPL3VoiceDB.
 * @param p_db Pointer to OPL3VoiceDB to free.
 */
void opl3_voice_db_free(OPL3VoiceDB *p_db);

/**
 * Find a matching voice in the DB or add a new one.
 * If found, returns its voice_no (unique ID).
 * If not found, adds the voice, assigns a new voice_no, and returns it.
 * @param p_db Pointer to OPL3VoiceDB.
 * @param p_vp Pointer to OPL3VoiceParam to find or add.
 * @return voice_no (unique ID) in database.
 */
int opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, OPL3VoiceParam *p_vp);

/**
 * Compare two OPL3VoiceParam structures for equality, ignoring TL (Total Level).
 * Returns 1 if parameters match (except TL), 0 otherwise.
 * TL is explicitly ignored for matching purposes.
 */
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

/**
 * Check if given channel is in 4op mode (returns 1 for 4op, 0 for 2op).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @return 1 for 4op, 0 for 2op.
 */
int is_4op_channel(const OPL3State *p_state, int ch);

/**
 * Get the OPL3 channel mode (OPL3_MODE_2OP or OPL3_MODE_4OP).
 * @param p_state Pointer to OPL3State.
 * @param ch Channel index.
 * @return OPL3_MODE_2OP or OPL3_MODE_4OP.
 */
int get_opl3_channel_mode(const OPL3State *p_state, int ch);

/**
 * Extracts all operator and channel parameters for a single OPL3 channel
 * from the register mirror and fills OPL3VoiceParam.
 * Scans the register state to determine channel and operator parameters.
 *
 * @param p_state Pointer to OPL3State containing register mirror
 * @param p_out Pointer to output OPL3VoiceParam structure
 */
void extract_voice_param(const OPL3State *p_state, OPL3VoiceParam *p_out);
#endif // OPL3_VOICE_H