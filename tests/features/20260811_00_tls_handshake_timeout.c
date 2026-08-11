#include "tests/test.h"
#include <time.h>

static void test_basic() {
#ifdef __WINDOWS
    return;
#else
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    alarm(5);
    char *raw = exec_output();
    alarm(0);
    clock_gettime(CLOCK_MONOTONIC, &end);

    int64_t elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                         (end.tv_nsec - start.tv_nsec) / 1000000;
    assert_string_equal(raw, "handshake timeout\n");
    assertf(elapsed_ms < 1000, "TLS handshake timeout took %ldms", elapsed_ms);
#endif
}

int main(void) {
    TEST_BASIC
}
