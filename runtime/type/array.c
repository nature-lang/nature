#include "array.h"
#include <stdlib.h>
#include <stdio.h>

void *array_new(int count, uint8_t size) {
    int capacity = count;
    if (capacity < count) {
        capacity = count;
    }
    if (capacity == 0) {
        capacity = 8;
    }

    array_t *a = malloc(sizeof(array_t));
    a->count = count; // 当前时间数组的容量,
    a->capacity = capacity; // 当前实际申请的内存空间
    a->item_size = size;
    a->data = malloc(capacity * size);
    return a;
}

uint8_t *array_value(array_t *a, int index) {
    // 判断是否越界
    if (index + 1 > a->count) {
        printf("index out of range [%d] with length %d\n", index, a->count);
        exit(1);
    }

    uint8_t *p = a->data;
    p += a->item_size * index;
    return p;
}

void array_push(array_t *a, void *value) {
    
}
