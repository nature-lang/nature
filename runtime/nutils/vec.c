#include "vec.h"

#include "array.h"
#include "runtime/memory.h"

void vec_grow(n_vec_t *vec) {
    vec->capacity = vec->capacity * 2;

    rtype_t *element_rtype = rt_find_rtype(vec->element_rtype_hash);
    assertf(element_rtype, "cannot find element_rtype with hash");

    n_array_t *new_data = rt_array_new(element_rtype, vec->capacity);

    uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);

    DEBUGF("[vec_grow] old_vec=%p, len=%lu, cap=%lu, new_vec=%p, element_size=%lu", vec, vec->length, vec->capacity, new_data,
           element_size);

    memmove(new_data, vec->data, vec->length * element_size);

    vec->data = new_data;
}

/**
 * [string] 对于这样的声明，现在默认其 element 元素是存储在堆上的
 * @param rtype_hash
 * @param element_rtype_hash
 * @param length vec 大小，允许为 0，当 capacity = 0 时，使用 default_capacity
 * @return
 */
n_vec_t *vec_new(uint64_t rtype_hash, uint64_t element_rtype_hash, uint64_t length, uint64_t capacity) {
    DEBUGF("[vec_new] r_hash=%lu,e_hash=%lu,len=%lu,cap=%lu", rtype_hash, element_rtype_hash, length, capacity);

    assertf(rtype_hash > 0, "rtype_hash must be a valid hash");
    assertf(element_rtype_hash > 0, "element_rtype_hash must be a valid hash");

    if (capacity == 0) {
        if (length > 0) {
            capacity = length;
        } else {
            capacity = VEC_DEFAULT_CAPACITY;
        }
    }

    assert(capacity >= length && "capacity must be greater than length");
    TRACEF("[runtime.vec_new] length=%lu, capacity=%lu", length, capacity);

    // find rtype and element_rtype
    rtype_t *vec_rtype = rt_find_rtype(rtype_hash);
    assert(vec_rtype && "cannot find rtype with hash");

    rtype_t *element_rtype = rt_find_rtype(element_rtype_hash);
    assert(element_rtype && "cannot find element_rtype with hash");

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rt_clr_malloc(vec_rtype->size, vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->element_rtype_hash = element_rtype_hash;
    vec->data = rt_array_new(element_rtype, capacity);

    TRACEF("[runtime.vec_new] success, vec=%p, data=%p, element_rtype_hash=%lu", vec, vec->data, vec->element_rtype_hash);
    return vec;
}

/**
 * @param l
 * @param index
 * @param value_ref
 */
void vec_access(n_vec_t *l, uint64_t index, void *value_ref) {
    if (index >= l->length) {
        char *msg = dsprintf("index out of range [%d] with length %d", index, l->length);
        DEBUGF("[runtime.vec_access] has err %s", msg);
        rt_processor_attach_errort(msg);
        return;
    }

    uint64_t element_size = rt_rtype_out_size(l->element_rtype_hash);
    // 计算 offset
    uint64_t offset = element_size * index; // (size unit byte) * index
    memmove(value_ref, l->data + offset, element_size);
}

/**
 * index 必须在 length 范围内
 * @param l
 * @param index
 * @param ref
 * @return
 */
void vec_assign(n_vec_t *l, uint64_t index, void *ref) {
    // assert(index <= l->length - 1 && "index out of range [%d] with length %d", index, l->length);
    assert(index <= l->length - 1 && "index out of range"); // TODO runtime 错误提示优化

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_hash);
    uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);
    DEBUGF("[runtime.vec_assign] element_size=%lu", element_size);
    // 计算 offset
    uint64_t offset = rtype_out_size(element_rtype, POINTER_SIZE) * index; // (size unit byte) * index
    void *p = l->data + offset;
    memmove(p, ref, element_size);
}

uint64_t vec_length(n_vec_t *l) {
    return l->length;
}

uint64_t vec_capacity(n_vec_t *l) {
    return l->capacity;
}

void *vec_ref(n_vec_t *l) {
    return l->data;
}

/**
 * ref 指向 element value 值所在的地址，其可能是一个栈地址，也可能是一个堆地址
 * @param vec
 * @param ref
 */
