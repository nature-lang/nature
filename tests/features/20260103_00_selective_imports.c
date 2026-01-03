#include "tests/test.h"
#include "utils/assertf.h"
#include "utils/exec.h"
#include <stdio.h>

static void test_basic() {
    char *raw = exec_output();
    printf("%s", raw);

    // Should print output from selective imports
    assert_string_equal(raw, "sqrt(16) = 4.000000\npow(2, 3) = 8.000000\nadd(5, 7) = 12\npi = 3.141593\nPoint: 10 , 20\nMAX_SIZE: 100\nutils.MIN_SIZE: 10\nformat_string: hello\n");
}

int main(void) {
    TEST_BASIC
}
