//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

#define CO_TIMEOUT (1 * 1000 * 1000)

// 等待 1000ms 的时间，如果无法抢占则 deadlock
#define PREEMPT_TIMEOUT 1000
void wait_sysmon() {
    // 每 50 * 10ms 进行 eval 一次
    int gc_eval_count = WAIT_MID_TIME;

    // 循环监控(每 10ms 监控一次)
    while (true) {
        // - 监控长时间被占用的 share processor 进行抢占式调度
        PROCESSOR_FOR(share_processor_list) {
            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_SYSCALL) {
                continue;
            }

            RDEBUGF("[wait_sysmon.share] p_index=%d status=%d", p->index, p->status);
            coroutine_t *co = p->coroutine;
            assert(co);

            if (co->gc_work) {
                RDEBUGF("[wait_sysmon.share] p_index=%d(%lu), co=%p is gc_work, will skip", p->index, (uint64_t)p->thread_id, p->coroutine);
                continue;
            }

            // 0 表示未开始调度
            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                continue;
            }

            // 当前未超时，不需要处理
            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine, co,
                        time / 1000 / 1000);
                continue;
            }

            RDEBUGF("[wait_sysmon.share] runtime timeout(%lu ms), wait locker, p_index_%d=%d(%lu)", time / 1000 / 1000, p->share, p->index,
                    (uint64_t)p->thread_id);

            // 尝试 10ms 获取抢占 disable_preempt_locker,避免抢占期间抢占状态变更成不可抢占, 如果获取不到就跳过
            // 但是可以从 not -> can
            int trylock = mutex_times_trylock(&p->thread_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon.share] trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);
                continue;
            }

            // yield/exit 首先就设置 can_preempt = false, 这里就无法完成。所以 p->coroutine 一旦设置就无法被清空或者切换
            RDEBUGF("[wait_sysmon.share.thread_locker] try locker success, p_index_%d=%d", p->share, p->index);

            // 判断当前是否是可抢占状态
            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_SYSCALL) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, status=%d cannot preempt, goto unlock", p->index, p->status);
                goto SHARE_UNLOCK_NEXT;
            }

            // 获取了 thread_locker 此时协程的状态被锁定，且 p->co 也不再允许进行切换
            // 但是获取该锁期间，status/p->co 可能都发生了变化，所以需要重新进行判断
            // 成功获取到 locker 并且当前 can_preempt = true 允许抢占，
            // 但是这期间可能已经发生了 co 切换，所以再次进行 co_start 超时的判断
            coroutine_t *post_co = p->coroutine;
            assert(post_co);
            // 协程发生了切换，原来的超时已经失效
            if (post_co != co) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, co=%d is gc_work, goto unlock", p->index, co->gc_work);
                goto SHARE_UNLOCK_NEXT;
            }

            // 由于获取了锁，此时 p->co_started_at 无法被重新赋值，所以再次获取时间判断超时
            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p/%p co_stared_at = 0, goto unlock", p->index, p->coroutine, co);
                goto SHARE_UNLOCK_NEXT;
            }
            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p/%p run not timeout(%lu ms),  goto unlock", p->index,
                        p->coroutine, post_co, time / 1000 / 1000);
                goto SHARE_UNLOCK_NEXT;
            }

            // 开始抢占
            RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), status=%d co=%p run timeout(%lu ms), will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->status, p->coroutine, time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            // 直接更新 can_preempt 的值以及清空 co_start_t 避免被重复抢占
            RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d send SIGURG success", p->index);
            p->co_started_at = 0; // 直接更新状态为 preempt? 这个状态应该就不可以抢占了？
            p->status = P_STATUS_PREEMPT;
        SHARE_UNLOCK_NEXT:
            mutex_unlock(&p->thread_locker);
            RDEBUGF("[wait_sysmon.share.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor, 需要做的工作有
        // 1. 遍历 solo_processor 需要先获取 solo processor lock
        // 2. 如果 solo processor exit 需要进行清理
        // 3. 如果 solo processor.need_stw == true, 则需要辅助 processor 进入 safe_point
        RDEBUGF("[wait_sysmon.solo] wait locker");
        mutex_lock(&solo_processor_locker);
        RDEBUGF("[wait_sysmon.solo] get locker, solo_p_count=%d", solo_processor_count);

        processor_t *prev = NULL;
        processor_t *p = solo_processor_list;
        while (p) {
            if (p->status == P_STATUS_EXIT) {
                RDEBUGF("[wait_sysmon.solo] p=%p, index=%d, exited, prev=%p,  will remove from solo_processor_list=%p", p, p->index, prev,
                        solo_processor_list);

                processor_t *exited = p;
                if (prev) {
                    prev->next = p->next;
                    p = p->next; // 不需要更新 prev 了
                } else {
                    solo_processor_list = p->next;
                    p = p->next;
                }

                processor_free(exited);
                RDEBUGF("[wait_sysmon.solo] p=%p remove from solo processor list, prev=%p, solo_processor_list=%p", exited, prev,
                        solo_processor_list);

                continue;
            }

            // solo processor 仅需要 stw 的时候进行抢占, 由于锁定了 solo_processor_locker, 所以其他线程此时
            // 无法对 p->need_stw 进行解锁。
            if (p->need_stw == false) {
                RDEBUGF("[wait_sysmon.solo] processor_index=%d, current not need stw, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // 不知道是不是自己的 stw 锁，难道需要重复解锁？什么时候解锁比较合适？processor_stop_stw() 之后？
            // 什么时候将 safe_point 配置为 false 呢？也在 processor_stop_stw 之后？
            if (p->safe_point) {
                RDEBUGF("[wait_sysmon.solo] processor_index=%d, in safe_point, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // 仅仅超时才考虑进行抢占或者辅助 safe_point
            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p co_stared_at = 0, will skip", p->index, p->coroutine);
                continue;
            }
            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p run not timeout(%lu ms), will skip", p->index, p->coroutine,
                        time / 1000 / 1000);
                continue;
            }

            RDEBUGF("[wait_sysmon.solo] need stw, will get stw locker, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

            // 1. 禁止状态切换，禁止 p->co_start_at 清空
            // 2. 禁止 p->co 切换
            // 3. thread_locker 是短期锁，一旦解锁， p->status 的状态依旧会发生修改
            mutex_lock(&p->thread_locker);
            RDEBUGF("[wait_sysmon.solo.thread_locker] success get thread_locker, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            // 获取锁期间可能会发生一些状态转换，所以需要做一些重新判断
            if (p->safe_point) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] current in safe_point, p_index_%d=%d(%lu), will goto unlock", p->share, p->index,
                        (uint64_t)p->thread_id);

                prev = p;
                p = p->next;
                goto SOLO_UNLOCK_NEXT;
            }

            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_SYSCALL) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p->status=%d, not running/syscall, p_index=%d, goto unlock", p->status, p->index);

                prev = p;
                p = p->next;
                goto SOLO_UNLOCK_NEXT;
            }

            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, co_stared_at = 0, goto unlock", p->index, p->coroutine);
                prev = p;
                p = p->next;
                goto SOLO_UNLOCK_NEXT;
            }
            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, not run timeout(%lu ms),  goto unlock", p->index, time / 1000 / 1000);
                goto SOLO_UNLOCK_NEXT;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, need assist to safe_point", p->index, time / 1000 / 1000);

            // 开启 gc_stw_locker, 避免从安全点进入到 running 状态
            // 仅对 solo_p 生效的锁
            // 1. 使用 rt_gc_malloc 分配新的内存
            // 2. 通过 write_barrier 产生新的 gc_work_ptr
            // 3. 将 p status 设置为 running 时会进行阻塞
            RDEBUGF("[wait_sysmon.solo.thread_locker] need assist safe_point, will get gc_stw_locker, p_index_%d=%d(%lu)", p->share,
                    p->index, (uint64_t)p->thread_id);
            mutex_lock(&p->gc_stw_locker);
            RDEBUGF("[wait_sysmon.solo.thread_locker] success get gc_stw_locker, will get thread_locker, p_index_%d=%d(%lu)", p->share,
                    p->index, (uint64_t)p->thread_id);

            if (p->status == P_STATUS_SYSCALL) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu), status=%d, co=%p in syscall, can direct to safe point", p->index,
                        (uint64_t)p->thread_id, p->status, p->coroutine);
                p->safe_point = true;

                prev = p;
                p = p->next;
                goto SOLO_UNLOCK_NEXT;
            }

            assert(p->status == P_STATUS_RUNNING);

            // 通过抢占进入安全点(可以直接抢占)
            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu),status=%d co=%p run in user code will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->status, p->coroutine);

            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d send SIGURG success", p->index);
            p->co_started_at = 0;
            p->safe_point = true;
            p->status = P_STATUS_PREEMPT;
        SOLO_UNLOCK_NEXT:
            mutex_unlock(&p->thread_locker);

            RDEBUGF("[wait_sysmon.solo.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);

            prev = p;
            p = p->next;
        }

        mutex_unlock(&solo_processor_locker);
        RDEBUGF("[wait_sysmon.solo] unlocker");

        // processor exit 主要和 main coroutine 相关，当 main coroutine 退出后，则整个 wait sysmon 进行退出
        if (processor_get_exit()) {
            RDEBUGF("[wait_sysmon] processor need exit, will exit");
            break;
        }

        // - GC 判断 (每 100ms 进行一次)
        if (gc_eval_count <= 0) {
            runtime_eval_gc();
            gc_eval_count = WAIT_MID_TIME;
        }

        gc_eval_count--;

        usleep(5 * 1000); // 5ms
    }

    RDEBUGF("[wait_sysmon] wait sysmon exit success");
}