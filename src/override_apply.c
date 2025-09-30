#include "override_apply.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERRIDE_MAX_VARIANTS 32

typedef struct {
    char variant[32];
    int  mod_tl_delta;
    int  car_tl_delta;
    int  fb_delta;
    int  active;
} variant_override_t;

static variant_override_t g_overrides[OVERRIDE_MAX_VARIANTS];

int override_init(void) {
    memset(g_overrides, 0, sizeof(g_overrides));
    return 0;
}

void override_reset(void) {
    memset(g_overrides, 0, sizeof(g_overrides));
}

int override_add(const char *variant, int mod_tl_delta, int car_tl_delta, int fb_delta) {
    if (!variant || !variant[0]) return -1;
    for (int i = 0; i < OVERRIDE_MAX_VARIANTS; ++i) {
        if (!g_overrides[i].active) {
            snprintf(g_overrides[i].variant, sizeof(g_overrides[i].variant), "%s", variant);
            g_overrides[i].mod_tl_delta = mod_tl_delta;
            g_overrides[i].car_tl_delta = car_tl_delta;
            g_overrides[i].fb_delta     = fb_delta;
            g_overrides[i].active       = 1;
            return 0;
        }
    }
    return -2;
}

static const variant_override_t *find_override(const char *variant) {
    if (!variant) return NULL;
    for (int i = 0; i < OVERRIDE_MAX_VARIANTS; ++i) {
        if (g_overrides[i].active && strcmp(g_overrides[i].variant, variant) == 0) {
            return &g_overrides[i];
        }
    }
    return NULL;
}

int override_apply_fb(const char *variant, int fb_value) {
    const variant_override_t *ov = find_override(variant);
    if (!ov) return fb_value;
    int v = fb_value + ov->fb_delta;
    if (v < 0) v = 0;
    if (v > 7) v = 7;
    return v;
}

int override_apply_tl(const char *variant, int tl_value, int is_modulator) {
    const variant_override_t *ov = find_override(variant);
    if (!ov) return tl_value;
    int delta = is_modulator ? ov->mod_tl_delta : ov->car_tl_delta;
    int v = tl_value + delta;
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    return v;
}

void override_dump_table(void) {
    fprintf(stderr, "[OVERRIDE] ---- table dump ----\n");
    for (int i = 0; i < OVERRIDE_MAX_VARIANTS; ++i) {
        if (!g_overrides[i].active) continue;
        fprintf(stderr, "[OVERRIDE] variant=%s modTL=%+d carTL=%+d fb=%+d\n",
                g_overrides[i].variant,
                g_overrides[i].mod_tl_delta,
                g_overrides[i].car_tl_delta,
                g_overrides[i].fb_delta);
    }
}