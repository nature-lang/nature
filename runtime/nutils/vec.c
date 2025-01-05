#include "vec.h"

#include "array.h"
#include "runtime/runtime.h"

static void rt_vec_grow(n_vec_t *vec, int custom_capacity) {
    PRE_RTCALL_HOOK();

    if (custom_capacity) {
        vec->capacity = custom_capacity;
    } else if (vec->capacity > 0) {
        vec->capacity = vec->capacity * 2;
    } else {
        vec->capacity = VEC_DEFAULT_CAPACITY;
    }

    rtype_t *element_rtype = rt_find_rtype(vec->ele_rhash);
    assertf(element_rtype, "cannot find element_rtype with hash");

    n_array_t *new_data = rti_array_new(element_rtype, vec->capacity);

    uint64_t element_size = rtype_stack_size(element_rtype, POINTER_SIZE);

    DEBUGF("[rt_vec_grow] old_vec=%p, len=%lu, cap=%lu, new_vec=%p, element_size=%lu", vec, vec->length, vec->capacity,
           new_data,
           element_size);

    if (vec->length > 0) {
        memmove(new_data, vec->data, vec->length * element_size);
    }

    vec->data = new_data;
}

/**
 * [string] 对于这样的声明，现在默认其 element 元素是存储在堆上的
 * @param rhash
 * @param ele_rhash
 * @param length vec 大小，允许为 0，当 capacity = -1 时，使用 default_capacity
 * @return
 */
n_vec_t *rt_vec_new(int64_t rhash, int64_t ele_rhash, int64_t length, int64_t capacity) {
    PRE_RTCALL_HOOK();

    DEBUGF("[rt_vec_new] r_hash=%lu,e_hash=%lu,len=%lu,cap=%lu", rhash, ele_rhash, length, capacity);

    assertf(rhash > 0, "rhash must be a valid hash");
    assertf(ele_rhash > 0, "ele_rhash must be a valid hash");

    // capacity 需要大于等于 length
    if (capacity < length) {
        capacity = length;
    }

    if (capacity == -1) {
        capacity = VEC_DEFAULT_CAPACITY;
    }
    if (length == -1) {
        length = 0;
    }

    assert(capacity >= length && "capacity must be greater than length");
    TRACEF("[rt_vec_new] length=%lu, capacity=%lu", length, capacity);

    rtype_t *element_rtype = rt_find_rtype(ele_rhash);
    assert(element_rtype && "cannot find element_rtype with hash");

    // - 进行内存申请,申请回来一段内存是 memory_vec_t 大小的内存, memory_vec_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 vec_data + 1 时会按照 sizeof(memory_vec_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->ele_rhash = ele_rhash;
    vec->rhash = rhash;
    if (capacity > 0) {
        vec->data = rti_array_new(element_rtype, capacity);
    }

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_rtype_hash=%lu", vec, vec->data, vec->ele_rhash);
    return vec;
}

/**
 * @param l
 * @param index
 * @param value_ref
 */
