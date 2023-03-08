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

bitmap_t *bitmap_clear(bitmap_t *bitmap, int index);

bool bitmap_test(bitmap_t *bitmap, int index);

/**
 * uint32_t bits_index = nr / 8;
 * uint8_t uint8_index = nr % 8;
 * @param bits
 * @param index
 * @return
 */
bool bitmap_unsafe_test(uint8_t *bits, int index);


#endif //NATURE_BITMAP_H
