#include "vec.h"

#include "array.h"
#include "runtime/runtime.h"

static void rti_vec_grow(n_vec_t *vec, rtype_t *element_rtype, int custom_capacity) {


    if (custom_capacity) {
        vec->capacity = custom_capacity;
    } else if (vec->capacity > 0) {
        vec->capacity = vec->capacity * 2;
    } else {
        vec->capacity = VEC_DEFAULT_CAPACITY;
    }

    assertf(element_rtype, "cannot find element_rtype with hash");

    n_array_t *new_data = rti_array_new(element_rtype, vec->capacity);

    DEBUGF("[rt_vec_grow] old_vec=%p, len=%lu, cap=%lu, new_vec=%p, element_size=%lu", vec, vec->length, vec->capacity,
           new_data,
           vec->element_size);

    if (vec->length > 0) {
        memmove(new_data, vec->data, vec->length * vec->element_size);
    }

    // vec->data = new_data; new_data 是 gc_malloc 新申请的已经进行了 write_barrier 处理
    rti_write_barrier_ptr(&vec->data, new_data, false);
}

/**
 * 通常用于已存在元素的数组初始化
 */
n_vec_t *rt_unsafe_vec_new(int64_t hash, int64_t element_hash, int64_t length) {
    DEBUGF("[rt_vec_new] hash=%lu, element_hash=%lu, len=%lu", hash, element_hash, length);

    assertf(hash > 0, "hash must be a valid hash");
    assertf(element_hash > 0, "element_hash must be a valid hash");

    int64_t capacity = length;
    if (capacity == 0) {
        capacity = VEC_DEFAULT_CAPACITY;
    }

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype with hash");
    int64_t element_size = rtype_stack_size(element_rtype, POINTER_SIZE);

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->element_size = element_size;
    vec->hash = hash;
    vec->data = rti_array_new(element_rtype, capacity);

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_size=%lu", vec, vec->data, vec->element_size);
    return vec;
}

/**
 * [string] 对于这样的声明，现在默认其 element 元素是存储在堆上的
 * @param hash
 * @param element_hash
 * @param length vec 大小，允许为 0，当 capacity = -1 时，使用 default_capacity
 * @return
 */
n_vec_t *rt_vec_new(int64_t hash, int64_t element_hash, int64_t length, void *value_ref) {
    DEBUGF("[rt_vec_new] hash=%lu, element_hash=%lu, len=%lu, cap=%lu", hash, element_hash, length);

    assertf(hash > 0, "hash must be a valid hash");
    assertf(element_hash > 0, "element_hash must be a valid hash");

    if (length < 0) {
        char *msg = tlsprintf("len must be greater than 0");
        rti_throw(msg, true);
        return NULL;
    }
    int64_t capacity = length;
    if (capacity == 0) {
        capacity = VEC_DEFAULT_CAPACITY;
    }

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype with hash");
    int64_t element_size = rtype_stack_size(element_rtype, POINTER_SIZE);

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->element_size = element_size;
    vec->hash = hash;
    if (capacity > 0) {
        vec->data = rti_array_new(element_rtype, capacity);

        if (length > 0) {
            uint64_t zero = 0;
            if (memcmp(value_ref, &zero, element_size) != 0) {
                DEBUGF("[rt_vec_new] will set default value_ref=%p, element_size=%lu", value_ref, element_size);

                for (int64_t i = 0; i < length; i++) {
                    void *dst = vec->data + (i * element_size);
                    memmove(dst, value_ref, element_size);
                }
            }
        }
    }

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_size=%lu", vec, vec->data, vec->element_size);
    return vec;
}

n_vec_t *rt_vec_cap(int64_t hash, int64_t element_hash, int64_t capacity) {
    if (capacity < 0) {
        char *msg = tlsprintf("cap must be greater than 0");
        rti_throw(msg, true);
        return NULL;
    }


    DEBUGF("[rt_vec_cap] hash=%lu, element_hash=%lu, len=%lu, cap=%lu", hash, element_hash, capacity);

    assertf(hash > 0, "rhash must be a valid hash");
    assertf(element_hash > 0, "element_hash must be a valid hash");

    int64_t length = 0;

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype && "cannot find element_rtype with hash");

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->element_size = rtype_stack_size(element_rtype, POINTER_SIZE);
    vec->hash = hash;
    if (capacity > 0) {
        vec->data = rti_array_new(element_rtype, capacity);
    }

    DEBUGF("[rt_vec_cap] success, vec=%p, data=%p, element_size=%lu, cap=%d", vec, vec->data, vec->element_size, capacity);
    return vec;
}

/**
 * @param l
 * @param index
 * @param value_ref
 */
