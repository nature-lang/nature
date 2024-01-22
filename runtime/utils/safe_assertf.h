#ifndef NATURE_SAFE_ASSERTF_H
#define NATURE_SAFE_ASSERTF_H

#include "runtime/runtime.h"
#include "utils/assertf.h"

#define safe_assertf(e, fmt, ...)       \
    do {                                \
        PREEMPT_LOCK();                 \
        assertf(e, fmt, ##__VA_ARGS__); \
        PREEMPT_UNLOCK();               \
    } while (0)

#endif // NATURE_SAFE_ASSERTF_H
