#include "test.h"
#include <stdio.h>
#include "utils/bitmap.h"

int setup(void **state) {
    return 0;
}

int teardown(void **state) {
    return 0;
}

static void test_bitmap_base() {
    bitmap_t *b = bitmap_new(16);
    assert_int_equal(b->size, 16);
    assert(b->bits);

    bitmap_set(b->bits, 7);
    assert_int_equal(bitmap_test(b->bits, 7), 1);
    assert_int_equal(bitmap_set_count(b), 1);

    bitmap_set(b->bits, 11);
    assert_int_equal(bitmap_test(b->bits, 11), 1);
    assert_int_equal(bitmap_set_count(b), 2);

    bitmap_clear(b->bits, 7);
    assert_int_equal(bitmap_test(b->bits, 11), 1);
    assert_int_equal(bitmap_test(b->bits, 7), 0);
    assert_int_equal(bitmap_set_count(b), 1);
//    printf("hello\n");
}

int main(void) {
    test_bitmap_base();
}