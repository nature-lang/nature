#include <assert.h>
#include "slice.h"
#include "stdlib.h"
#include "helper.h"

slice_t *slice_new() {
    slice_t *s = mallocz(sizeof(slice_t));
    s->count = 0;
    s->take = mallocz(sizeof(void *) * 8);
    s->capacity = 8;
    return s;
}

slice_t *slice_capacity_new(int capacity) {
    slice_t *s = mallocz(sizeof(slice_t));
    s->count = 0;
    s->take = mallocz(sizeof(void *) * capacity);
    s->capacity = capacity;
    return s;
}

void slice_insert(slice_t *s, int index, void *value) {
    if (s->count == s->capacity) {
        s->capacity *= 2;
        s->take = realloc(s->take, sizeof(void *) * s->capacity);
    }
    for (int i = s->count; i > index; --i) {
        s->take[i] = s->take[i - 1];
    }
    s->take[index] = value;
    s->count++;
}

int slice_push(slice_t *s, void *value) {
    if (s->capacity <= s->count) {
        s->capacity = s->capacity * 2;
        s->take = realloc(s->take, sizeof(void *) * s->capacity);
    }

    s->take[s->count++] = value;
    return s->count - 1; // index
}

void slice_concat(slice_t *dst, slice_t *src) {
    assert(src);
    for (int i = 0; i < src->count; ++i) {
        slice_push(dst, src->take[i]);
    }
}

void slice_concat_free(slice_t *dst, slice_t *src) {
    for (int i = 0; i < src->count; ++i) {
        slice_push(dst, src->take[i]);
    }
    free(src->take);
    free(src);
}

/**
 * 如果 i == s->count 或者为 -1 则删除最后一个元素
 * @param s
 * @param i
 * @return
 */
void *slice_remove(slice_t *s, int i) {
    assert(0 < i < s->count);

    void *value = s->take[i];
    for (int j = i; j < s->count - 1; ++j) {
        s->take[j] = s->take[j + 1]; // 从 i 开始重拍
    }
    s->count--;
    return value;
}