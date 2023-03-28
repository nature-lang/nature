#include "ct_list.h"
#include "assertf.h"

void ct_list_grow(list_t *l) {
    l->capacity = l->capacity * 2;
    l->take = realloc(l->take, l->capacity * l->element_size);
}

list_t *ct_list_new(uint64_t element_size) {
    list_t *list = malloc(sizeof(list_t));
    list->element_size = element_size;
    list->length = 0;
    list->capacity = LIST_DEFAULT_CAPACITY;
    list->take = malloc(list->capacity * element_size);

    return list;
}

// 返回 push 后的堆内存地址
void *ct_list_push(list_t *l, void *src) {
    if (l->length == l->capacity) {
        ct_list_grow(l);
    }

    uint64_t index = l->length++;
    uint8_t *dst = ct_list_value(l, index);
    memmove(dst, src, l->element_size);
    return dst;
}

// 返回的一定是一个指针数据，指针指向堆内存中的地址
void *ct_list_value(list_t *l, uint64_t index) {
    assertf(index <= l->length - 1, "index out of range [%d] with length %d", l->length, index);

    uint64_t offset = index * l->element_size;
    return l->take + offset;
}
