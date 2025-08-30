#include "opl3_convert.h"
#include "opl3_voice.h"
#include "opl3_event.h"
#include <stdlib.h>
#include <string.h>

// Initialize event list
void opl3_event_list_init(OPL3EventList *pList) {
    pList->count = 0;
    pList->capacity = 64;
    pList->events = (OPL3Event*)calloc(pList->capacity, sizeof(OPL3Event));
}

// Free event list
void opl3_event_list_free(OPL3EventList *pList) {
    if (pList->events) free(pList->events);
    pList->events = NULL;
    pList->count = pList->capacity = 0;
}

// Add event to the list (auto-expand)
void opl3_event_list_add(OPL3EventList *pList, const OPL3Event *pEvent) {
    if (pList->count >= pList->capacity) {
        pList->capacity *= 2;
        pList->events = (OPL3Event*)realloc(pList->events, pList->capacity * sizeof(OPL3Event));
    }
    pList->events[pList->count++] = *pEvent;
}