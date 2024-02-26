#include "string.h"

#include "array.h"
#include "vec.h"

/**
 * length 不包含 '\0'
 * @param raw_string
 * @param length
 * @return
 */
n_string_t *string_new(void *raw_string, uint64_t length) {
    PRE_RTCALL_HOOK();
    TRACEF("[string_new] raw_string=%s, length=%lu", (char *)raw_string, length);

    // byte 数组，先手动创建一个简单类型
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);

    uint64_t capacity = length + 1; // +1 预留 '\0' 空间 给 string_ref 时使用

    n_array_t *data = rt_array_new(element_rtype, capacity);
    // 创建 memory_string_t 类型，并转换成 rtype 进行 堆内存申请

    rtype_t *string_rtype = gc_rtype(TYPE_STRING, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);

    assert(element_rtype->hash > 0);

    TRACEF("[string_new] rtype gc_bits=%s", bitmap_to_str(string_rtype->gc_bits, 2));
    n_string_t *str = rt_clr_malloc(string_rtype->size, string_rtype);
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    str->element_rtype_hash = element_rtype->hash;
    memmove(str->data, raw_string, length);

    DEBUGF("[string_new] success, string=%p, data=%p", str, str->data);
    return str;
}

/**
 * c 语言字符串中添加 '\0' 作为结束字符
 * @param n_str
 * @return
 */
void *string_ref(n_string_t *n_str) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_ref] length=%lu, data=%p", n_str->length, n_str->data);

    // 空间足够，且最后一位已经是 0， 可以直接返回
    if (n_str->capacity > n_str->length && n_str->data[n_str->length] == '\0') {
        return n_str->data;
    }

    // 结尾添加 '\0' 字符
    int a = '\0';
    vec_push(n_str, &a);
    n_str->length -= 1;

    return n_str->data;
}

n_string_t *string_concat(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_concat] a=%s, b=%s", a->data, b->data);

    uint64_t length = a->length + b->length;
    uint64_t capacity = length + 1;
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);
    rtype_t *string_rtype = gc_rtype(TYPE_STRING, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    n_array_t *data = rt_array_new(element_rtype, capacity);

    // 将 str copy 到 data 中
    memmove(data, a->data, a->length);
    memmove(data + a->length, b->data, b->length);

    n_string_t *str = rt_clr_malloc(string_rtype->size, string_rtype);
    str->length = length;
    str->data = data;
    str->length = length;
    str->capacity = capacity;
    str->element_rtype_hash = element_rtype->hash;
    DEBUGF("[runtime.string_concat] success, string=%p, data=%p", str, str->data);
    return str;
}

n_int_t string_length(n_string_t *a) {
    PRE_RTCALL_HOOK();
    return (n_int_t)a->length;
}

n_bool_t string_ee(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_ee] a=%s, b=%s, a_len=%ld, b_len=%ld", a->data, b->data, a->length, b->length);
    return a->length == b->length && memcmp(a->data, b->data, a->length) == 0;
}

n_bool_t string_ne(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_ne] a=%s, b=%s", a->data, b->data);
    return a->length != b->length || memcmp(a->data, b->data, a->length) != 0;
}

n_bool_t string_lt(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_lt] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result < 0 || (cmp_result == 0 && a->length < b->length);
}

n_bool_t string_le(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_le] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result < 0 || (cmp_result == 0 && a->length <= b->length);
}

n_bool_t string_gt(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_gt] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result > 0 || (cmp_result == 0 && a->length > b->length);
}

n_bool_t string_ge(n_string_t *a, n_string_t *b) {
    PRE_RTCALL_HOOK();
    DEBUGF("[runtime.string_ge] a=%s, b=%s\n", a->data, b->data);
    size_t min_length = a->length < b->length ? a->length : b->length;
    int cmp_result = memcmp(a->data, b->data, min_length);
    return cmp_result > 0 || (cmp_result == 0 && a->length >= b->length);
}
