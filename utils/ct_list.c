#include "ct_list.h"
#include "assertf.h"

void ct_list_grow(list_t *l) {
    l->capacity = l->capacity * 2;
    l->take = realloc(l->take, l->capacity * l->element_size);
}

list_t *ct_list_new(uint64_t element_size) {
    list_t *list = mallocz(sizeof(list_t));
    list->element_size = element_size;
    list->length = 0;
    list->capacity = DEFAULT_LIST_CAPACITY;
    list->take = mallocz(list->capacity * element_size);

    return list;
}

void *ct_list_push(list_t *l, void *src) {
    if (l->length == l->capacity) {
        ct_list_grow(l);
    }

    uint64_t index = l->length++;
    byte *dst = ct_list_value(l, index);
    memmove(dst, src, l->element_size);
    return dst;
}

void *ct_list_value(list_t *l, uint64_t index) {
    assertf(index <= l->length - 1, "index out of range [%d] with length %d", l->length, index);

    uint64_t offset = index * l->element_size;
    return l->take + offset;
}
