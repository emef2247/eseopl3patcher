#ifndef ESEOPL3PATCHER_COMPAT_BOOL_H
#define ESEOPL3PATCHER_COMPAT_BOOL_H

/* 
 * compat_bool.h
 * C99 <stdbool.h> が使えない古いコンパイラ向けフォールバック。
 * 通常は C99 以降で <stdbool.h> を使用。
 */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #include <stdbool.h>
#else
    typedef int bool;
    #ifndef true
    #define true  1
    #endif
    #ifndef false
    #define false 0
    #endif
#endif

#endif /* ESEOPL3PATCHER_COMPAT_BOOL_H */