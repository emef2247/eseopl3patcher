#ifndef OPL3_EVENT_H
#define OPL3_EVENT_H

#include <stdint.h>

// OPL3 event type enumeration
typedef enum {
    OPL3_EVENT_NONE = 0,
    OPL3_EVENT_KEYON,
    OPL3_EVENT_KEYOFF,
    OPL3_EVENT_NOTE,      // Generic note event (optional, for MML etc.)
    OPL3_EVENT_PITCH,     // F-Number, Block, etc.
    OPL3_EVENT_VOICECHANGE,
    OPL3_EVENT_CONTROL,   // Expression, pitch bend, modulation, etc.
    OPL3_EVENT_WAIT,      // Wait/delta time
    OPL3_EVENT_SYSTEM,    // System/meta event (for tempo, etc.)
} OPL3EventType;

// OPL3 event structure
typedef struct {
    OPL3EventType type;      // Event type
    uint32_t timestamp;      // Absolute sample count or tick
    uint8_t channel;         // Logical OPL3 channel (0..17)
    uint8_t note;            // MIDI note number (0..127), if applicable
    uint8_t velocity;        // Velocity (0..127), if applicable (KeyOn/KeyOff)
    int voice_id;            // Voice ID (refer to OPL3VoiceDB), -1 if N/A
    uint16_t fnum;           // FM F-Number (pitch), for pitch events
    uint8_t block;           // FM Block (octave), for pitch events
    uint8_t control_type;    // Control type (expression, modulation, etc.), OPL3/MIDI
    int16_t control_value;   // Control value (signed), for control events
    uint32_t wait_value;     // Number of samples/ticks to wait (for WAIT type)
    uint32_t meta_value;     // System/meta (e.g., tempo, time signature)
    // You can add more fields as needed for advanced export (e.g. detune, pan, etc.)
} OPL3Event;

// Dynamic event list for easy appending (expandable array)
typedef struct {
    OPL3Event *p_events;
    int count;
    int capacity;
} OPL3EventList;

// Utility functions
void opl3_event_list_init(OPL3EventList *p_list);
void opl3_event_list_free(OPL3EventList *p_list);
void opl3_event_list_add(OPL3EventList *p_list, const OPL3Event *p_event);

// KeyOn/KeyOff helper for OPL3 channel state
typedef struct {
    uint8_t keyon;     // Current KeyOn bit (0 or 1)
    uint8_t prev_keyon;// Previous KeyOn bit (0 or 1)
} OPL3KeyOnStatus;

#endif // OPL3_EVENT_H