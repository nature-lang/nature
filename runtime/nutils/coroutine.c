#include "coroutine.h"

#include "runtime/processor.h"

coroutine_t *n_coroutine_create(co_fn fn, n_vec_t *args, uint64_t flag) {
    bool solo = FLAG(CO_FLAG_SOLO) & flag;
    return coroutine_new(fn, args, solo);
}

coroutine_t *n_coroutine_run(co_fn fn, n_vec_t *args, uint64_t flag) {
    coroutine_t *co = n_coroutine_create(fn, args, flag);

    coroutine_dispatch(co);

    return co;
}

void n_coroutine_yield() {
    coroutine_yield(CO_STATUS_RUNNABLE);
}
