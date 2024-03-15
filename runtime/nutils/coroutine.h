#ifndef NATURE_NUTILS_COROUTINE_H
#define NATURE_NUTILS_COROUTINE_H

#include "runtime/runtime.h"

typedef void (*co_fn)();

typedef enum {
    CO_FLAG_SOLO = 1, // 独享线程模式
} lir_flag_t;

coroutine_t *coroutine_create(void *fn, uint64_t flag);

coroutine_t *rt_coroutine_async(void *fn, uint64_t flag);

void coroutine_sleep(int64_t ms);

void coroutine_yield();

// ------------ libuv 的一些回调 -----------------------
static void uv_on_timer(uv_timer_t *timer);

#endif // NATURE_COROUTINE_H
