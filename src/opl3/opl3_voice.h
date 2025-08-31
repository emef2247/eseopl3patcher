#ifndef OPL3_VOICE_H
#define OPL3_VOICE_H

#include <stdint.h>
#include "vgm_helpers.h" // for FMChipType

#define OPL3_MODE_2OP 0
#define OPL3_MODE_4OP 1

// Forward declaration of OPL3State (actual definition is in opl3_convert.h)
typedef struct OPL3State OPL3State;

// OPL3 operator parameter set (except TL)
typedef struct {
    uint8_t am;    // Amplitude modulation enable
    uint8_t vib;   // Vibrato enable
    uint8_t egt;   // Envelope generator type
    uint8_t ksr;   // Key scale rate
    uint8_t mult;  // Frequency multiplier
    uint8_t ksl;   // Key scale level
    uint8_t ar;    // Attack rate
    uint8_t dr;    // Decay rate
    uint8_t sl;    // Sustain level
    uint8_t rr;    // Release rate
    uint8_t ws;    // Waveform select
} OPL3OperatorParam;

// 2op/4op voice structure
typedef struct {
    int is_4op;                // 1: 2op, 2: 4op (YMF262 spec)
    OPL3OperatorParam op[4];   // [0..1] for 2op, [0..3] for 4op
    uint8_t fb[2];             // Feedback for each 2op pair
    uint8_t cnt[2];            // Connection type for each 2op pair
    FMChipType source_fmchip;  // Source FM chip type for this voice (e.g. YM2413/YM3812)
    int patch_no;              // Patch/instrument number from the source FM chip (if applicable)
} OPL3VoiceParam;

// Dynamic array for voice database
typedef struct {
    OPL3VoiceParam *p_voices;  // Array of voice parameter sets
    int count;                 // Number of registered voices
    int capacity;              // Allocated capacity
} OPL3VoiceDB;

// Voice DB utility functions
void opl3_voice_db_init(OPL3VoiceDB *p_db);
void opl3_voice_db_free(OPL3VoiceDB *p_db);
int  opl3_voice_db_find_or_add(OPL3VoiceDB *p_db, const OPL3VoiceParam *p_vp);

// Compare two OPL3VoiceParam (compare only meaningful fields)
int opl3_voice_param_cmp(const OPL3VoiceParam *a, const OPL3VoiceParam *b);

// Check if given channel is in 4op mode (returns 2 for 4op, 1 for 2op)
int is_4op_channel(const OPL3State *p_state, int ch);

// Get the OPL3 channel mode (OPL3_MODE_2OP or OPL3_MODE_4OP)
int get_opl3_channel_mode(const OPL3State *p_state, int ch);

// Extract voice parameters for the given logical channel (0..17)
void extract_voice_param(const OPL3State *p_state, int ch, OPL3VoiceParam *p_out);

// Returns the FM chip name string for the given FMChipType enum value
const char* fmchip_type_name(FMChipType type);

// Detect which FM chip is present in the VGM header and used in the input file
FMChipType detect_fmchip_from_header(const unsigned char *p_vgm_data, long filesize);

#endif // OPL3_VOICE_H