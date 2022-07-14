#include "test.h"
#include <stdio.h>
#include "runtime/type/string.h"
#include "runtime/type/type_debug.h"
#include "src/lib/list.h"
#include "src/lir/lir.h"
#include "src/assembler/amd64/asm.h"
#include "src/assembler/amd64/register.h"

static void test_string() {
    void *point = gen_hello_world();
    string_t *type_point = string_new(point, 11);
    void *addr = string_addr(type_point);
    int len = string_length(type_point);
    printf("%s: %d", addr, len);
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_string),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}