#include "list.h"
#include "utils/custom_links.h"
#include "runtime/memory.h"
#include "runtime/processor.h"
#include "array.h"
#include "struct.h"

void list_grow(n_list_t *l) {
    l->capacity = l->capacity * 2;
    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_hash);
    n_array_t *new_array_data = rt_array_new(element_rtype, l->capacity);
    memmove(new_array_data, l->data, l->capacity * rtype_out_size(element_rtype, POINTER_SIZE));
    l->data = new_array_data;
}

/**
 * [string] 对于这样的声明，现在默认其 element 元素是存储在堆上的
 * @param rtype_hash
 * @param element_rtype_hash
 * @param length list 大小，允许为 0，当 capacity = 0 时，使用 default_capacity
 * @return
 */
n_list_t *list_new(uint64_t rtype_hash, uint64_t element_rtype_hash, uint64_t length, uint64_t capacity) {
    DEBUGF("[list_new] rtype_hash=%lu, element_hash=%lu, length=%lu, capacity=%lu", rtype_hash, element_rtype_hash,
           length, capacity);

    if (capacity == 0) {
        if (length > 0) {
            capacity = length;
        } else {
            capacity = LIST_DEFAULT_CAPACITY;
        }
    }

    assertf(capacity >= length, "capacity must be greater than length");
    DEBUGF("[runtime.list_new] length=%lu, capacity=%lu", length, capacity)

    // find rtype and element_rtype
    rtype_t *list_rtype = rt_find_rtype(rtype_hash);
    assertf(list_rtype, "cannot find rtype with hash %lu", rtype_hash);

    rtype_t *element_rtype = rt_find_rtype(element_rtype_hash);
    assertf(element_rtype, "cannot find element_rtype with hash %lu", element_rtype_hash);

    // 创建 array data
    n_array_t *data = rt_array_new(element_rtype, capacity);

    // - 进行内存申请,申请回来一段内存是 memory_list_t 大小的内存, memory_list_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 list_data + 1 时会按照 sizeof(memory_list_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    n_list_t *list_data = runtime_malloc(list_rtype->size, list_rtype);
    list_data->capacity = capacity;
    list_data->length = length;
    list_data->element_rtype_hash = element_rtype_hash;
    list_data->data = data;

    return list_data;
}

/**
 * 其返回值在 value_ref 中， value_ref 是数组中存放的值，而不是存放的值的起始地址
 * 考虑到未来可能会有超过 8byte 大小的值，操作 lir move 困难，所以在 runtime 中进行 move
 * @param l
 * @param index
 * @param value_ref
 */
void list_access(n_list_t *l, uint64_t index, void *value_ref) {
    if (index >= l->length) {
        char *msg = dsprintf("index out of range [%d] with length %d", index, l->length);
        DEBUGF("[runtime.list_access] has err %s", msg);
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
void list_assign(n_list_t *l, uint64_t index, void *ref) {
    assertf(index <= l->length - 1, "index out of range [%d] with length %d", index, l->length);

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_hash);
    uint64_t element_size = rtype_out_size(element_rtype, POINTER_SIZE);
    // 计算 offset
    uint64_t offset = rtype_out_size(element_rtype, POINTER_SIZE) * index; // (size unit byte) * index
    void *p = l->data + offset;
    memmove(p, ref, element_size);
}

uint64_t list_length(n_list_t *l) {
    return l->length;
}

uint64_t list_capacity(n_list_t *l) {
    return l->capacity;
}

void *list_raw(n_list_t *l) {
    return l->data;
}

/**
 * ref 指向 element value 值所在的地址，其可能是一个栈地址，也可能是一个堆地址
 * @param l
 * @param ref
 */
void list_push(n_list_t *l, void *ref) {
//    DEBUGF("[list_push] current_length=%lu", l->length);

    if (l->length == l->capacity) {
        DEBUGF("[list_push] current_length=%lu == capacity, trigger grow, next capacity=%lu",
               l->length,
               l->capacity * 2);
        list_grow(l);
    }

    uint64_t index = l->length++;
    list_assign(l, index, ref);
}

/**
 * 不影响原有的的 list，而是返回一个 slice 之后的新的 list
 * @param rtype_hash
 * @param l
 * @param start
 * @param end
 * @return
 */
n_list_t *list_slice(uint64_t rtype_hash, n_list_t *l, uint64_t start, uint64_t end) {
    DEBUGF("[list_slice] rtype_hash=%lu, start=%lu, end=%lu", rtype_hash, start, end);
    uint64_t length = end - start;
    uint64_t element_size = rt_rtype_out_size(l->element_rtype_hash);
    n_list_t *sliced_list = list_new(rtype_hash, l->element_rtype_hash, length, length);

    void *src = l->data + start * element_size;

    // memmove
    memmove(sliced_list->data, src, element_size * length);

    return sliced_list;
}

n_list_t *list_concat(uint64_t rtype_hash, n_list_t *a, n_list_t *b) {
    DEBUGF("[list_concat] rtype_hash=%lu, a=%p, b=%p", rtype_hash, a, b);
    assertf(a->element_rtype_hash == b->element_rtype_hash, "The types of the two lists are different");
    uint64_t element_size = rt_rtype_out_size(a->element_rtype_hash);
    uint64_t length = a->length + b->length;
    n_list_t *merged = list_new(rtype_hash, a->element_rtype_hash, length, length);

    // 合并 a
    void *dst = merged->data + (merged->length - 1 * element_size);
    memmove(dst, a->data, a->length * element_size);
    merged->length + a->length;

    // 合并 b
    dst = merged->data + (merged->length - 1 * element_size);
    memmove(dst, b->data, b->length * element_size);
    merged->length + a->length;

    return merged;
}






