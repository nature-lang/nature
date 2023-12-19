#ifndef NATURE_NUTILS_COROUTINE_H
#define NATURE_NUTILS_COROUTINE_H

#include "runtime/basic.h"

typedef void (*co_fn)();

typedef enum {
    CO_FLAG_SOLO = 1, // 独享线程模式
} lir_flag_t;

coroutine_t *coroutine_create(void *fn, n_vec_t *args, uint64_t flag);

coroutine_t *coroutine_run(void *fn, n_vec_t *args, uint64_t flag);

void coroutine_yield();

#endif // NATURE_COROUTINE_H
