#ifndef NATURE_UTILS_SAFE_SLICE_H_
#define NATURE_UTILS_SAFE_SLICE_H_

#include "runtime/runtime.h"
#include "utils/slice.h"

/**
 * 初始给 8 个大小
 * @return
 */
static inline slice_t *safe_slice_new() {
    slice_t *s = safe_mallocz(sizeof(slice_t));
    s->count = 0;
    s->take = safe_mallocz(sizeof(void *) * 8);
    s->capacity = 8;
    return s;
}

static inline slice_t *safe_slice_capacity_new(int capacity) {
    slice_t *s = safe_mallocz(sizeof(slice_t));
    s->count = 0;
    s->take = safe_mallocz(sizeof(void *) * capacity);
    s->capacity = capacity;
    return s;
}

static inline void safe_slice_insert(slice_t *s, int index, void *value) {
    if (s->count == s->capacity) {
        s->capacity *= 2;
        s->take = safe_realloc(s->take, sizeof(void *) * s->capacity);
    }
    for (int i = s->count; i > index; --i) {
        s->take[i] = s->take[i - 1];
    }
    s->take[index] = value;
    s->count++;
}

static inline int safe_slice_push(slice_t *s, void *value) {
    if (s->capacity <= s->count) {
        s->capacity = s->capacity * 2;
        s->take = safe_realloc(s->take, sizeof(void *) * s->capacity);
    }

    s->take[s->count++] = value;
    return s->count - 1; // index
}

static inline void safe_slice_concat(slice_t *dst, slice_t *src) {
    assert(src);
    for (int i = 0; i < src->count; ++i) {
        safe_slice_push(dst, src->take[i]);
    }
}

static inline void safe_slice_concat_free(slice_t *dst, slice_t *src) {
    for (int i = 0; i < src->count; ++i) {
        slice_push(dst, src->take[i]);
    }
    safe_free(src->take);
    safe_free(src);
}

#endif
