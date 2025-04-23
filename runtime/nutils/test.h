#ifndef NATURE_RUNTIME_NUTILS_TEST_H_
#define NATURE_RUNTIME_NUTILS_TEST_H_

#include <stdlib.h>
#include <stdint.h>

_Thread_local int64_t tls_safepoint = 0;

void test_gc_sleep_yield();

void init_safepoint(int64_t v);

int64_t get_safepoint();

#endif //NATURE_RUNTIME_NUTILS_TEST_H_
