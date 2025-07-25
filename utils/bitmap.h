#ifndef NATURE_UTILS_BITMAP_H
#define NATURE_UTILS_BITMAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "mutex.h"

typedef struct {
    uint8_t *bits; // 每一个元素有 8 bit
    uint64_t size; // 位图的大小, 单位 bit
    mutex_t *locker; // TODO 优化锁的粒度
} bitmap_t;


// size 的单位是 byte
static inline bitmap_t *bitmap_new(uint64_t size) {
    bitmap_t *b = NEW(bitmap_t);

    b->bits = mallocz(size);
    b->size = size;
    b->locker = mutex_new(false);

    return b;
}

static inline void bitmap_free(bitmap_t *b) {
    free(b->bits);
    free(b);
}


static inline void bitmap_set(uint8_t *bits, uint64_t index) {
    // index & 7 等于 index % 8
    bits[index / 8] |= 1 << (index & 7);
}


static inline void bitmap_batch_clear(uint8_t *bits, uint64_t index, uint64_t clear_count) {
    if (clear_count == 0) {
        return;
    }
    
    uint64_t end_index = index + clear_count;
    uint64_t current_index = index;
    
    while (current_index < end_index) {
        uint64_t byte_index = current_index / 8;
        uint8_t bit_offset = current_index & 7;
        
        // 计算当前字节中需要清除的位数
        uint8_t bits_in_current_byte = 8 - bit_offset;
        uint64_t remaining_bits = end_index - current_index;
        uint8_t bits_to_clear = (bits_in_current_byte < remaining_bits) ? bits_in_current_byte : remaining_bits;
        
        // 创建掩码
        uint8_t mask = ((1 << bits_to_clear) - 1) << bit_offset;
        
        // 清除位
        bits[byte_index] &= ~mask;
        
        current_index += bits_to_clear;
    }
}

static inline void bitmap_batch_set(uint8_t *bits, uint64_t index, uint64_t set_count) {
    if (set_count == 0) {
        return;
    }

    uint64_t end_index = index + set_count;
    uint64_t current_index = index;

    while (current_index < end_index) {
        uint64_t byte_index = current_index / 8;
        uint8_t bit_offset = current_index & 7;

        // 计算当前字节中需要设置的位数
        uint8_t bits_in_current_byte = 8 - bit_offset;
        uint64_t remaining_bits = end_index - current_index;
        uint8_t bits_to_set = (bits_in_current_byte < remaining_bits) ? bits_in_current_byte : remaining_bits;

        // 创建掩码
        uint8_t mask = ((1 << bits_to_set) - 1) << bit_offset;

        // 设置位
        bits[byte_index] |= mask;

        current_index += bits_to_set;
    }
}

static inline void bitmap_clear(uint8_t *bits, uint64_t index) {
    // ~ 表示二进制取反
    bits[index / 8] &= ~(1 << (index & 7));
}

static inline void bitmap_locker_set(bitmap_t *b, uint64_t index) {
    mutex_lock(b->locker);
    bitmap_set(b->bits, index);
    mutex_unlock(b->locker);
}

static inline void bitmap_locker_clear(bitmap_t *b, uint64_t index) {
    mutex_lock(b->locker);
    bitmap_clear(b->bits, index);
    mutex_unlock(b->locker);
}

static inline bool bitmap_test(uint8_t *bits, uint64_t index) {
    return bits[index / 8] & (1 << (index & 7)) ? true : false;
}

static inline bool bitmap_empty(uint8_t *bits, uint64_t count) {
    // 计算需要检查的完整字节数
    uint64_t bytes = count / 8;
    // 计算剩余的位数
    uint8_t remainder = count % 8;

    // 首先检查完整字节
    for (uint64_t i = 0; i < bytes; i++) {
        if (bits[i] != 0) {
            return false;
        }
    }

    // 检查剩余的位
    if (remainder > 0) {
        // 创建掩码，只检查有效的位
        uint8_t mask = (1 << remainder) - 1;
        if ((bits[bytes] & mask) != 0) {
            return false;
        }
    }

    return true;
}

static inline int bitmap_set_count(bitmap_t *b) {
    int count = 0;
    for (uint64_t i = 0; i < b->size; ++i) {
        if (bitmap_test(b->bits, i)) {
            count++;
        }
    }

    return count;
}

static inline char *bitmap_to_str(uint8_t *bits, uint64_t count) {
    char *result = mallocz(count + 1);
    for (int i = 0; i < count; ++i) {
        if (bitmap_test(bits, i)) {
            result[i] = '1';
        } else {
            result[i] = '0';
        };
    }
    result[count] = '\0';
    return result;
}

// 如果 index 超过了
static inline void bitmap_grow_set(bitmap_t *b, uint64_t index, bool test) {
    uint64_t need_size = index / 8;
    if (need_size > b->size) {
        uint8_t *new_bits = mallocz(need_size * 2);
        memcpy(new_bits, b->bits, b->size);
        free(b->bits);
        b->bits = new_bits;
        b->size = need_size * 2;
    }

    if (test) {
        bitmap_set(b->bits, index);
    } else {
        bitmap_clear(b->bits, index);
    }
}

/**
 * 将 src 中的数组 copy 到 dst 中
 * @param dst
 * @param src
 */
static inline void bitmap_copy(bitmap_t *dst, bitmap_t *src) {
    mutex_lock(src->locker);
    mutex_lock(dst->locker);

    assert(dst->size == src->size);

    // signl safe memmove(3) Added in POSIX.1-2008 TC2
    memmove(dst->bits, src->bits, src->size);

    mutex_unlock(src->locker);
    mutex_unlock(dst->locker);
}


#endif // NATURE_BITMAP_H
