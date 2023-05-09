#ifndef __ASSERTF_H
#define __ASSERTF_H

#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define COL_NONE    ""
#define COL_RST     "\x1b[0m"
#define COL_RED     "\x1b[91m"
#define COL_GRAY    "\x1b[02m"
#define COL_CYAN    "\x1b[36m"

#define COL(col)            (isatty(fileno(stderr)) ? (COL_##col) : COL_NONE)

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

#define assertf(e, fmt, ...)                                                        \
    x_assertf_c21162d2(!!(e), "Assert %s(%s)%s failed: " fmt " %s[%s:%d (%s)]%s\n", \
        COL(RED), #e, COL(RST),                                                     \
        ##__VA_ARGS__,                                                              \
        COL(GRAY), x_bname_dfc95d52(__BASE_FILE__), __LINE__, __func__, COL(RST))

#endif
