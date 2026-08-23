#include "tests/test.h"

int main(void) {
    alarm(10);
    TEST_EXEC_IMM
    alarm(0);
}
