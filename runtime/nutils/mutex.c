#include "mutex.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <uv.h>


/**
 * @param mutex
 */
void rt_mutex_lock(rt_mutex_t *mutex) {
    int64_t expected = 0;// starving = 0 and locked=0
    if (atomic_compare_exchange_strong(&mutex->state, &expected, MUTEX_LOCKED)) {
        return;
    }

    uint64_t wait_time = 0;
    int64_t count = 0;
    bool starving = false;
    int64_t old = atomic_load(&mutex->state);
    while (true) {
        // 非饥饿模式下一致自旋
        if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) == MUTEX_LOCKED && rt_can_spin(count)) {
            // 非饥饿模式且锁定模式进行 spin
            rt_do_spin();
            count++;

            // 更新 old
            old = atomic_load(&mutex->state);
            continue;
        }

        // 非饥饿模式下直接尝试加锁
        if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) == 0) {
            int64_t new = old | MUTEX_LOCKED;
            if (atomic_compare_exchange_strong(&mutex->state, &old, new)) {
                return;
            } else {
                // 抢占锁失败, 更新 old, 从新进入
                old = atomic_load(&mutex->state);
                continue;
            }
        }

        // 判断当前 coroutine 是否长时间未获取锁
        if (wait_time == 0) {
            wait_time = uv_hrtime();
        }

        // old 依旧处于 locked 状态，或者已经进入到了 starting 状态
        if ((old & MUTEX_LOCKED | MUTEX_STARVING) != 0) {
        }


        // old 进入到饥饿模式，或者以及解锁, 或者超过自旋次数依旧未解锁
        int64_t new = old;

        // 即使原来已经上锁，也直接尝试加锁
        if ((old & MUTEX_STARVING) == 0) {
            new |= MUTEX_LOCKED;
        }

        // 当前协程准备进入饥饿模式，并且当前的锁是上锁状态
        if (starving && (old & MUTEX_LOCKED) != 0) {
            new |= MUTEX_STARVING;
        }

        if (atomic_compare_exchange_strong(&mutex->state, &old, new)) {
            // locked 或者 starving 中成功了一个。 TODO 这不对, 可能 new 依旧是 locked 和 starving 啥也没干！
            //
            // 加锁成功, 且 old 非饥饿模式/也没有上锁 (cas 模式下，old 完全可以信任)
            if ((old & (MUTEX_LOCKED | MUTEX_STARVING)) == 0) {
                return;
            }

            // old 有哪些可能


            // 1.
            // 加锁成功但是处于饥饿模式 or 原先是上锁状态但添加饥饿 flag 成功
            // 当前一定处于饥饿模式，所以不能主动进行加锁, TODO 没有解锁时机了, 控制权在我手上, 还是有点复杂了。
        }
    }
}