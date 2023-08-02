#include "test.h"
#include <stdio.h>
#include "utils/slice.h"


static void test_slice_base() {
    slice_t *list = slice_new();
    char *a = "hello world!!!";

    slice_push(list, a);

    char *first = list->take[0];
    printf("%s", first);
}

int main(void) {
    test_slice_base();
}