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

void bitmap_free(bitmap_t *b);

/**
 * index 从 0 开始计数
 * @param bits
 * @param index
 */
void bitmap_set(uint8_t *bits, int index);

void bitmap_clear(uint8_t *bits, int index);

bool bitmap_test(uint8_t *bits, int index);

int bitmap_set_count(bitmap_t *b);

#endif //NATURE_BITMAP_H
