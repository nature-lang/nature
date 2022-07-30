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

#endif //NATURE_ARRAY_H
