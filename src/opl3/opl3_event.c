#include "opl3_event.h"
#include "opl3_voice.h"
#include <stdlib.h>
#include <string.h>

/**
 * Initialize an OPL3EventList structure.
 */
void opl3_event_list_init(OPL3EventList *p_list) {
    p_list->count = 0;
    p_list->capacity = 64;
    p_list->p_events = (OPL3Event*)calloc(p_list->capacity, sizeof(OPL3Event));
}

/**
 * Free all memory used by an OPL3EventList.
 */
void opl3_event_list_free(OPL3EventList *p_list) {
    if (p_list->p_events) free(p_list->p_events);
    p_list->p_events = NULL;
    p_list->count = 0;
    p_list->capacity = 0;
}

/**
 * Add a new event to the OPL3EventList, expanding capacity if needed.
 */
void opl3_event_list_add(OPL3EventList *p_list, const OPL3Event *p_event) {
    if (p_list->count >= p_list->capacity) {
        p_list->capacity *= 2;
        p_list->p_events = (OPL3Event*)realloc(p_list->p_events, p_list->capacity * sizeof(OPL3Event));
    }
    p_list->p_events[p_list->count++] = *p_event;
}