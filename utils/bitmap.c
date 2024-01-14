#include "bitmap.h"

#include "helper.h"

// size 的单位是 byte
bitmap_t *bitmap_new(uint64_t size) {
    bitmap_t *b = NEW(bitmap_t);

    b->bits = mallocz(size);
    b->size = size;
    b->locker = mutex_new();

    return b;
}

void bitmap_free(bitmap_t *b) {
    free(b->bits);
    free(b);
}

void bitmap_locker_set(bitmap_t *b, uint64_t index) {
    mutex_lock(b->locker);
    bitmap_set(b->bits, index);
    mutex_unlock(b->locker);
}

void bitmap_locker_clear(bitmap_t *b, uint64_t index) {
    mutex_lock(b->locker);
    bitmap_clear(b->bits, index);
    mutex_unlock(b->locker);
}

void bitmap_set(uint8_t *bits, uint64_t index) {
    // index & 7 等于 index % 8
    bits[index / 8] |= 1 << (index & 7);
}

void bitmap_clear(uint8_t *bits, uint64_t index) {
    // ~ 表示二进制取反
    bits[index / 8] &= ~(1 << (index & 7));
}

bool bitmap_test(uint8_t *bits, uint64_t index) {
    return bits[index / 8] & (1 << (index & 7)) ? true : false;
}

int bitmap_set_count(bitmap_t *b) {
    int count = 0;
    for (uint64_t i = 0; i < b->size; ++i) {
        if (bitmap_test(b->bits, i)) {
            count++;
        }
    }

    return count;
}

char *bitmap_to_str(uint8_t *bits, uint64_t count) {
    return ""; // TODO
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
void bitmap_grow_set(bitmap_t *b, uint64_t index, bool test) {
    uint16_t need_size = index / 8;
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