void rti_vec_access(n_vec_t *l, uint64_t index, void *value_ref) {
    if (index >= l->length) {
        char *msg = tlsprintf("index out of range [%d] with length %d", index, l->length);
        DEBUGF("[runtime.rti_vec_access] has err %s", msg);
        rti_throw(msg, true);

        return;
    }

    // 计算 offset
    uint64_t offset = l->element_size * index; // (size unit byte) * index
    memmove(value_ref, l->data + offset, l->element_size);
}

/**
 * index 必须在 length 范围内
 * @param l
 * @param index
 * @param ref
 * @return
 */
void rti_vec_assign(n_vec_t *l, uint64_t index, void *ref) {
    // assert(index <= l->length - 1 && "index out of range [%d] with length %d", index, l->length);
    assert(index <= l->length - 1 && "index out of range"); // TODO runtime 错误提示优化

    DEBUGF("[runtime.rti_vec_assign] element_size=%lu", l->element_size);

    // 计算 offset
    uint64_t offset = l->element_size * index; // (size unit byte) * index
    void *p = l->data + offset;

    // 由于不清楚 element type, 所以进行保守的 write barrier
    if (l->element_size == POINTER_SIZE) {
        rti_write_barrier_ptr(p, *(void **) ref, false);
    } else {
        memmove(p, ref, l->element_size);
    }
}

uint64_t rt_vec_length(n_vec_t *l) {
    assert(l);

    return l->length;
}

uint64_t rt_vec_capacity(n_vec_t *l) {
    return l->capacity;
}

void *rt_vec_ref(n_vec_t *l) {
    return l->data;
}

void rt_vec_grow(n_vec_t *vec, int64_t element_hash, int custom_capacity) {
    assert(element_hash);
    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype);
    rti_vec_grow(vec, element_rtype, custom_capacity);
}

/**
 * ref 指向 element value 值所在的地址，其可能是一个栈地址，也可能是一个堆地址
 * @param vec
 * @param ref
 */
void rt_vec_push(n_vec_t *vec, int64_t element_hash, void *ref) {
    assert(element_hash);

    assert(ref > 0 && "ref must be a valid address");

    DEBUGF("[rt_vec_push] vec=%p, ref=%p, hash=%ld, element_size=%ld, len=%ld, cap=%ld", vec, ref, vec->hash,
           vec->element_size, vec->length, vec->capacity);

    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype);

    // TODO debug 验证 gc 问题
    if (span_of((addr_t) vec) == NULL || vec->element_size <= 0) {
        n_processor_t *p = processor_get();
        coroutine_t *co = coroutine_get();
        assertf(false,
                "vec_push failed, p_index=%d(%lu), p_status=%d, co=%p vec=%p element_size=%lu must be a valid hash",
                p->index, (uint64_t) p->thread_id, p->status, co, vec, vec->element_size);
    }

    DEBUGF("[vec_push] vec=%p,data=%p, current_length=%lu, value_ref=%p, value_data(uint64)=%0lx", vec, vec->data,
           vec->length, ref,
           (uint64_t) fetch_int_value((addr_t) ref, 8));

    if (vec->length == vec->capacity) {
        DEBUGF("[vec_push] current len=%lu equals cap, trigger grow, next capacity=%lu", vec->length,
               vec->capacity * 2);
        rti_vec_grow(vec, element_rtype, 0);
    }

    uint64_t index = vec->length++;
    rti_vec_assign(vec, index, ref);
}

/**
 * 共享 array_data
 * @param rtype_hash
 * @param l
 * @param start 起始 index [start, end)
 * @param end 结束 index, end 如果 == -1 则解析为 len()
 * @return
 */
n_vec_t *rt_vec_slice(n_vec_t *l, int64_t start, int64_t end) {
    if (end == -1) {
        end = l->length;
    }

    // start end 检测
    if (start > l->length || end > l->length || start < 0 || end < 0) {
        char *msg = tlsprintf("slice [%d:%d] out of vec with length %d", start, end, l->length);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rti_throw(msg, true);
        return 0;
    }

    if (start > end) {
        char *msg = tlsprintf("invalid index values, must be low %d <= high %d", start, end);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rti_throw(msg, true);
        return 0;
    }

    DEBUGF("[vec_slice] rtype_hash=%lu, element_size=%lu, start=%lu, end=%lu", l->hash,
           l->element_size, start, end);
    int64_t length = end - start;

    n_vec_t *sliced_vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    sliced_vec->capacity = length;
    sliced_vec->length = length;
    sliced_vec->hash = l->hash;
    sliced_vec->element_size = l->element_size;

    sliced_vec->data = l->data + start * l->element_size;
    rti_write_barrier_ptr(&sliced_vec->data, l->data + start * l->element_size, false);

    DEBUGF("[rt_vec_slice] old %p, new %p", l, sliced_vec);
    return sliced_vec;
}


