#ifndef NATURE_NUTILS_COROUTINE_H
#define NATURE_NUTILS_COROUTINE_H

#include "runtime/basic.h"

typedef void (*co_fn)();

typedef enum {
    CO_FLAG_SOLO = 1, // 独享线程模式
} lir_flag_t;

coroutine_t *n_coroutine_create(co_fn fn, n_vec_t *args, uint64_t flag);

coroutine_t *n_coroutine_run(co_fn fn, n_vec_t *args, uint64_t flag);

void n_coroutine_yield();

#endif // NATURE_COROUTINE_H
