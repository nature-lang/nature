#ifndef NATURE_ARRAY_H
#define NATURE_ARRAY_H

#include <stdint.h>

#define MIN_CAPACITY 8

typedef struct {
    int count;
    int capacity;
    uint8_t item_size;
    uint8_t *data;
} array_t;

void *array_new(int count, uint8_t size);

uint8_t *array_value(array_t *a, int index);

void array_push(array_t *a, void *value);

array_t *array_concat(array_t *a, array_t *b);

array_t *array_slice(array_t *a, int start, int end);

#endif //NATURE_ARRAY_H