void rt_vec_append(n_vec_t *dst, n_vec_t *src, int64_t element_hash) {
    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype);

    // assert(dst->ele_rhash == src->ele_rhash && "The types of the two vecs are different");
    if (dst->length + src->length > dst->capacity) {
        rti_vec_grow(dst, element_rtype, dst->length + src->length + 1);
    }

    memmove(dst->data + dst->length * dst->element_size, src->data, src->length * src->element_size);
    dst->length += src->length;
}

/**
 * 不影响原来的 vec，而是返回一个新的 vec
 * @param rtype_hash
 * @param a
 * @param b
 * @return
 */
n_vec_t *rt_vec_concat(n_vec_t *a, n_vec_t *b, int64_t element_hash) {
    assert(element_hash);
    DEBUGF("[vec_concat] rtype_hash=%lu, a=%p, b=%p", a->hash, a, b);

    int64_t length = a->length + b->length;
    n_vec_t *merged = rt_vec_cap(a->hash, element_hash, length);
    merged->length = length;
    DEBUGF("[vec_concat] a->len=%lu, b->len=%lu", a->length, b->length);

    // 合并 a
    void *dst = merged->data;
    memmove(dst, a->data, a->length * a->element_size);

    // 合并 b
    dst = merged->data + (a->length * a->element_size);
    memmove(dst, b->data, b->length * a->element_size);

    return merged;
}

n_anyptr_t rt_vec_element_addr(n_vec_t *l, uint64_t index) {
    assert(l);

    DEBUGF("[rt_vec_element_addr] l=%p, element_size=%lu, index=%lu, length=%ld", l, l->element_size, index,
           l->length);

    if (index >= l->length) {
        char *msg = tlsprintf("index out of vec [%d] with length %d", index, l->length);
        DEBUGF("[runtime.rt_vec_element_addr] has err %s", msg);
        rti_throw(msg, true);
        return 0;
    }

    // 计算 offset
    uint64_t offset = l->element_size * index; // (size unit byte) * index

    DEBUGF("[rt_vec_element_addr] l->data=%p, offset=%lu, result=%p", l->data, offset, (l->data + offset));
    return (n_anyptr_t) l->data + offset;
}

n_anyptr_t rt_vec_iterator(n_vec_t *l, int64_t element_hash) {
    assert(element_hash);
    rtype_t *element_rtype = rt_find_rtype(element_hash);
    assert(element_rtype);

    if (l->length == l->capacity) {
        DEBUGF("[rt_vec_iterator] current_length=%lu == capacity, trigger grow, next capacity=%lu", l->length,
               l->capacity * 2);
        rti_vec_grow(l, element_rtype, 0);
    }
    uint64_t index = l->length++;

    DEBUGF("[rt_vec_iterator] l=%p, element_size=%lu, index=%lu", l, l->element_size, index);

    n_anyptr_t addr = rt_vec_element_addr(l, index);
    DEBUGF("[rt_vec_iterator] addr=%lx", addr);
    return addr;
}

n_vec_t *rti_vec_new(rtype_t *element_rtype, int64_t length, int64_t capacity) {
    if (capacity == 0) {
        if (length > 0) {
            capacity = length;
        } else {
            capacity = VEC_DEFAULT_CAPACITY;
        }
    }

    assert(vec_rtype.size == sizeof(n_vec_t));
    assert(capacity >= length && "capacity must be greater than length");
    assert(element_rtype && "ele_rtype is empty");

    // 申请 vec 空间
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->element_size = rtype_stack_size(element_rtype, POINTER_SIZE);
    vec->hash = vec_rtype.hash;

    void *data = rti_array_new(element_rtype, capacity);
    rti_write_barrier_ptr(&vec->data, data, false);

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_size=%lu", vec, vec->data, vec->element_size);
    return vec;
}

/**
 * 类似 Go 的copy 内置函数，返回实际复制的元素数量
 * @param dst 目标 vec（必须预先分配空间）
 * @param src 源 vec
 * @return 实际复制的元素数量（取 dst 剩余空间和src长度的最小值）
 */
uint64_t rt_vec_copy(n_vec_t *dst, n_vec_t *src) {
    uint64_t copy_len = src->length < dst->length ? src->length : dst->length;

    if (copy_len > 0) {
        memmove(dst->data, src->data, copy_len * src->element_size);
    }

    DEBUGF("[rt_vec_copy] copied %lu elements from %p to %p", copy_len, src, dst);
    return copy_len;
}