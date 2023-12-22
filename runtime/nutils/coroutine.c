#include "coroutine.h"

#include "runtime/processor.h"

coroutine_t *coroutine_create(void *fn, n_vec_t *args, uint64_t flag) {
    bool solo = FLAG(CO_FLAG_SOLO) & flag;
    return coroutine_new(fn, args, solo, false);
}

coroutine_t *coroutine_run(void *fn, n_vec_t *args, uint64_t flag) {
    coroutine_t *co = coroutine_create(fn, args, flag);

    coroutine_dispatch(co);

    return co;
}

void coroutine_yield() {
    coroutine_yield_with_status(CO_STATUS_RUNNABLE);
}
