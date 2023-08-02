#include "string.h"
#include "array.h"

/**
 * 实际申请的空间为 length + 1, 多一个空间用于存放 \0
 * 但是 str->length 的值依旧为不包含 \0 的长度(gc 应该会自动释放这多余的长度)
 * @param raw_string
 * @param length
 * @return
 */
n_string_t *string_new(void *raw_string, uint64_t length) {
    DEBUGF("[string_new] raw_string=%s, length=%lu", (char *) raw_string, length);

    // byte 数组，先手动创建一个简单类型
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);
    n_array_t *data = NULL;
    rtype_t *string_rtype;
    // 如果是空字符串则不需要申请 data 部分的内存空间
    if (length > 0) {
        data = rt_array_new(element_rtype, length + 1);
        data[length] = '\0';

        // 创建 memory_string_t 类型，并转换成 rtype 进行 堆内存申请
        string_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    } else {
        // 空字符串特殊处理
        string_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    }

    DEBUGF("[string_new] rtype gc_bits=%s", bitmap_to_str(string_rtype->gc_bits, 2));
    n_string_t *string_data = runtime_malloc(string_rtype->size, string_rtype);
    string_data->data = data;
    string_data->length = length;
    memmove(string_data->data, raw_string, length);

    DEBUGF("[string_new] success, string_data=%p", string_data);
    return string_data;
}

void *string_raw(n_string_t *n_str) {
    DEBUGF("[runtime.string_raw] length=%lu, data=%p, last is 0?  %d", n_str->length, n_str->data,
           n_str->data[n_str->length] == '\0');
    return n_str->data;

}

n_string_t *string_concat(n_string_t *a, n_string_t *b) {
    DEBUGF("[runtime.string_concat] a=%s, b=%s", a->data, b->data);

    uint64_t length = a->length + b->length;
    rtype_t *element_rtype = gc_rtype(TYPE_UINT8, 0);
    rtype_t *string_rtype = gc_rtype(TYPE_STRING, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_array_t *data = rt_array_new(element_rtype, length + 1);

    // 将 str copy 到 data 中
    memmove(data, a->data, a->length);
    memmove(data + a->length, b->data, b->length);
    data[length] = '\0';

    n_string_t *str = runtime_malloc(string_rtype->size, string_rtype);
    str->length = length;
    str->data = data;
    return str;
}

n_int_t string_length(n_string_t *a) {
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
