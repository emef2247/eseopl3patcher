#include "override_loader.h"
#include "override_apply.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* 内部ヘルパ */

static char *read_entire_file(const char *path, long *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[rd] = '\0';
    if (out_len) *out_len = (long)rd;
    return buf;
}

static char *skip_ws(char *p) {
    while (*p && (unsigned char)*p <= 0x20) p++;
    return p;
}

static int match_key(char **pp, const char *key) {
    char *p = *pp;
    p = skip_ws(p);
    if (*p != '\"') return 0;
    p++;
    const char *k = key;
    while (*k && *p && *p == *k) { p++; k++; }
    if (*k != '\0') return 0;
    if (*p != '\"') return 0;
    p++;
    p = skip_ws(p);
    if (*p != ':') return 0;
    p++;
    *pp = p;
    return 1;
}

/* variant 名 (キー) を抽出 */
static int parse_variant_key(char **pp, char *out_variant, size_t out_sz) {
    char *p = *pp;
    p = skip_ws(p);
    if (*p != '\"') return 0;
    p++;
    char tmp[64];
    size_t i = 0;
    while (*p && *p != '\"' && i+1 < sizeof(tmp)) {
        if ((unsigned char)*p < 0x20) return 0;
        tmp[i++] = *p++;
    }
    if (*p != '\"') return 0;
    tmp[i] = '\0';
    p++;
    p = skip_ws(p);
    if (*p != ':') return 0;
    p++;
    strncpy(out_variant, tmp, out_sz-1);
    out_variant[out_sz-1] = '\0';
    *pp = p;
    return 1;
}

static int parse_int_value(char **pp, int *out_val) {
    char *p = *pp;
    p = skip_ws(p);
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (!isdigit((unsigned char)*p)) return 0;
    long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
        if (v > 1000000) break;
    }
    *out_val = (int)(sign * v);
    *pp = p;
    return 1;
}

/**
 * override_loader_load_json
 * 非常に限定的な JSON を解析。失敗しても既存登録は残す。
 */
int override_loader_load_json(const char *path) {
    long sz = 0;
    char *buf = read_entire_file(path, &sz);
    if (!buf) {
        fprintf(stderr, "[OVERRIDE] cannot open: %s\n", path);
        return -1;
    }

    char *p = buf;
    p = skip_ws(p);

    // 全体から "patch_overrides" セクションを探す (緩い検索)
    char *section = strstr(p, "\"patch_overrides\"");
    if (!section) {
        fprintf(stderr, "[OVERRIDE] 'patch_overrides' not found in %s\n", path);
        free(buf);
        return -2;
    }
    p = section;
    if (!match_key(&p, "patch_overrides")) {
        fprintf(stderr, "[OVERRIDE] malformed 'patch_overrides' key\n");
        free(buf);
        return -3;
    }
    p = skip_ws(p);
    if (*p != '{') {
        fprintf(stderr, "[OVERRIDE] expected '{' after patch_overrides\n");
        free(buf);
        return -4;
    }
    p++;

    int add_count = 0;

    while (1) {
        p = skip_ws(p);
        if (*p == '}') {
            p++;
            break;
        }
        char variant[32];
        if (!parse_variant_key(&p, variant, sizeof(variant))) {
            // 解析不能 → スキップ: '{' / '}' を進むまで読み飛ばす
            fprintf(stderr, "[OVERRIDE] skip unknown entry near: %.20s\n", p);
            while (*p && *p != ',' && *p != '}') p++;
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; break; }
            continue;
        }
        p = skip_ws(p);
        if (*p != '{') {
            fprintf(stderr, "[OVERRIDE] expected '{' for variant=%s\n", variant);
            while (*p && *p != ',' && *p != '}') p++;
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; break; }
            continue;
        }
        p++;

        int mod_delta = 0, car_delta = 0, fb_delta = 0;
        int have_mod=0, have_car=0, have_fb=0;

        // オブジェクト内キー
        while (1) {
            p = skip_ws(p);
            if (*p == '}') { p++; break; }
            if (match_key(&p, "mod_tl_delta")) {
                int v;
                if (parse_int_value(&p, &v)) {
                    mod_delta = v; have_mod=1;
                }
            } else if (match_key(&p, "car_tl_delta")) {
                int v;
                if (parse_int_value(&p, &v)) {
                    car_delta = v; have_car=1;
                }
            } else if (match_key(&p, "fb_delta")) {
                int v;
                if (parse_int_value(&p, &v)) {
                    fb_delta = v; have_fb=1;
                }
            } else {
                // 未知のキー → 次の ',' or '}' まで読み飛ばす
                p = skip_ws(p);
                if (*p == '\"') {
                    p++; while (*p && *p != '\"') p++; if (*p=='\"') p++;
                    p = skip_ws(p);
                    if (*p == ':') { p++; }
                }
                // 値スキップ（括弧/数値/文字列の単純ケースのみ想定）
                p = skip_ws(p);
                if (*p == '{') {
                    int brace=1; p++;
                    while (*p && brace>0) {
                        if (*p=='{') brace++;
                        else if (*p=='}') brace--;
                        p++;
                    }
                } else if (*p=='\"') {
                    p++; while (*p && *p!='\"') p++; if(*p=='\"') p++;
                } else {
                    while (*p && *p!=',' && *p!='}' ) p++;
                }
            }
            p = skip_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; break; }
        }

        if (have_mod || have_car || have_fb) {
            if (override_add(variant, mod_delta, car_delta, fb_delta) == 0) {
                add_count++;
            } else {
                fprintf(stderr, "[OVERRIDE] cannot add variant=%s (table full)\n", variant);
            }
        }

        p = skip_ws(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') { p++; p = skip_ws(p); break; }
    }

    fprintf(stderr, "[OVERRIDE] loaded %d entries from %s\n", add_count, path);
    free(buf);
    return 0;
}