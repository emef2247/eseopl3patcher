#include "opl3_convert.h"
#include "opl3_event.h"
#include <stdlib.h>
#include <string.h>

// Initialize event list
void opl3_event_list_init(OPL3EventList *list) {
    list->count = 0;
    list->capacity = 64;
    list->events = (OPL3Event*)calloc(list->capacity, sizeof(OPL3Event));
}

// Free event list
void opl3_event_list_free(OPL3EventList *list) {
    if (list->events) free(list->events);
    list->events = NULL;
    list->count = list->capacity = 0;
}

// Add event to the list (auto-expand)
void opl3_event_list_add(OPL3EventList *list, const OPL3Event *event) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->events = (OPL3Event*)realloc(list->events, list->capacity * sizeof(OPL3Event));
    }
    list->events[list->count++] = *event;
}