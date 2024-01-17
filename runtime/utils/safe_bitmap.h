#ifndef NATURE_UTILS_SAFE_BITMAP_H
#define NATURE_UTILS_SAFE_BITMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "runtime/runtime.h"
#include "safe_mutex.h"
#include "utils/bitmap.h"

static inline bitmap_t *safe_bitmap_new(uint64_t size) {
    bitmap_t *b = SAFE_NEW(bitmap_t);

    b->bits = safe_mallocz(size);
    b->size = size;
    b->locker = safe_mutex_new(false);

    return b;
}

static inline void safe_bitmap_free(bitmap_t *b) {
    safe_free(b->bits);
    safe_free(b);
}

#endif // NATURE_BITMAP_H
