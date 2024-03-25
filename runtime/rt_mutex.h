#ifndef NATURE_RT_MUTEX_H
#define NATURE_RT_MUTEX_H

#include "rt_linked.h"
#include "utils/mutex.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MUTEX_LOCKED 1  // (1 << 0) // 1
#define MUTEX_WOKEN 2   // 1 << 1 // 2
#define MUTEX_STARVING 4// 1 << 2 // 4
#define MUTEX_WAITER_SHIFT 3
#define MUTEX_STARVING_THRESHOLD_NS 10000000

#define ACTIVE_SPIN 4
#define ACTIVE_SPIN_COUNT 30

typedef struct {
    int64_t state;
    int64_t sema;
    rt_linked_t waiters;
    int64_t waiter_count;
} rt_mutex_t;

void rt_mutex_init(rt_mutex_t *m);

void rt_mutex_lock(rt_mutex_t *m);

void rt_mutex_unlock(rt_mutex_t *m);

bool rt_can_spin(int64_t count);

void rt_mutex_waiter_acquire(rt_mutex_t *m, bool to_head);

/**
 * 当 handoff = true 时，release 的 coroutine 将会被放在 runnable list 的最前面
 * 并直接 yield 当前 coroutine, 让 release coroutine 能够快速获取锁并工作
 * @param m
 * @param handoff
 */
void rt_mutex_waiter_release(rt_mutex_t *m, bool handoff);

void rt_do_spin();

#endif//NATURE_RT_MUTEX_H
