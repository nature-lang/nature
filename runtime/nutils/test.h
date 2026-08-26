#ifndef NATURE_RUNTIME_NUTILS_TEST_H_
#define NATURE_RUNTIME_NUTILS_TEST_H_

#include <stdint.h>
#include <stdlib.h>

_Thread_local int64_t tls_safepoint = 0;

void test_gc_sleep_yield();

void init_safepoint(int64_t v);

int64_t get_safepoint();

void test_processor_safepoint_reset();

int64_t test_processor_busy_no_safepoint(int64_t milliseconds);

void test_processor_mark_waiter();

void test_processor_mark_other();

int64_t test_processor_safepoint_state();

int64_t test_processor_count();

int64_t test_processor_current_safepoint_request();

int64_t test_processor_current_global_safepoint();

void test_processor_dispatch_runtime_busy(int64_t milliseconds);

int64_t test_processor_runtime_busy_request();

typedef struct {
    float x;
    float y;
} vector2_t;

void test_arm64_abi_draw_line_ex(vector2_t v1, vector2_t v2);

#endif //NATURE_RUNTIME_NUTILS_TEST_H_
