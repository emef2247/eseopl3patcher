#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
  #include <string.h>
  #include <ctype.h>
  #define strcasecmp _stricmp
#else
  #include <strings.h> // strcasecmp
#endif

int compat_strcasecmp(const char* a, const char* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return strcasecmp(a, b);
}