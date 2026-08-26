#include "test.h"

#include "process.h"
#include "runtime/processor.h"

#include <stdatomic.h>
#include <stdlib.h>

enum {
    PROCESSOR_TEST_WAITER_RAN = 1,
    PROCESSOR_TEST_OTHER_RAN = 2,
    PROCESSOR_TEST_WAITER_SAW_FINISHED = 4,
    PROCESSOR_TEST_BUSY_FINISHED = 8,
};

static _Atomic int64_t processor_test_state;
static _Atomic int64_t processor_test_runtime_busy_ms;
static _Atomic int64_t processor_test_runtime_request;

static void __attribute__((noinline)) test_sleep_yield() {
    char *str = "sleep wait gc";
    char *str2 = "sleep wait gc";
    char *str3 = "sleep wait gc";
    DEBUGF("sleep wait gc completed")
    rt_coroutine_sleep(1000);
    DEBUGF("sleep completed")
}

static void __attribute__((noinline)) test_nest1() {
    int a = 12;
    int b = 24;
    int c = 32;
    void *d = malloc(10);
    test_sleep_yield();
}

void __attribute__((noinline)) test_gc_sleep_yield() {
    int a = 12;
    int b = 24;
    int c = 32;
    void *d = malloc(10);
    test_nest1();
    int e[12] = {0};
    free(d);
}

void init_safepoint(int64_t v) {
    tls_test_value = v;
}

int64_t get_safepoint() {
    return tls_test_value;
}

void test_processor_safepoint_reset() {
    atomic_store_explicit(&processor_test_state, 0, memory_order_release);
    atomic_store_explicit(&processor_test_runtime_request, -1, memory_order_release);
}

static int64_t test_processor_state_snapshot() {
    return atomic_load_explicit(&processor_test_state, memory_order_acquire) &
           (PROCESSOR_TEST_WAITER_RAN | PROCESSOR_TEST_OTHER_RAN |
            PROCESSOR_TEST_WAITER_SAW_FINISHED);
}

int64_t test_processor_busy_no_safepoint(int64_t milliseconds) {
    uint64_t duration = (uint64_t) milliseconds * 1000 * 1000;
    uint64_t started_at = uv_hrtime();
    while (uv_hrtime() - started_at < duration) {
        // This C loop deliberately contains no Nature function-entry
        // safepoint. A pending request must remain cooperative until return.
    }

    int64_t state = test_processor_state_snapshot();
    atomic_fetch_or_explicit(&processor_test_state, PROCESSOR_TEST_BUSY_FINISHED,
                             memory_order_release);
    return state;
}

void test_processor_mark_waiter() {
    int64_t state = atomic_load_explicit(&processor_test_state, memory_order_acquire);
    int64_t flags = PROCESSOR_TEST_WAITER_RAN;
    if (state & PROCESSOR_TEST_BUSY_FINISHED) {
        flags |= PROCESSOR_TEST_WAITER_SAW_FINISHED;
    }
    atomic_fetch_or_explicit(&processor_test_state, flags, memory_order_release);
}

void test_processor_mark_other() {
    atomic_fetch_or_explicit(&processor_test_state, PROCESSOR_TEST_OTHER_RAN,
                             memory_order_release);
}

int64_t test_processor_safepoint_state() {
    return test_processor_state_snapshot();
}

int64_t test_processor_count() {
    return cpu_count;
}

int64_t test_processor_current_safepoint_request() {
    return (int64_t) tls_safepoint;
}

int64_t test_processor_current_need_stw() {
    n_processor_t *p = processor_get();
    assert(p);
    return (int64_t) atomic_load_explicit(&p->need_stw, memory_order_acquire);
}

static void test_processor_runtime_busy() {
    int64_t milliseconds = atomic_load_explicit(&processor_test_runtime_busy_ms, memory_order_acquire);
    uint64_t duration = (uint64_t) milliseconds * 1000 * 1000;
    uint64_t started_at = uv_hrtime();
    while (uv_hrtime() - started_at < duration) {
    }
    atomic_store_explicit(&processor_test_runtime_request,
                          (int64_t) tls_safepoint,
                          memory_order_release);
}

void test_processor_dispatch_runtime_busy(int64_t milliseconds) {
    atomic_store_explicit(&processor_test_runtime_busy_ms, milliseconds, memory_order_release);
    coroutine_t *co = rt_coroutine_new((void *) test_processor_runtime_busy,
                                       FLAG(CO_FLAG_RTFN) | FLAG(CO_FLAG_DIRECT) | FLAG(CO_FLAG_SAME),
                                       NULL, NULL);
    rt_coroutine_dispatch(co);
}

int64_t test_processor_runtime_busy_request() {
    return atomic_load_explicit(&processor_test_runtime_request, memory_order_acquire);
}

void test_arm64_abi_draw_line_ex(vector2_t v1, vector2_t v2) {
    printf("v1 %f, %f\n", v1.x, v1.y);
    printf("v2 %f, %f\n", v1.x, v2.y);
}
