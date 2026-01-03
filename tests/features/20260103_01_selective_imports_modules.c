#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    printf("%s", raw);

    // Should print output from module-style selective imports
    assert_string_equal(raw, "add(5, 3) = 8\nsquare(4) = 16\nMATH_CONSTANT = 42\nPoint: x = 10 y = 20\nPoint2: x = 5 y = 15\nmath.multiply(3, 7) = 21\n");
}

int main(void) {
    TEST_BASIC
}
