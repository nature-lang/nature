#include <stdio.h>
#include <stdatomic.h>

#include "runtime/processor.h"
#include "runtime/rt_mutex.h"
#include "test_runtime.h"
#include "utils/assertf.h"

rt_mutex_t m = {0};
int64_t sum = 0;
int64_t sum_no_lock = 0;

static void sub_sum_fn() {
    coroutine_t *co = coroutine_get();
    for (int i = 0; i < 100; i++) {
        rt_mutex_lock(&m);
//        sum = sum + 1;
        int64_t old = sum;
        if (!atomic_compare_exchange_strong(&sum, &old, old + 1)) {
            assertf(false, "existential race, sum=%ld, old=%ld", sum, old);
        }

        rt_mutex_unlock(&m);
        co_yield_runnable(co->p, co);
    }
}

static void sub_sum_no_lock_fn() {
    for (int i = 0; i < 100; i++) {
//        sum_no_lock += 1;
        int64_t old = sum_no_lock;
        atomic_compare_exchange_strong(&sum_no_lock, &old, old + 1);
    }
}

void test_lock_sum() {
    TESTDUMP("[test_lock_sum] start");
    memset(&m, 0, sizeof(rt_mutex_t));

    for (int i = 0; i < 10000; ++i) {
        coroutine_t *sub_co = rt_coroutine_new((void *) sub_sum_fn, 0, 0);
        rt_coroutine_dispatch(sub_co);
    }


    // 等待所有的子任务完成
    coroutine_sleep(5000);

    uint64_t remain_count = 0;
    PROCESSOR_FOR(share_processor_list) {
        // 1 标识 main 协程自身
        remain_count += p->runnable_list.count;
    }

    TESTDUMP("[test_lock_sum] processor remain_count=%lu", remain_count)


    TESTDUMP("[test_lock_sum] sum=%ld", sum);
    assert(sum == 1000000);
}

void test_no_lock_sum() {
    TESTDUMP("[test_no_lock_sum] start");

    for (int i = 0; i < 10000; ++i) {
        coroutine_t *sub_co = rt_coroutine_new((void *) sub_sum_no_lock_fn, 0, 0);
        rt_coroutine_dispatch(sub_co);
    }

    coroutine_sleep(2000);
    TESTDUMP("[test_no_lock_sum] no_lock_sum=%ld", sum_no_lock);
    assert(sum_no_lock < 1000000);
}

static void sub_fn() {
    TESTDUMP("[sub_fn] wait lock")
    rt_mutex_lock(&m);
    TESTDUMP("[sub_fn] get lock success, hello world")

    TESTDUMP("[sub_fn] will unlock")
    rt_mutex_unlock(&m);
}

void test_basic() {
    rt_mutex_lock(&m);


    // 创建 sub_co 同样尝试获取锁获取锁获取锁获取锁
    coroutine_t *sub_co = rt_coroutine_new((void *) sub_fn, 0, 0);
    rt_coroutine_dispatch(sub_co);


    TESTDUMP("[test_slow] will yield sleep")
    coroutine_sleep(2000);

    TESTDUMP("[test_slow] sleep come back")
    rt_mutex_unlock(&m);
    TESTDUMP("[test_slow] unlock success, and sleep while")
    coroutine_sleep(200);
}

int main(void) {
    test_runtime_init();
    test_runtime_main(test_no_lock_sum);

    test_runtime_main(test_lock_sum);
}