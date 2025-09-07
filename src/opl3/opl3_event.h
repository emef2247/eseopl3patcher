#ifndef OPL3_EVENT_H
#define OPL3_EVENT_H

#include <stdint.h>
#include "../vgm/vgm_helpers.h" // For FMChipType

typedef enum {
    OPL3_EVENT_NONE = 0,
    OPL3_EVENT_KEYON,
    OPL3_EVENT_KEYOFF,
    OPL3_EVENT_NOTE,
    OPL3_EVENT_PITCH,
    OPL3_EVENT_VOICECHANGE,
    OPL3_EVENT_CONTROL,
    OPL3_EVENT_WAIT,
    OPL3_EVENT_SYSTEM
} OPL3EventType;

typedef struct {
    OPL3EventType type;
    uint32_t timestamp;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    int voice_id;
    uint16_t fnum;
    uint8_t block;
    uint8_t control_type;
    int16_t control_value;
    uint32_t wait_value;
    uint32_t meta_value;
    FMChipType source_fmchip;
    int patch_no;
} OPL3Event;

typedef struct {
    OPL3Event *p_events;
    int count;
    int capacity;
} OPL3EventList;

void opl3_event_list_init(OPL3EventList *p_list);
void opl3_event_list_free(OPL3EventList *p_list);
void opl3_event_list_add(OPL3EventList *p_list, const OPL3Event *p_event);

typedef struct {
    uint8_t keyon;      // Current KeyOn bit (0 or 1)
    uint8_t prev_keyon; // Previous KeyOn bit (0 or 1)
} OPL3KeyOnStatus;

#endif // OPL3_EVENT_H