void vec_push(n_vec_t *vec, void *ref) {
    assert(ref > 0 && "ref must be a valid address");

    // TODO debug 验证 gc 问题
    if (span_of((addr_t)vec) == NULL || vec->element_rtype_hash <= 0) {
        processor_t *p = processor_get();
        coroutine_t *co = coroutine_get();
        assertf(false, "vec_push failed, p_index_%d=%d(%lu), p_status=%d, co=%p vec=%p ele_rtype_hash=%lu must be a valid hash", p->share,
                p->index, (uint64_t)p->thread_id, p->status, co, vec, vec->element_rtype_hash);
    }

    DEBUGF("[vec_push] vec=%p,data=%p, current_length=%lu, value_ref=%p, value_data(uint64)=%0lx", vec, vec->data, vec->length, ref,
           (uint64_t)fetch_int_value((addr_t)ref, 8));

    if (vec->length == vec->capacity) {
        DEBUGF("[vec_push] current len=%lu equals cap, trigger grow, next capacity=%lu", vec->length, vec->capacity * 2);
        vec_grow(vec);
    }

    uint64_t index = vec->length++;
    vec_assign(vec, index, ref);
}

/**
 * 在原有 array data 上进行切割
 * @param rtype_hash
 * @param l
 * @param start 起始 index [start, end)
 * @param end 结束 index
 * @return
 */
n_vec_t *vec_slice(uint64_t rtype_hash, n_vec_t *l, int64_t start, int64_t end) {
    // start end 检测
    if (start >= l->length || end > l->length || start < 0 || end < 0) {
        char *msg = dsprintf("slice [%d:%d] out of vec with length %d", start, end, l->length);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rt_processor_attach_errort(msg);
        return 0;
    }

    if (start > end) {
        char *msg = dsprintf("invalid index values, must be low %d <= high %d", start, end);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rt_processor_attach_errort(msg);
        return 0;
    }

    DEBUGF("[vec_slice] rtype_hash=%lu, element_rtype_hash=%lu, start=%lu, end=%lu", rtype_hash, l->element_rtype_hash, start, end);
    uint64_t length = end - start;

    rtype_t *vec_rtype = rt_find_rtype(rtype_hash);
    assert(vec_rtype && "cannot find rtype with hash");
    n_vec_t *sliced_vec = rt_clr_malloc(vec_rtype->size, vec_rtype);
    sliced_vec->capacity = length;
    sliced_vec->length = length;
    sliced_vec->element_rtype_hash = l->element_rtype_hash;

    uint64_t element_size = rt_rtype_out_size(l->element_rtype_hash);
    sliced_vec->data = l->data + start * element_size;

    return sliced_vec;
}

/**
 * 不影响原来的 vec，而是返回一个新的 vec
 * @param rtype_hash
 * @param a
 * @param b
 * @return
 */
n_vec_t *vec_concat(uint64_t rtype_hash, n_vec_t *a, n_vec_t *b) {
    DEBUGF("[vec_concat] rtype_hash=%lu, a=%p, b=%p", rtype_hash, a, b);
    assert(a->element_rtype_hash == b->element_rtype_hash && "The types of the two vecs are different");
    uint64_t element_size = rt_rtype_out_size(a->element_rtype_hash);
    uint64_t length = a->length + b->length;
    n_vec_t *merged = vec_new(rtype_hash, a->element_rtype_hash, length, length);
    DEBUGF("[vec_concat] a->len=%lu, b->len=%lu", a->length, b->length);

    // 合并 a
    void *dst = merged->data;
    memmove(dst, a->data, a->length * element_size);

    // 合并 b
    dst = merged->data + (a->length * element_size);
    memmove(dst, b->data, b->length * element_size);

    return merged;
}

n_cptr_t vec_element_addr(n_vec_t *l, uint64_t index) {
    TRACEF("[vec_element_addr] l=%p, element_rtype_hash=%lu, index=%lu", l, l->element_rtype_hash, index);
    if (index >= l->length) {
        char *msg = dsprintf("index out of vec [%d] with length %d", index, l->length);
        DEBUGF("[runtime.vec_element_addr] has err %s", msg);
        rt_processor_attach_errort(msg);
        return 0;
    }

    uint64_t element_size = rt_rtype_out_size(l->element_rtype_hash);
    // 计算 offset
    uint64_t offset = element_size * index; // (size unit byte) * index

    DEBUGF("[vec_element_addr] l->data=%p, offset=%lu, result=%p", l->data, offset, (l->data + offset));
    return (n_cptr_t)l->data + offset;
}

n_cptr_t vec_iterator(n_vec_t *l) {
    if (l->length == l->capacity) {
        DEBUGF("[vec_iterator] current_length=%lu == capacity, trigger grow, next capacity=%lu", l->length, l->capacity * 2);
        vec_grow(l);
    }
    uint64_t index = l->length++;

    DEBUGF("[vec_iterator] l=%p, element_rtype_hash=%lu, index=%lu", l, l->element_rtype_hash, index);

    n_cptr_t addr = vec_element_addr(l, index);
    DEBUGF("[vec_iterator] addr=%lx", addr);
    return addr;
}
