#include "string.h"

#include <runtime/rtype.h>

#include "array.h"
#include "vec.h"

/**
 * length 不包含 '\0'
 * @param raw_string
 * @param length
 * @return
 */
n_string_t *string_new(void *raw_string, int64_t length) {
    TRACEF("[string_new] raw_string=%s, length=%lu, ptr=%p", (char *) raw_string, length, raw_string);

    // byte 数组，先手动创建一个简单类型
    int64_t capacity = length + 1; // +1 预留 '\0' 空间 给 string_ref 时使用

    n_array_t *data = rti_array_new(&string_element_rtype, capacity);

    n_string_t *str = rti_gc_malloc(string_rtype.size, &string_rtype);
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    str->element_size = rtype_stack_size(&string_element_rtype, POINTER_SIZE);
    str->hash = string_rtype.hash;
    memmove(str->data, raw_string, length);

    DEBUGF("[string_new] success, string=%p, data=%p, len=%ld, ele_size=%ld, raw_str=%s", str, str->data, str->length,
            str->element_size, (char *) raw_string);
    return str;
}

n_string_t *string_concat(n_string_t *a, n_string_t *b) {

    DEBUGF("[runtime.string_concat] a=%s, b=%s", a->data, b->data);

    int64_t length = a->length + b->length;
    int64_t capacity = length + 1;
    n_array_t *data = rti_array_new(&string_element_rtype, capacity);

    // 将 str copy 到 data 中
    memmove(data, a->data, a->length);
    memmove(data + a->length, b->data, b->length);

    n_string_t *str = rti_gc_malloc(string_rtype.size, &string_rtype);
    str->length = length;
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    str->element_size = rtype_stack_size(&string_element_rtype, POINTER_SIZE);
    str->hash = string_rtype.hash;
    DEBUGF("[runtime.string_concat] success, string=%p, data=%p", str, str->data);
    return str;
}

n_int_t rt_string_length(n_string_t *a) {

    return (n_int_t) a->length;
}

n_bool_t string_ee(n_string_t *a, n_string_t *b) {

    assert(a);
    assert(b);
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
