#include "rt_mutex.h"
#include "runtime/processor.h"
#include "runtime/runtime.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <uv.h>

static bool can_semacquire(int64_t *addr) {
    while (true) {
        int64_t v = atomic_load(addr);
        if (v == 0) {
            return false;
        }

        if (atomic_compare_exchange_strong(addr, &v, v - 1)) {
            return true;
        }
    }
}

void rt_mutex_init(rt_mutex_t *m) {
    m->state = 0;
    m->sema = 0;
    m->waiter_count = ATOMIC_VAR_INIT(0);
    rt_linked_fixalloc_init(&m->waiters);
}

void rt_mutex_lock(rt_mutex_t *m) {
    int64_t expected = 0;// starving = 0 and locked=0
    if (atomic_compare_exchange_strong(&m->state, &expected, MUTEX_LOCKED)) {
        return;
    }

    uint64_t wait_start_time = 0;
    int64_t iter = 0;
    bool starving = false;
    bool awoke = false;
    int64_t old = atomic_load(&m->state);
    while (true) {

        // can_spin 的条件非常苛刻
        if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) == MUTEX_LOCKED && rt_can_spin(iter)) {
            if (!awoke && (old & MUTEX_WOKEN) == 0 &&
                (old >> MUTEX_WAITER_SHIFT) != 0 &&// 如果 waiter list 中存在被阻塞的 coroutine
                atomic_compare_exchange_strong(&m->state, &old, old | MUTEX_WOKEN)) {
                awoke = true;
            }

            rt_do_spin();
            iter++;
            old = atomic_load(&m->state);
            continue;
        }

        int64_t new = old;

        // 1. old 处于饥饿模式
        // 2. old 在非饥饿模式且未上锁
        // 3. 不满足自旋条件, 可能已上锁

        // 只要在非饥饿模式下就可以抢锁，不管是否已经上锁, 下面的抢 waiter 排除了已经上锁的情况
        if ((old & MUTEX_STARVING) == 0) {
            new |= MUTEX_LOCKED;
        }

        // 已经加锁或者处于饥饿模式，则尝试抢 waiter
        if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) != 0) {
            new += 1 << MUTEX_WAITER_SHIFT;
        }


        // 如果是 unlock 状态，就说明没有等待中的 coroutine, 此时不需要切换到饥饿模式
        if (starving && (old & MUTEX_LOCKED) != 0) {
            new |= MUTEX_STARVING;
        }

        if (awoke) {
            assert((new & MUTEX_WOKEN) != 0 && "inconsistent mutex state");

            new = new & ~MUTEX_WOKEN;
        }

        if (atomic_compare_exchange_strong(&m->state, &old, new)) {
            if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) == 0) {
                DEBUGF("[rt_mutex_lock] m=%p, state=%lu, quick lock success", m, m->state);
                return;
            }

            // old 是 locked 或者 starving 的情况下，抢 waiter 成功，现在将推送 coroutine 到阻塞队列中
            bool to_head = wait_start_time != 0;
            if (wait_start_time == 0) {
                wait_start_time = uv_hrtime();
            }

            rt_mutex_waiter_acquire(m, to_head);

            // 更新 starving
            starving = starving || uv_hrtime() - wait_start_time > MUTEX_STARVING_THRESHOLD_NS;

            // 非饥饿模式下 coroutine 被唤醒, 进行竞争模式
            old = atomic_load(&m->state);
            if ((old & MUTEX_STARVING) == 0) {
                awoke = true;
                iter = 0;
                continue;
            }

            // 饥饿模式被唤醒, 当前 coroutine 已经获取锁的所有权
            // 额外处理一下饥饿模式的退出即可

            // unlock 已经明确解锁并交接给了当前 co, 所以此时不可能处于上锁状态
            if ((old & (MUTEX_LOCKED | MUTEX_WOKEN)) != 0 ||
                old >> MUTEX_WAITER_SHIFT == 0) {
                assert(false && "inconsistent mutex state");
            }

            // 加锁并减少一个 waiter
            int64_t delta = MUTEX_LOCKED - (1 << MUTEX_WAITER_SHIFT);
            // 如果从当前 coroutine 开始未进入到饥饿模式，则当前 coroutine 后面等待的 coroutine 是更晚进入的，更加不会是饥饿的
            if (!starving || (old >> MUTEX_WAITER_SHIFT == 1)) {
                delta -= MUTEX_STARVING;// 退出饥饿模式
            }

            // 饥饿模式下有序排队，不存在竞争
            atomic_add_int64(&m->state, delta);
            return;
        } else {
            // 存在竞争，重新获取并计算状态
            old = atomic_load(&m->state);
        }
    }
}

