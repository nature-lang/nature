#include "rt_mutex.h"
#include "runtime/processor.h"
#include "runtime/runtime.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <uv.h>

atomic_flag lock_taken = ATOMIC_FLAG_INIT;

static bool can_semacquire(ATOMIC int64_t *addr) {
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

void rt_mutex_lock(rt_mutex_t *m) {
    //    TDEBUGF("[rt_mutex_lock] m->waiters.locker.__sig = %d", m->waiters.locker.__sig)

    int64_t expected = 0; // starving = 0 and locked=0
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
                (old >> MUTEX_WAITER_SHIFT) != 0 && // 如果 waiter list 中存在被阻塞的 coroutine
                atomic_compare_exchange_strong(&m->state, &old, old | MUTEX_WOKEN)) {
                awoke = true;
            }

            rt_do_spin();
            iter++;

            old = atomic_load(&m->state);
            continue;
        }

        int64_t new = old;

        // 当前可能的情况
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
        if (starving && ((old & MUTEX_LOCKED) != 0)) {
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
            starving = starving || ((uv_hrtime() - wait_start_time) > MUTEX_STARVING_THRESHOLD_NS);

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
                delta -= MUTEX_STARVING; // 退出饥饿模式
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

bool rt_mutex_try_lock(rt_mutex_t *m) {
    int64_t old = atomic_load(&m->state);
    if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) != 0) {
        return false;
    }

    if (!atomic_compare_exchange_strong(&m->state, &old, old | MUTEX_LOCKED)) {
        return false;
    }

    return true;
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
        // 非饥饿模式
        int64_t old = new;

        while (true) {
            // waiter shift == 0 表示当前没有等待者
            if ((old >> MUTEX_WAITER_SHIFT) == 0 || (old & (MUTEX_LOCKED | MUTEX_WOKEN | MUTEX_STARVING)) != 0) {
                // 上面已经强制解锁，但是 new != 0, 所以需要判断当前的情况
                return;
            }

            new = (old - (1 << MUTEX_WAITER_SHIFT)) | MUTEX_WOKEN;

            if (atomic_compare_exchange_strong(&m->state, &old, new)) {
                // 预留等待着成功，正式解锁一个等待者
                rt_mutex_waiter_release(m, false);
                return;
            }

            old = atomic_load(&m->state);
        }
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

    n_processor_t *p = processor_get();
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

static bool mutex_yield_commit(coroutine_t *co, void *chan_lock) {
    pthread_mutex_unlock(chan_lock);
    return true;
}


void rt_mutex_waiter_acquire(rt_mutex_t *m, bool to_head) {
    DEBUGF("[rt_mutex_waiter_acquire] m=%p, state=%lu, waiter_count=%lu, to_heap=%d", m, m->state, m->waiter_count,
           to_head);
    // 直接截获了 release 释放的信号量，不需要进入等待队列，直接返回
    if (can_semacquire(&m->sema)) {
        return;
    }

    coroutine_t *co = coroutine_get();
    co->ticket = false;

    // 未获取到信号，将当前 coroutine 加入到阻塞队列中, 并 yield 当前协程， 等待信号唤醒
    while (true) {
        pthread_mutex_lock(&m->waiters.locker);

        atomic_add_int64(&m->waiter_count, 1);
        if (can_semacquire(&m->sema)) {
            atomic_add_int64(&m->waiter_count, -1);

            assertf(m->waiter_count == m->waiters.count, "waiter_count=%lu, waiters.count=%lu", m->waiter_count,
                    m->waiters.count);

            pthread_mutex_unlock(&m->waiters.locker);
            break;
        }

        // no lock
        if (to_head) {
            linkco_list_push_head(&m->waiters, co);
        } else {
            linkco_list_push(&m->waiters, co);
        }

        //        assertf(m->waiter_count == m->waiters.count, "waiter_count=%lu, waiters.count=%lu", m->waiter_count,
        //                m->waiters.count);

        // 一旦解锁， release 就能读取 waiters 并 push 到 runnable list 中导致数据异常
        // 所以需要将锁延迟到 yield 到 sched 后再进行处理
        co_yield_waiting(co, mutex_yield_commit, &m->waiters.locker);

        // co 返回，尝试获取信号量
        if (co->ticket || can_semacquire(&m->sema)) {
            break;
        }
    }
}

void rt_mutex_waiter_release(rt_mutex_t *m, bool handoff) {
    // 无锁添加一个可以释放的信号
    atomic_add_int64(&m->sema, 1);

    if (atomic_load(&m->waiter_count) == 0) {
        assert(m->waiters.count == 0);
        // 无锁状态下 sema 被抢占消费
        return;
    }

    // > 0 开始加锁进行准确判断
    pthread_mutex_lock(&m->waiters.locker);

    // 加锁期间，waiter_count 可能被其他线程消费
    if (atomic_load(&m->waiter_count) == 0) {
        assert(m->waiters.count == 0);
        // consumed by another

        pthread_mutex_unlock(&m->waiters.locker);
        return;
    }

    // waiter_count 存在无锁抢占，所以此处不能安全进行 assert
    //    assertf(m->waiter_count == m->waiters.count, "waiter_count=%lu, waiters.count=%lu", m->waiter_count,
    //            m->waiters.count);

    //    if (atomic_flag_test_and_set(&lock_taken)) {
    //        assertf(false, "race: Lock already taken!");
    //    }

    //    TDEBUGF("waiter info ...%d, %d, %p, %p", m->waiter_count, m->waiters.count, m->waiters.head, m->waiters.rear);
    // head pop
    coroutine_t *wait_co = linkco_list_pop(&m->waiters);
    assert(wait_co);

    atomic_add_int64(&m->waiter_count, -1);


    //    assertf(m->waiter_count == m->waiters.count, "waiter_count=%lu, waiters.count=%lu", m->waiter_count,
    //            m->waiters.count);

    pthread_mutex_unlock(&m->waiters.locker);

    n_processor_t *p = wait_co->p;

    // 尝试抢占自己释放的信号, 并将抢占标记传递给 wait_co
    bool ticket = can_semacquire(&m->sema);
    if (ticket) {
        wait_co->ticket = true; // 抢占信号成功标识
    }

    // 先更新状态避免更新异常
    co_set_status(p, wait_co, CO_STATUS_RUNNABLE);
    if (handoff) {
        rt_linked_fixalloc_push_heap(&p->runnable_list, wait_co);
    } else {
        rt_linked_fixalloc_push(&p->runnable_list, wait_co);
    }
    // 如果 wait_co->p 和当前 co 在同一个 processor 中调度，则直接让出自己的控制权
    coroutine_t *co = coroutine_get();
    if (handoff && co->p == p) {
        co_yield_runnable(co->p, co);
    }
}

/**
 * add int64 必定会更新成功，因为其不停的更新 old 的值并进行更新，而在乎具体的顺序
 * 和 c 语言的 atomic_fetch_add 相比，该函数返回的是更新后的值，而不是更新前的值
 * @param state
 * @param delta
 * @return
 */
int64_t atomic_add_int64(ATOMIC int64_t *state, int64_t delta) {
    int64_t old = atomic_load(state);
    int64_t n = old + delta;
    while (!atomic_compare_exchange_strong(state, &old, n)) {
        old = atomic_load(state);
        n = old + delta;
    }

    return n;
}
