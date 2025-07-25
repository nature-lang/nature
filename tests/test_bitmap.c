#include "test.h"
#include "utils/bitmap.h"
#include <stdio.h>

int setup(void **state) {
    return 0;
}

int teardown(void **state) {
    return 0;
}

static void test_bitmap_base() {
    bitmap_t *b = bitmap_new(16);
    assert_int_equal(b->size, 16);
    assert_true(b->bits);

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

static void test_bitmap_batch_set() {
    bitmap_t *b = bitmap_new(16);

    // Test batch set within single byte
    bitmap_batch_set(b->bits, 2, 3); // Set bits 2, 3, 4
    assert_int_equal(bitmap_test(b->bits, 2), 1);
    assert_int_equal(bitmap_test(b->bits, 3), 1);
    assert_int_equal(bitmap_test(b->bits, 4), 1);
    assert_int_equal(bitmap_test(b->bits, 1), 0);
    assert_int_equal(bitmap_test(b->bits, 5), 0);
    assert_int_equal(bitmap_set_count(b), 3);

    // Test batch set across multiple bytes
    bitmap_t *b2 = bitmap_new(16);
    bitmap_batch_set(b2->bits, 6, 4); // Set bits 6, 7, 8, 9
    assert_int_equal(bitmap_test(b2->bits, 6), 1);
    assert_int_equal(bitmap_test(b2->bits, 7), 1);
    assert_int_equal(bitmap_test(b2->bits, 8), 1);
    assert_int_equal(bitmap_test(b2->bits, 9), 1);
    assert_int_equal(bitmap_test(b2->bits, 5), 0);
    assert_int_equal(bitmap_test(b2->bits, 10), 0);
    assert_int_equal(bitmap_set_count(b2), 4);

    // Test batch set with count 0
    bitmap_t *b3 = bitmap_new(16);
    bitmap_batch_set(b3->bits, 5, 0);
    assert_int_equal(bitmap_set_count(b3), 0);

    // Test batch set entire byte
    bitmap_t *b4 = bitmap_new(16);
    bitmap_batch_set(b4->bits, 8, 8); // Set entire second byte
    for (int i = 8; i < 16; i++) {
        assert_int_equal(bitmap_test(b4->bits, i), 1);
    }
    for (int i = 0; i < 8; i++) {
        assert_int_equal(bitmap_test(b4->bits, i), 0);
    }
    assert_int_equal(bitmap_set_count(b4), 8);

    bitmap_free(b);
    bitmap_free(b2);
    bitmap_free(b3);
    bitmap_free(b4);
}

static void test_bitmap_batch_clear() {
    bitmap_t *b = bitmap_new(16);

    // First set all bits
    for (int i = 0; i < 16; i++) {
        bitmap_set(b->bits, i);
    }
    assert_int_equal(bitmap_set_count(b), 16);

    // Test batch clear within single byte
    bitmap_batch_clear(b->bits, 2, 3); // Clear bits 2, 3, 4
    assert_int_equal(bitmap_test(b->bits, 0), 1);
    assert_int_equal(bitmap_test(b->bits, 1), 1);
    assert_int_equal(bitmap_test(b->bits, 2), 0);
    assert_int_equal(bitmap_test(b->bits, 3), 0);
    assert_int_equal(bitmap_test(b->bits, 4), 0);
    assert_int_equal(bitmap_test(b->bits, 5), 1);
    assert_int_equal(bitmap_set_count(b), 13);

    // Test batch clear across multiple bytes
    bitmap_batch_clear(b->bits, 6, 4); // Clear bits 6, 7, 8, 9
    assert_int_equal(bitmap_test(b->bits, 0), 1);
    assert_int_equal(bitmap_test(b->bits, 1), 1);
    assert_int_equal(bitmap_test(b->bits, 2), 0);
    assert_int_equal(bitmap_test(b->bits, 3), 0);
    assert_int_equal(bitmap_test(b->bits, 4), 0);
    assert_int_equal(bitmap_test(b->bits, 5), 1);
    assert_int_equal(bitmap_test(b->bits, 6), 0);
    assert_int_equal(bitmap_test(b->bits, 7), 0);
    assert_int_equal(bitmap_test(b->bits, 8), 0);
    assert_int_equal(bitmap_test(b->bits, 9), 0);
    assert_int_equal(bitmap_test(b->bits, 5), 1);
    assert_int_equal(bitmap_test(b->bits, 10), 1);
    assert_int_equal(bitmap_set_count(b), 9);

    // Test batch clear with count 0
    int count_before = bitmap_set_count(b);
    bitmap_batch_clear(b->bits, 10, 0);
    assert_int_equal(bitmap_set_count(b), count_before);

    // Test batch clear remaining bits
    bitmap_batch_clear(b->bits, 0, 16); // Clear all bits
    for (int i = 0; i < 16; i++) {
        assert_int_equal(bitmap_test(b->bits, i), 0);
    }
    assert_int_equal(bitmap_set_count(b), 0);

    bitmap_free(b);
}

static void test_bitmap_batch_operations_combined() {
    bitmap_t *b = bitmap_new(16);

    // Set some bits using batch_set
    bitmap_batch_set(b->bits, 0, 8); // Set first byte
    bitmap_batch_set(b->bits, 12, 4); // Set bits 12-15 (still within size range)
    assert_int_equal(bitmap_set_count(b), 12);

    // Clear some bits using batch_clear
    bitmap_batch_clear(b->bits, 2, 4); // Clear bits 2-5
    assert_int_equal(bitmap_set_count(b), 8);

    // Verify specific bits
    assert_int_equal(bitmap_test(b->bits, 0), 1);
    assert_int_equal(bitmap_test(b->bits, 1), 1);
    assert_int_equal(bitmap_test(b->bits, 2), 0);
    assert_int_equal(bitmap_test(b->bits, 3), 0);
    assert_int_equal(bitmap_test(b->bits, 4), 0);
    assert_int_equal(bitmap_test(b->bits, 5), 0);
    assert_int_equal(bitmap_test(b->bits, 6), 1);
    assert_int_equal(bitmap_test(b->bits, 7), 1);
    assert_int_equal(bitmap_test(b->bits, 12), 1);
    assert_int_equal(bitmap_test(b->bits, 13), 1);
    assert_int_equal(bitmap_test(b->bits, 14), 1);
    assert_int_equal(bitmap_test(b->bits, 15), 1);

    bitmap_free(b);
}

int main(void) {
    test_bitmap_base();
    test_bitmap_batch_set();
    test_bitmap_batch_clear();
    test_bitmap_batch_operations_combined();
    printf("All bitmap batch tests passed!\n");
}