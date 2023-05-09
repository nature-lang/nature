#include "bitmap.h"
#include "helper.h"

bitmap_t *bitmap_new(uint64_t size) {
    bitmap_t *b = NEW(bitmap_t);

    b->bits = mallocz(size);
    b->size = size;

    return b;
}

void bitmap_free(bitmap_t *b) {
    free(b->bits);
    free(b);
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
    char *result = malloc(count);
    result[count - 1] = '\0';
    for (int i = 0; i < count; ++i) {
        if(bitmap_test(bits, i)) {
            result[i] = '1';
        } else {
            result[i] = '0';
        };
    }
    return result;
}


