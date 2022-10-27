/*
 * C header-only formattable assert macros.
 *
 * Created May 21, 2020. leiless.
 * XXX: side-effect unsafe.
 *
 * Usage:
 *  #define ASSERTF_DEF_ONCE
 *  #include "assertf.h"
 *
 *  -DASSERTF_DISABLE in Makefile to disable assertf.h
 *
 * For more usage, check out README.md
 * Released under BSD-2-Clause license.
 */

#ifndef __ASSERTF_H
#define __ASSERTF_H

#define ASSERTF_DEF_ONCE 1

/**
 * Compile-time assertion  see: linux/arch/x86/boot/boot.h
 *
 * [sic modified]
 * BUILD_BUG_ON - break compile if a condition is true.
 * @cond: the condition which the compiler should know is false.
 *
 * If you have some code which relies on certain constants being true, or
 * some other compile-time evaluated condition, you should use BUILD_BUG_ON() to
 * detect if someone changes it unexpectedly.
 */
#ifndef BUILD_BUG_ON
#ifdef DEBUG
#define BUILD_BUG_ON(cond)      ((void) sizeof(char[1 - 2 * !!(cond)]))
#else
#define BUILD_BUG_ON(cond)      ((void) (cond))
#endif
#endif

/* see: https://stackoverflow.com/a/6849629 */
#ifdef _WIN32
#if _MSC_VER >= 1400
#include <sal.h>
#if _MSC_VER > 1400
#define __printflike(p) _Printf_format_string_ p
#else
#define __printflike(p) __format_string p
#endif /* __printflike */
#else
#define __printflike(p) p
#endif /* _MSC_VER */
#else
/* Macro taken from macOS/Frameworks/Kernel/sys/cdefs.h */
#ifndef __printflike
#define __printflike(fmtarg, firstvararg) \
        __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#endif
#endif  /* _WIN32 */

#ifndef ASSERTF_DISABLE

#ifdef __cplusplus
extern "C" {
#endif
#ifdef _WIN32
extern void x_assertf_c21162d2(int, __printflike(const char *), ...);
#else

extern void x_assertf_c21162d2(int, const char *, ...) __printflike(2, 3);

#endif

const char *x_bname_dfc95d52(const char *);

#ifdef __cplusplus
}
#endif
#ifdef ASSERTF_DEF_ONCE

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/**
 * Formatted version of assert()
 *
 * @param expr  Expression to assert with
 * @param fmt   Format string when assertion failed
 * @param ...   Format string arguments
 */
#ifdef _WIN32
void x_assertf_c21162d2(int expr, __printflike(const char *fmt), ...)
#else

void x_assertf_c21162d2(int expr, const char *fmt, ...)
#endif
{
    if (!expr) {
        va_list ap;
        va_start(ap, fmt);
        (void) vfprintf(stderr, fmt, ap);
        va_end(ap);

        abort();
    }
}

#include <string.h>

/**
 * basename(3) have inconsistent implementation across UNIX-like systems.
 * Besides, Windows doesn't have such API.
 */
const char *x_bname_dfc95d52(const char *path) {
    const char *p;
#ifdef _WIN32
    p = strrchr(path, '\\');
#else
    p = strrchr(path, '/');
#endif
    return p != NULL ? p + 1 : path;
}

#endif

#ifdef _WIN32
#define __FILE0__       __FILE__
#else
#define __FILE0__       __BASE_FILE__
#endif

/**
 * Taken from https://github.com/sharkdp/dbg-macro
 */
#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#define ASSERTF_IS_UNIX
#endif

/* see: https://misc.flogisoft.com/bash/tip_colors_and_formatting */
#define COL_NONE    ""
#define COL_RST     "\x1b[0m"
#define COL_RED     "\x1b[91m"
#define COL_GRAY    "\x1b[02m"
#define COL_CYAN    "\x1b[36m"

#ifdef ASSERTF_IS_UNIX

#include <stdio.h>
#include <unistd.h>

#define COL(col)            (isatty(fileno(stderr)) ? (COL_##col) : COL_NONE)
#else
/* Eat color strings */
#define COL(col)            ""
#endif

#define assertf(e, fmt, ...)                                                        \
    x_assertf_c21162d2(!!(e), "Assert %s(%s)%s failed: " fmt " %s[%s:%d (%s)]%s\n", \
        COL(RED), #e, COL(RST),                                                     \
        ##__VA_ARGS__,                                                              \
        COL(GRAY), x_bname_dfc95d52(__FILE0__), __LINE__, __func__, COL(RST))
#else   /* ASSERTF_DISABLE */
#ifdef __cplusplus
extern "C" {
#endif
/*
 * see:
 *  https://stackoverflow.com/questions/29117836/attribute-const-vs-attribute-pure-in-gnu-c
 *  https://stackoverflow.com/questions/2798188/pure-const-function-attributes-in-different-compilers
 */
#ifdef _WIN32
int __vunused(void *, ...);
#else
int __vunused(void *, ...) __attribute__((const));
#endif
#ifdef __cplusplus
}
#endif

#ifdef ASSERTF_DEF_ONCE
int __vunused(void *arg, ...)
{
    return arg != NULL;
}
#endif

/* Fix GitHub #2 implicit declaration of function 'COL' */
#define COL(col)    ""

#include <stdint.h>
#define assertf(e, fmt, ...)        (void) __vunused((void *) (uintptr_t) (e), fmt, ##__VA_ARGS__)
#endif  /* ASSERTF_DISABLE */

#ifdef _WIN32
#define __unreachable()     __assume(0)
#else
#define __unreachable()     __builtin_unreachable()
#endif

#endif
