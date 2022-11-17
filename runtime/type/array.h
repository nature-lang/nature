#ifndef NATURE_ARRAY_H
#define NATURE_ARRAY_H

#include <stdint.h>

#define MIN_CAPACITY 8

typedef struct {
    int count;
    int capacity;
    int size; // item size, 单位 byte
    void *data;
} array_t;

array_t * array_new(int count, int size);

void * array_value(array_t *a, int index);

void array_push(array_t *a, void *value);

void array_concat(array_t *a, array_t *b);

array_t *array_slice(array_t *a, int start, int end);

#endif //NATURE_ARRAY_H