void rt_vec_access(n_vec_t *l, uint64_t index, void *value_ref) {
    PRE_RTCALL_HOOK();

    if (index >= l->length) {
        char *msg = dsprintf("index out of range [%d] with length %d", index, l->length);
        DEBUGF("[runtime.rt_vec_access] has err %s", msg);
        rt_throw(msg, true);

        return;
    }

    uint64_t element_size = rt_rtype_out_size(l->ele_rhash);
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
void rt_vec_assign(n_vec_t *l, uint64_t index, void *ref) {
    PRE_RTCALL_HOOK();

    // assert(index <= l->length - 1 && "index out of range [%d] with length %d", index, l->length);
    assert(index <= l->length - 1 && "index out of range"); // TODO runtime 错误提示优化

    rtype_t *element_rtype = rt_find_rtype(l->ele_rhash);
    uint64_t element_size = rtype_stack_size(element_rtype, POINTER_SIZE);
    DEBUGF("[runtime.rt_vec_assign] element_size=%lu", element_size);
    // 计算 offset
    uint64_t offset = rtype_stack_size(element_rtype, POINTER_SIZE) * index; // (size unit byte) * index
    void *p = l->data + offset;
    memmove(p, ref, element_size);
}

uint64_t rt_vec_length(n_vec_t *l) {
    PRE_RTCALL_HOOK();
    assert(l);

    return l->length;
}

uint64_t rt_vec_capacity(n_vec_t *l) {
    PRE_RTCALL_HOOK();

    return l->capacity;
}

void *rt_vec_ref(n_vec_t *l) {
    PRE_RTCALL_HOOK();

    return l->data;
}

/**
 * ref 指向 element value 值所在的地址，其可能是一个栈地址，也可能是一个堆地址
 * @param vec
 * @param ref
 */
void rt_vec_push(n_vec_t *vec, void *ref) {
    PRE_RTCALL_HOOK();

    assert(ref > 0 && "ref must be a valid address");

    DEBUGF("[rt_vec_push] vec=%p, ref=%p, hash=%ld, ele_hash=%ld, len=%ld, cap=%ld", vec, ref, vec->rhash,
           vec->ele_rhash, vec->length, vec->capacity);

    // TODO debug 验证 gc 问题
    if (span_of((addr_t) vec) == NULL || vec->ele_rhash <= 0) {
        n_processor_t *p = processor_get();
        coroutine_t *co = coroutine_get();
        assertf(false,
                "vec_push failed, p_index_%d=%d(%lu), p_status=%d, co=%p vec=%p ele_rtype_hash=%lu must be a valid hash",
                p->share,
                p->index, (uint64_t) p->thread_id, p->status, co, vec, vec->ele_rhash);
    }

    DEBUGF("[vec_push] vec=%p,data=%p, current_length=%lu, value_ref=%p, value_data(uint64)=%0lx", vec, vec->data,
           vec->length, ref,
           (uint64_t) fetch_int_value((addr_t) ref, 8));

    if (vec->length == vec->capacity) {
        DEBUGF("[vec_push] current len=%lu equals cap, trigger grow, next capacity=%lu", vec->length,
               vec->capacity * 2);
        rt_vec_grow(vec, 0);
    }

    uint64_t index = vec->length++;
    rt_vec_assign(vec, index, ref);
}

/**
 * 共享 array_data
 * @param rtype_hash
 * @param l
 * @param start 起始 index [start, end)
 * @param end 结束 index
 * @return
 */
n_vec_t *rt_vec_slice(n_vec_t *l, int64_t start, int64_t end) {
    PRE_RTCALL_HOOK();

    // start end 检测
    if (start >= l->length || end > l->length || start < 0 || end < 0) {
        char *msg = dsprintf("slice [%d:%d] out of vec with length %d", start, end, l->length);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rt_throw(msg, true);
        return 0;
    }

    if (start > end) {
        char *msg = dsprintf("invalid index values, must be low %d <= high %d", start, end);
        DEBUGF("[runtime.vec_slice] has err %s", msg);
        rt_throw(msg, true);
        return 0;
    }

    DEBUGF("[vec_slice] rtype_hash=%lu, element_rtype_hash=%lu, start=%lu, end=%lu", l->rhash,
           l->ele_rhash, start, end);
    int64_t length = end - start;

    n_vec_t *sliced_vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    sliced_vec->capacity = length;
    sliced_vec->length = length;
    sliced_vec->ele_rhash = l->ele_rhash;
    sliced_vec->rhash = l->rhash;

    int64_t element_size = rt_rtype_out_size(l->ele_rhash);
    sliced_vec->data = l->data + start * element_size;

    return sliced_vec;
}


void rt_vec_append(n_vec_t *dst, n_vec_t *src) {
    PRE_RTCALL_HOOK();
    // assert(dst->ele_rhash == src->ele_rhash && "The types of the two vecs are different");
    if (dst->length + src->length > dst->capacity) {
        rt_vec_grow(dst, dst->length + src->length + 1);
    }
    int64_t element_size = rt_rtype_out_size(src->ele_rhash);
    memmove(dst->data + dst->length * element_size, src->data, src->length * element_size);
    dst->length += src->length;
}

/**
 * 不影响原来的 vec，而是返回一个新的 vec
 * @param rtype_hash
 * @param a
 * @param b
 * @return
 */
n_vec_t *rt_vec_concat(n_vec_t *a, n_vec_t *b) {
    PRE_RTCALL_HOOK();

    DEBUGF("[vec_concat] rtype_hash=%lu, a=%p, b=%p", a->rhash, a, b);
    assert(a->ele_rhash == b->ele_rhash && "The types of the two vecs are different");
    int64_t element_size = rt_rtype_out_size(a->ele_rhash);
    int64_t length = a->length + b->length;
    n_vec_t *merged = rt_vec_new(a->rhash, a->ele_rhash, length, length);
    DEBUGF("[vec_concat] a->len=%lu, b->len=%lu", a->length, b->length);

    // 合并 a
    void *dst = merged->data;
    memmove(dst, a->data, a->length * element_size);

    // 合并 b
    dst = merged->data + (a->length * element_size);
    memmove(dst, b->data, b->length * element_size);

    return merged;
}

n_void_ptr_t rt_vec_element_addr(n_vec_t *l, uint64_t index) {
    PRE_RTCALL_HOOK();

    assert(l);

    DEBUGF("[rt_vec_element_addr] l=%p, element_rtype_hash=%lu, index=%lu, length=%ld", l, l->ele_rhash, index,
           l->length);

    if (index >= l->length) {
        char *msg = dsprintf("index out of vec [%d] with length %d", index, l->length);
        DEBUGF("[runtime.rt_vec_element_addr] has err %s", msg);
        rt_throw(msg, true);
        return 0;
    }

    uint64_t element_size = rt_rtype_out_size(l->ele_rhash);
    // 计算 offset
    uint64_t offset = element_size * index; // (size unit byte) * index

    DEBUGF("[rt_vec_element_addr] l->data=%p, offset=%lu, result=%p", l->data, offset, (l->data + offset));
    return (n_void_ptr_t) l->data + offset;
}

n_void_ptr_t rt_vec_iterator(n_vec_t *l) {
    PRE_RTCALL_HOOK();

    if (l->length == l->capacity) {
        DEBUGF("[rt_vec_iterator] current_length=%lu == capacity, trigger grow, next capacity=%lu", l->length,
               l->capacity * 2);
        rt_vec_grow(l, 0);
    }
    uint64_t index = l->length++;

    DEBUGF("[rt_vec_iterator] l=%p, element_rtype_hash=%lu, index=%lu", l, l->ele_rhash, index);

    n_void_ptr_t addr = rt_vec_element_addr(l, index);
    DEBUGF("[rt_vec_iterator] addr=%lx", addr);
    return addr;
}

n_vec_t *rti_vec_new(rtype_t *ele_rtype, int64_t length, int64_t capacity) {
    if (capacity == 0) {
        if (length > 0) {
            capacity = length;
        } else {
            capacity = VEC_DEFAULT_CAPACITY;
        }
    }

    assert(vec_rtype.size == sizeof(n_vec_t));
    assert(capacity >= length && "capacity must be greater than length");
    assert(ele_rtype && "ele_rtype is empty");

    // 申请 vec 空间
    n_vec_t *vec = rti_gc_malloc(vec_rtype.size, &vec_rtype);
    vec->capacity = capacity;
    vec->length = length;
    vec->ele_rhash = ele_rtype->hash;
    vec->rhash = vec_rtype.hash;

    void *data = rti_array_new(ele_rtype, capacity);
    write_barrier(&vec->data, &data);

    DEBUGF("[rt_vec_new] success, vec=%p, data=%p, element_rtype_hash=%lu", vec, vec->data, vec->ele_rhash);
    return vec;
}
