#ifndef ESEOPL3PATCHER_COMPAT_STRING_H
#define ESEOPL3PATCHER_COMPAT_STRING_H

#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
  #include <strings.h> /* strcasecmp */
#else
  #define strcasecmp _stricmp
#endif

static inline char *compat_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *p = (char*)malloc(len + 1);
    if (!p) return NULL;
    memcpy(p, s, len + 1);
    return p;
}

#ifndef HAVE_STRDUP
#define strdup compat_strdup
#endif

#endif /* ESEOPL3PATCHER_COMPAT_STRING_H */