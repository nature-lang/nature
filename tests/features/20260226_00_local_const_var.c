#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    printf("%s", raw);

    assert_string_equal(raw, "PUBLIC_VAR: 100\nPUBLIC_CONST: hello\npublic_fn: 84\nuse_private_type: 3\n");
}

int main(void) {
    TEST_BASIC
}
