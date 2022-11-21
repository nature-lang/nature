#include "test.h"
#include <stdio.h>
#include "runtime/builtin/builtin.h"
#include "runtime/type/string.h"
#include "runtime/type/array.h"
#include "runtime/type/any.h"

static void test_basic() {
    array_t *args = array_new(3, 8); // any_t
    // 参数 1
    char *str_raw = "hello";
    string_t *str = string_new((uint8_t *) str_raw, 5);
    any_t *str_a = any_new(TYPE_STRING, str);
    array_push(args, str_a);

    // 参数 2
    char *str_raw1 = "world";
    string_t *str1 = string_new((uint8_t *) str_raw1, 5);
    any_t *str_b = any_new(TYPE_STRING, str1);
    array_push(args, str_b);

    // 参数 3
//    double value = 11.55;
//    any_t *float_value = any_new(TYPE_FLOAT, (void *) value);
//    array_push(args, float_value);

    print(args);
}


int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_basic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}