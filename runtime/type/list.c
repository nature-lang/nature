#include "list.h"
#include "utils/links.h"
#include "runtime/allocator.h"
#include "array.h"

void list_grow(memory_list_t *l) {
    l->capacity = l->capacity * 2;
    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_index);
    memory_array_t *new_array_data = array_new(element_rtype, l->capacity);
    memmove(new_array_data, l->array_data, l->capacity * rtype_heap_out_size(element_rtype));
    l->array_data = new_array_data;
}

/**
 * [string] 对于这样的声明，现在默认其 element 元素是存储在堆上的
 * @param rtype_index
 * @param element_rtype_index
 * @param capacity list 大小，允许为 0，当 capacity = 0 时，使用 default_capacity
 * @return
 */
memory_list_t *list_new(uint64_t rtype_index, uint64_t element_rtype_index, uint64_t capacity) {
    if (!capacity) {
        capacity = LIST_DEFAULT_CAPACITY;
    }

    // find rtype and element_rtype
    rtype_t *list_rtype = rt_find_rtype(rtype_index);
    rtype_t *element_rtype = rt_find_rtype(element_rtype_index);

    // 创建 array data
    memory_array_t *array_data = array_new(element_rtype, capacity);

    // - 进行内存申请,申请回来一段内存是 memory_list_t 大小的内存, memory_list_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 list_data + 1 时会按照 sizeof(memory_list_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    memory_list_t *list_data = runtime_malloc(list_rtype->size, list_rtype);
    list_data->capacity = capacity;
    list_data->length = 0;
    list_data->element_rtype_index = element_rtype->index;
    list_data->array_data = array_data;

    return list_data;
}

/**
 * 其返回值在 value_ref 中， value_ref 是数组中存放的值，而不是存放的值的起始地址
 * 考虑到未来可能会有超过 8byte 大小的值，操作 lir move 困难，所以在 runtime 中进行 move
 * @param l
 * @param index
 * @param value_ref
 */
void list_access(memory_list_t *l, uint64_t index, void *value_ref) {
    assertf(index < l->length, "index out of range [%d] with length %d", l->length, index);

    uint64_t element_size = rt_rtype_heap_out_size(l->element_rtype_index);
    // 计算 offset
    uint64_t offset = element_size * index; // (size unit byte) * index
    memmove(value_ref, l->array_data + offset, element_size);
}

/**
 * index 必须在 length 范围内
 * @param l
 * @param index
 * @param ref
 * @return
 */
void list_assign(memory_list_t *l, uint64_t index, void *ref) {
    assertf(index <= l->length - 1, "index out of range [%d] with length %d", l->length, index);

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_index);
    uint64_t element_size = rtype_heap_out_size(element_rtype);
    // 计算 offset
    uint64_t offset = rtype_heap_out_size(element_rtype) * index; // (size unit byte) * index
    void *p = l->array_data + offset;
    memmove(p, ref, element_size);
}

uint64_t list_length(memory_list_t *l) {
    return l->length;
}

/**
 * ref 指向 element value 值所在的地址，其可能是一个栈地址，也可能是一个堆地址
 * @param l
 * @param ref
 */
void list_push(memory_list_t *l, void *ref) {
    if (l->length == l->capacity) {
        list_grow(l);
    }

    uint64_t index = l->length++;
    list_assign(l, index, ref);
}

/**
 * 不影响原有的的 list，而是返回一个 slice 之后的新的 list
 * @param rtype_index
 * @param l
 * @param start
 * @param end
 * @return
 */
memory_list_t *list_slice(uint64_t rtype_index, memory_list_t *l, uint64_t start, uint64_t end) {
    uint64_t capacity = end - start;
    uint64_t element_size = rt_rtype_heap_out_size(l->element_rtype_index);
    memory_list_t *sliced_list = list_new(rtype_index, l->element_rtype_index, capacity);

    void *src = l->array_data + start * element_size;

    // memmove
    memmove(sliced_list->array_data, src, element_size * capacity);
    sliced_list->length = capacity;

    return sliced_list;
}

memory_list_t *list_concat(uint64_t rtype_index, memory_list_t *a, memory_list_t *b) {
    assertf(a->element_rtype_index == b->element_rtype_index, "The types of the two lists are different");
    uint64_t element_size = rt_rtype_heap_out_size(a->element_rtype_index);
    uint64_t capacity = a->length + b->length;
    memory_list_t *merged = list_new(rtype_index, a->element_rtype_index, capacity);

    // 合并 a
    void *dst = merged->array_data + (merged->length - 1 * element_size);
    memmove(dst, a->array_data, a->length * element_size);
    merged->length + a->length;

    // 合并 b
    dst = merged->array_data + (merged->length - 1 * element_size);
    memmove(dst, b->array_data, b->length * element_size);
    merged->length + a->length;

    return merged;
}
