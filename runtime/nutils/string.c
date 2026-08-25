#include "string.h"

#include <runtime/rtype.h>
#include <stdlib.h>

#include "array.h"
#include "vec.h"

// string copy to vec
// vec copy to string
n_vec_t string_to_vec(n_string_t *src) {
    return string_new(src->data, src->length);
}

n_string_t vec_to_string(n_vec_t *vec) {
    // Runtime-created strings are not constants. Interning every short value
    // keeps dynamic data alive forever and also requires a NUL-terminated key,
    // which a vec does not guarantee at its logical end.
    return string_new(vec->data, vec->length);
}

// Like Go's runtime.rawstring, this allocates the final backing storage so a
// trusted runtime caller can fill it directly. Nature reserves one extra byte
// because every string must also be NUL-terminated for the C ABI.
n_string_t rti_string_alloc(int64_t length) {
    assert(length >= 0 && length < INT64_MAX);
    int64_t capacity = length + 1; // +1 预留 '\0' 空间 给 string_ref 时使用
    n_array_t *data = rti_array_new(&string_element_rtype, capacity);

    n_string_t str = {0};
    str.data = data;
    str.length = length;
    str.capacity = capacity;
    str.element_size = (&string_element_rtype)->storage_size;
    str.hash = string_rtype.hash;
    str.data[length] = '\0';
    return str;
}

/**
 * length 不包含 '\0'
 * @param raw_string
 * @param length
 * @return
 */
n_string_t string_new(void *raw_string, int64_t length) {
    TRACEF("[string_new] raw_string=%s, length=%lu, ptr=%p", (char *) raw_string, length, raw_string);

    n_string_t str = rti_string_alloc(length);
    if (length > 0) {
        assert(raw_string);
        memmove(str.data, raw_string, length);
    }

    DEBUGF("[string_new] success, string=%p, data=%p, len=%ld, ele_size=%ld, raw_str=%s", &str, str.data, str.length,
           str.element_size, (char *) raw_string);
    return str;
}

n_string_t string_concat(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_concat] a=%s, b=%s", a->data, b->data);

    gc_mutator_yield_if_needed();

    if (a->length > INT64_MAX - b->length) {
        rti_throw("string concatenation result is too large", true);
        return (n_string_t) {0};
    }

    int64_t length = a->length + b->length;
    n_string_t str = rti_string_alloc(length);

    // 将 str copy 到 data 中
    if (a->length > 0) {
        memmove(str.data, a->data, a->length);
    }
    if (b->length > 0) {
        memmove(str.data + a->length, b->data, b->length);
    }
    DEBUGF("[runtime.string_concat] success, string=%p, data=%p", &str, str.data);
    return str;
}

n_int_t rt_string_find_after(n_string_t *self, n_string_t *sub, n_int_t after) {
    if (sub->length == 0 || after < 0 || after > self->length || sub->length > self->length - after) {
        return -1;
    }

    uint8_t *base = self->data;
    uint8_t *cursor = base + after;
    uint8_t *last = base + self->length - sub->length;
    while (cursor <= last) {
        size_t candidates = (size_t) (last - cursor + 1);
        cursor = memchr(cursor, sub->data[0], candidates);
        if (!cursor) {
            return -1;
        }

        if (sub->length == 1 || memcmp(cursor, sub->data, (size_t) sub->length) == 0) {
            return (n_int_t) (cursor - base);
        }
        cursor++;
    }

    return -1;
}

n_int_t rt_string_length(n_string_t *a) {
    DEBUGF("rt_string_length %p, a.leng %ld", a, a->length);
    return (n_int_t) a->length;
}

n_bool_t string_ee(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_ee] a=%s, b=%s, a_len=%ld, b_len=%ld", a->data, b->data, a->length, b->length);
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

n_bool_t string_ne(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_ne] a=%s, b=%s", a->data, b->data);
    return a->length != b->length || memcmp(a->data, b->data, a->length) != 0;
}

n_bool_t string_lt(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_lt] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result < 0 || (cmp_result == 0 && a->length < b->length);
}

n_bool_t string_le(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_le] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result < 0 || (cmp_result == 0 && a->length <= b->length);
}

n_bool_t string_gt(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_gt] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result > 0 || (cmp_result == 0 && a->length > b->length);
}

n_bool_t string_ge(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_ge] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result > 0 || (cmp_result == 0 && a->length >= b->length);
}