void rt_mutex_unlock(rt_mutex_t *m) {
    int64_t new = atomic_add_int64(&m->state, -MUTEX_LOCKED);
    if (new == 0) {
        return;
    }

    // 1. 存在 woken
    // 2. 存在 starving
    // 3. 存在 waiter
    // slow unlock

    if (((new + MUTEX_LOCKED) & MUTEX_LOCKED) == 0) {
        assert(false && "unlock of unlocked mutex");
    }


    if ((new & MUTEX_STARVING) == 0) {
        int64_t old = new;
        if ((old >> MUTEX_WAITER_SHIFT) == 0 || (old & (MUTEX_LOCKED | MUTEX_WOKEN | MUTEX_STARVING)) != 0) {
            return;
        }

        new = (old - (1 << MUTEX_WAITER_SHIFT)) | MUTEX_WOKEN;

        if (atomic_compare_exchange_strong(&m->state, &old, new)) {
            rt_mutex_waiter_release(m, false);
            return;
        }

        old = atomic_load(&m->state);
    } else {
        // 饥饿模式：将互斥锁的所有权交给下一个等待者，并放弃
        // 我们的时间片，以便下一个等待者可以立即开始运行。
        // 注意：mutexLocked 没有被设置，等待者会在唤醒后设置它。
        // 但是如果 mutexStarving 被设置，互斥锁仍然被认为是锁定的，
        // 所以新来的 goroutine 不会获取它。

        rt_mutex_waiter_release(m, true);
    }
}

bool rt_can_spin(int64_t iter) {
    if (iter >= ACTIVE_SPIN || cpu_count <= 1) {
        return false;
    }

    processor_t *p = processor_get();
    if (!rt_linked_fixalloc_empty(&p->runnable_list)) {
        return false;
    }

    return true;
}

void rt_do_spin() {
    for (int i = 0; i < ACTIVE_SPIN_COUNT; i++) {
        sched_yield();
    }
}

void rt_mutex_waiter_acquire(rt_mutex_t *m, bool to_head) {
    DEBUGF("[rt_mutex_waiter_acquire] m=%p, state=%lu, waiter_count=%lu, to_heap=%d", m, m->state, m->waiter_count,
           to_head);
    // 直接截获了 release 释放的信号量，不需要进入等待队列，直接返回
    if (can_semacquire(&m->sema)) {
        return;
    }

    coroutine_t *co = coroutine_get();

    // 未获取到信号，将当前 coroutine 加入到阻塞队列中, 并 yield 当前协程， 等待信号唤醒
    while (true) {
        mutex_lock(&m->waiters.locker);
        atomic_add_int64(&m->waiter_count, 1);
        if (can_semacquire(&m->sema)) {
            atomic_add_int64(&m->waiter_count, -1);
            mutex_unlock(&m->waiters.locker);
            break;
        }
        mutex_unlock(&m->waiters.locker);

        if (to_head) {
            rt_linked_fixalloc_push_heap(&m->waiters, co);
        } else {
            rt_linked_fixalloc_push(&m->waiters, co);
        }


        co_yield_waiting(co->p, co);

        // 再次竞争信号量
        if (can_semacquire(&m->sema)) {
            break;
        }
    }
}

void rt_mutex_waiter_release(rt_mutex_t *m, bool handoff) {
    // 添加一个释放信号, 但是没有直接激活, 而是等待 wait_co 唤醒后自己获取信号
    atomic_add_int64(&m->sema, 1);

    if (atomic_load(&m->waiter_count) == 0) {
        // 无锁状态下 sema 被抢占消费
        return;
    }

    // > 0 开始加锁进行准确判断
    mutex_lock(&m->waiters.locker);

    // 加锁期间，waiter_count 可能被其他线程消费
    if (atomic_load(&m->waiter_count) == 0) {
        // consumed by another
        mutex_unlock(&m->waiters.locker);
        return;
    }

    assert(m->waiter_count == m->waiters.count);
    atomic_add_int64(&m->waiter_count, -1);
    mutex_unlock(&m->waiters.locker);

    // head pop
    coroutine_t *wait_co = rt_linked_fixalloc_pop(&m->waiters);
    assert(wait_co);

    processor_t *p = wait_co->p;

    co_set_status(p, wait_co, CO_STATUS_RUNNABLE);

    // handoff 下排队到最前面
    if (handoff) {
        rt_linked_fixalloc_push_heap(&p->runnable_list, wait_co);
    } else {
        rt_linked_fixalloc_push(&p->runnable_list, wait_co);
    }

    // 直接让出自己的执行权利，并将让 runnable list 立刻执行
    coroutine_t *co = coroutine_get();
    if (handoff && co->p == p) {
        co_yield_runnable(co->p, co);
    }
}

/**
 * add int64 必定会更新成功，因为其不停的更新 old 的值并进行更新，而在乎具体的顺序
 * @param state
 * @param delta
 * @return
 */
int64_t atomic_add_int64(int64_t *state, int64_t delta) {
    int64_t old = atomic_load(state);
    int64_t n = old + delta;
    while (!atomic_compare_exchange_strong(state, &old, n)) {
        old = atomic_load(state);
        n = old + delta;
    }

    return n;
}
