#include <stdio.h>

#include "runtime/processor.h"
#include "runtime/rt_mutex.h"
#include "test_runtime.h"
#include "utils/assertf.h"

rt_mutex_t m;
int64_t sum = 0;

static void sub_sum_fn() {
    coroutine_t *co = coroutine_get();
    for (int i = 0; i < 100; i++) {
        rt_mutex_lock(&m);
        sum += 1;
        rt_mutex_unlock(&m);
        co_yield_runnable(co->p, co);
    }
}

void test_sum() {
    rt_mutex_init(&m);
    TDEBUGF("[test_sum] start");

    for (int i = 0; i < 1000; ++i) {
        coroutine_t *sub_co = rt_coroutine_new((void *) sub_sum_fn, 0, 0);
        rt_coroutine_dispatch(sub_co);
    }

    coroutine_sleep(1000);
    TDEBUGF("[test_sum] sum=%ld", sum);
    assert(sum == 100000);
}


static void sub_fn() {
    TDEBUGF("[sub_fn] wait lock")
    rt_mutex_lock(&m);
    TDEBUGF("[sub_fn] get lock success, hello world")

    TDEBUGF("[sub_fn] will unlock")
    rt_mutex_unlock(&m);
}

void test_basic() {
    rt_mutex_init(&m);
    rt_mutex_lock(&m);


    // 创建 sub_co 同样尝试获取锁获取锁获取锁获取锁
    coroutine_t *sub_co = rt_coroutine_new((void *) sub_fn, 0, 0);
    rt_coroutine_dispatch(sub_co);


    TDEBUGF("[test_slow] will yield sleep")
    coroutine_sleep(2000);

    TDEBUGF("[test_slow] sleep come back")
    rt_mutex_unlock(&m);
    TDEBUGF("[test_slow] unlock success, and sleep while")
    coroutine_sleep(200);
}

int main(void) {
    test_runtime_main(test_sum);
}