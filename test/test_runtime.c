#include "test.h"
#include <stdio.h>
#include "runtime/type/string.h"
#include "runtime/type/type_debug.h"
#include "runtime/builtin/builtin.h"
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

static void test_builtin_print() {
    void *point = gen_hello_world();
    string_t *type_point = string_new(point, 11);
    bool a = false;
    builtin_operand_t *operand2 = builtin_new_operand(TYPE_BOOL, &a);
    builtin_print(1, operand2);
}

int main(void) {
    const struct CMUnitTest tests[] = {
            cmocka_unit_test(test_string),
            cmocka_unit_test(test_builtin_print),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}