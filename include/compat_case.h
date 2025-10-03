#ifndef ESEOPL3PATCHER_COMPAT_CASE_H
#define ESEOPL3PATCHER_COMPAT_CASE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int compat_strcasecmp(const char *a, const char *b);
int compat_strncasecmp(const char *a, const char *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* ESEOPL3PATCHER_COMPAT_CASE_H */