#include "list.h"
#include "utils/links.h"
#include "runtime/allocator.h"
#include "array.h"

void list_grow(memory_list_t *l) {
    l->capacity = l->capacity * 2;
    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_index);
    memory_array_t new_array_data = array_new(element_rtype, l->capacity);
    memmove(new_array_data, l->array_data, l->capacity * element_rtype->size);
    l->array_data = new_array_data;
}

/**
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
    memory_array_t array_data = array_new(element_rtype, capacity);

    // - 进行内存申请,申请回来一段内存是 memory_list_t 大小的内存, memory_list_* 就是限定这一片内存区域的结构体表示
    // 虽然数组也这么表示，但是数组本质上只是利用了 list_data + 1 时会按照 sizeof(memory_list_t) 大小的内存区域移动
    // 的技巧而已，所以这里要和数组结构做一个区分
    memory_list_t *list_data = (memory_list_t *) runtime_malloc(list_rtype->size, list_rtype);
    list_data->capacity = capacity;
    list_data->length = 0;
    list_data->element_rtype_index = element_rtype->index;
    list_data->array_data = array_data;

    return list_data;
}

void *list_value(memory_list_t *l, uint64_t index) {
    assertf(index <= l->length - 1, "index out of range [%d] with length %d", l->length, index);

    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_index);
    // 计算 offset
    uint64_t offset = element_rtype->size * index; // (size unit byte) * index
    return l->array_data + offset;
}

uint64_t list_length(memory_list_t *l) {
    return l->length;
}

void list_push(memory_list_t *l, void *ref) {
    if (l->length == l->capacity) {
        list_grow(l);
    }

    rtype_t *element_type = rt_find_rtype(l->element_rtype_index);

    uint64_t index = l->length++;
    byte *value = list_value(l, index);
    // 内存移动操作
    memmove(value, ref, element_type->index);
}

memory_list_t *list_slice(uint64_t rtype_index, memory_list_t *l, uint64_t start, uint64_t end) {
    uint64_t capacity = end - start;
    rtype_t *element_rtype = rt_find_rtype(l->element_rtype_index);
    memory_list_t *sliced_list = list_new(rtype_index, l->element_rtype_index, capacity);

    void *src = list_value(l, start);

    // memmove
    memmove(sliced_list->array_data, src, element_rtype->size * capacity);
    sliced_list->length = capacity;

    return sliced_list;
}

memory_list_t *list_concat(uint64_t rtype_index, memory_list_t *a, memory_list_t *b) {
    assertf(a->element_rtype_index == b->element_rtype_index, "The types of the two lists are different");
    rtype_t *element_rtype = rt_find_rtype(a->element_rtype_index);
    uint64_t capacity = a->length + b->length;
    memory_list_t *concat_list = list_new(rtype_index, a->element_rtype_index, capacity);

    // 合并 a 到
    void *dst = list_value(concat_list, concat_list->length - 1);
    void *src = list_value(a, 0);
    memmove(dst, src, a->length * element_rtype->size);
    concat_list->length + a->length;

    // 合并 b
    dst = list_value(concat_list, concat_list->length - 1);
    src = list_value(b, 0);
    memmove(dst, src, b->length * element_rtype->size);
    concat_list->length += b->length;

    return concat_list;
}
