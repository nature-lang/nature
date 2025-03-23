#ifndef __ASSERTF_H
#define __ASSERTF_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
// 移除了 unistd.h 和 fcntl.h，因为不再需要 isatty

#define COL_NONE    ""
#define COL_RST     "\x1b[0m"
#define COL_RED     "\x1b[91m"
#define COL_GRAY    "\x1b[02m"
#define COL_CYAN    "\x1b[36m"

// 移除 isatty 检查，直接使用颜色代码
#define COL(col)            (COL_##col)

/**
 * basename(3) have inconsistent implementation across UNIX-like systems.
 * Besides, Windows doesn't have such API.
 */
static inline const char *x_bname_dfc95d52(const char *path) {
    const char *p;
    p = strrchr(path, '/');
    return p != NULL ? p + 1 : path;
}

static inline void x_assertf_c21162d2(int expr, const char *fmt, ...) {
    if (!expr) {
        va_list ap;
        va_start(ap, fmt);
        (void) vfprintf(stderr, fmt, ap);
        va_end(ap);

        abort();
    }
}

#ifdef NDEBUG
    #define assertf(e, fmt, ...) ((void)0)
#else
#define assertf(e, fmt, ...)                                                        \
    x_assertf_c21162d2(!!(e), "Assert %s(%s)%s failed: " fmt " %s[%s:%d (%s)]%s\n", \
        COL(RED), #e, COL(RST),                                                     \
        ##__VA_ARGS__,                                                              \
        COL(GRAY), x_bname_dfc95d52(__FILE__), __LINE__, __func__, COL(RST))
#endif
#endif
