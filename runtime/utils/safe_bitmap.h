#ifndef NATURE_UTILS_SAFE_BITMAP_H
#define NATURE_UTILS_SAFE_BITMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "runtime/runtime.h"
#include "safe_mutex.h"
#include "utils/bitmap.h"

static inline bitmap_t *safe_bitmap_new(uint64_t size) {
    bitmap_t *b = MANUAL_NEW(bitmap_t);

    b->bits = manual_malloc(size);
    b->size = size;
    b->locker = safe_mutex_new(false);

    return b;
}

static inline void rt_bitmap_free(bitmap_t *b) {
    manual_free(b->bits);
    manual_free(b);
}

#endif // NATURE_BITMAP_H
