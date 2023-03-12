#ifndef NATURE_ARRAY_H
#define NATURE_ARRAY_H

#include <stdint.h>

#define MIN_CAPACITY 8

typedef struct {
    int count;
    int capacity;
    int size; // item size, 单位 byte
    uint8_t *data; // void 类型的指针数组， sizeof(void) == 1
} array_t;

type_array_t *array_new(int capacity, int size);

void *array_value(type_array_t *a, int index);

void array_push(type_array_t *a, void *value);

void array_concat(type_array_t *a, type_array_t *b);

type_array_t *array_slice(type_array_t *a, int start, int end);

#endif //NATURE_ARRAY_H
