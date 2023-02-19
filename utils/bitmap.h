#ifndef NATURE_BITMAP_H
#define NATURE_BITMAP_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


typedef struct {
    uint8_t *bits; // 每一个元素有 8 bit
    int size; // 位图的大小, 单位 bit
} bitmap_t;

bitmap_t *bitmap_new(int size);

bitmap_t *bitmap_set(bitmap_t *bitmap, int index);

bool *bitmap_is(bitmap_t *bitmap, int index);


#endif //NATURE_BITMAP_H
