/**
 * gate_loader.c
 * 
 * Implementation of gate loading from Python-exported CSV files.
 */

#include "gate_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Maximum number of gate entries we can store */
#define MAX_GATE_ENTRIES 256

/* Gate entry structure */
typedef struct {
    int patch;
    int channel;
    uint16_t gate_samples;
} GateEntry;

/* Global state */
static GateEntry g_gates[MAX_GATE_ENTRIES];
static int g_num_gates = 0;
static int g_initialized = 0;
static uint16_t g_default_gate = 8192;

/* Helper: trim whitespace from string */
static char* trim(char *str) {
    char *end;
    
    /* Trim leading space */
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    *(end + 1) = '\0';
    return str;
}

/* Helper: parse CSV line */
static int parse_gate_line(char *line, int *patch, int *channel, uint16_t *gate) {
    char *token;
    char *saveptr;
    int field = 0;
    
    /* Parse fields */
    token = strtok_r(line, ",", &saveptr);
    while (token != NULL && field < 3) {
        token = trim(token);
        
        switch (field) {
            case 0: /* patch */
                *patch = atoi(token);
                break;
            case 1: /* channel */
                *channel = atoi(token);
                break;
            case 2: /* gate_samples */
                *gate = (uint16_t)atoi(token);
                break;
        }
        
        field++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    return (field == 3) ? 0 : -1;
}

int gate_loader_init(const char *csv_path) {
    FILE *fp;
    char line[256];
    int line_num = 0;
    int entries_loaded = 0;
    
    if (!csv_path) {
        fprintf(stderr, "[gate_loader] Error: NULL path\n");
        return -1;
    }
    
    /* Open file */
    fp = fopen(csv_path, "r");
    if (!fp) {
        fprintf(stderr, "[gate_loader] Error: Cannot open %s\n", csv_path);
        return -1;
    }
    
    /* Reset state */
    g_num_gates = 0;
    g_initialized = 0;
    
    /* Read lines */
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_num++;
        
        /* Skip header line */
        if (line_num == 1) {
            if (strstr(line, "patch") && strstr(line, "channel")) {
                continue;
            }
        }
        
        /* Skip empty or comment lines */
        char *trimmed = trim(line);
        if (*trimmed == '\0' || *trimmed == '#') {
            continue;
        }
        
        /* Parse entry */
        int patch, channel;
        uint16_t gate;
        if (parse_gate_line(line, &patch, &channel, &gate) == 0) {
            if (g_num_gates >= MAX_GATE_ENTRIES) {
                fprintf(stderr, "[gate_loader] Warning: Max entries (%d) reached at line %d\n",
                        MAX_GATE_ENTRIES, line_num);
                break;
            }
            
            g_gates[g_num_gates].patch = patch;
            g_gates[g_num_gates].channel = channel;
            g_gates[g_num_gates].gate_samples = gate;
            g_num_gates++;
            entries_loaded++;
        } else {
            fprintf(stderr, "[gate_loader] Warning: Invalid line %d: %s\n",
                    line_num, trimmed);
        }
    }
    
    fclose(fp);
    
    if (entries_loaded > 0) {
        g_initialized = 1;
        fprintf(stderr, "[gate_loader] Loaded %d gate entries from %s\n",
                entries_loaded, csv_path);
    } else {
        fprintf(stderr, "[gate_loader] Warning: No valid entries loaded from %s\n",
                csv_path);
        return -1;
    }
    
    return 0;
}

int gate_loader_lookup(int patch, int channel, uint16_t *out_gate) {
    int i;
    
    if (!g_initialized) {
        return -1;
    }
    
    if (!out_gate) {
        return -1;
    }
    
    /* Linear search (could optimize with hash table if needed) */
    for (i = 0; i < g_num_gates; i++) {
        if (g_gates[i].patch == patch && g_gates[i].channel == channel) {
            *out_gate = g_gates[i].gate_samples;
            return 0;
        }
    }
    
    /* Not found */
    return -1;
}

uint16_t gate_loader_get_default(void) {
    return g_default_gate;
}

void gate_loader_set_default(uint16_t default_gate) {
    g_default_gate = default_gate;
}

void gate_loader_cleanup(void) {
    g_num_gates = 0;
    g_initialized = 0;
}

int gate_loader_count(void) {
    return g_initialized ? g_num_gates : -1;
}
