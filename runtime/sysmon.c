//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

#define CO_TIMEOUT (10 * 1000 * 1000) // 10ms

// 等待 1000ms 的时间，如果无法抢占则 deadlock
#define PREEMPT_TIMEOUT 1000
void wait_sysmon() {
    // 每 50 * 10ms 进行 eval 一次
    int gc_eval_count = WAIT_MID_TIME;

    // 循环监控(每 10ms 监控一次)
    while (true) {
        // - 监控长时间被占用的 share processor 进行抢占式调度
        PROCESSOR_FOR(share_processor_list) {
            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_TPLCALL && p->status != P_STATUS_RTCALL) {
                RDEBUGF("[wait_sysmon.share] p_index=%d p_status=%d cannot preempt, will skip", p->index, p->status);
                continue;
            }

            coroutine_t *co = p->coroutine;
            assert(co); // p co 切换期间不会清空 co 了
            if (co->gc_work) {
                RDEBUGF("[wait_sysmon.share] p_index=%d(%lu), co=%p is gc_work, will skip", p->index, (uint64_t)p->thread_id, p->coroutine);
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                continue;
            }
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
            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_TPLCALL && p->status != P_STATUS_RTCALL) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, status=%d cannot preempt, goto unlock", p->index, p->status);
                goto SHARE_UNLOCK_NEXT;
            }

            // 获取了 thread_locker 此时协程的状态被锁定，且 p->co 也不再允许进行切换
            // 但是获取该锁期间，status/p->co 可能都发生了变化，所以需要重新进行判断
            // 成功获取到 locker 并且当前 can_preempt = true 允许抢占，
            // 但是这期间可能已经发生了 co 切换，所以再次进行 co_start 超时的判断
            co = p->coroutine;
            assert(co);
            // 协程发生了切换，原来的超时已经失效
            if (co->gc_work) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p is gc_work, goto unlock", p->index, co);
                goto SHARE_UNLOCK_NEXT;
            }

            // 由于获取了锁，此时 p->co_started_at 无法被重新赋值，所以再次获取时间判断超时
            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p co_stared_at = 0, goto unlock", p->index, co);
                goto SHARE_UNLOCK_NEXT;
            }
            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p not timeout(%lu ms), goto unlock", p->index, p->coroutine,
                        time / 1000 / 1000);
                goto SHARE_UNLOCK_NEXT;
            }

            // 基于协作的原则，还是不进行强制抢占，还是使用 deadlock 进行提醒，时间可以设置久一点，500ms
            // TODO cpu 繁忙时的超时处理
            for (int i = 0; i <= 1000; i++) {
                // 进入到允许抢占状态
                if (p->status == P_STATUS_RUNNING) {
                    break;
                }

                // 进入到 STW 安全状态
                if (p->status == P_STATUS_DISPATCH) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), p_status=%d co=%p change dispatch, will unlock", p->index,
                            (uint64_t)p->thread_id, p->status, p->coroutine);
                    goto SHARE_UNLOCK_NEXT;
                }

                usleep(1 * 1000);
            }

            // 抢占超时
            if (p->status == P_STATUS_TPLCALL || p->status == P_STATUS_RTCALL) {
                assertf(false, "deadlock: syscall run timeout, p_index=%d(%lu), co=%p", p->index, (uint64_t)p->thread_id, p->coroutine);
            }

            assert(p->status == P_STATUS_RUNNING);

            // 开始抢占
            TDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), p_status=%d co=%p run timeout(%lu ms), will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->status, p->coroutine, time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            // 直接更新 can_preempt 的值以及清空 co_start_t 避免被重复抢占
            TDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d send SIGURG success", p->index);
            p->status = P_STATUS_PREEMPT;
        SHARE_UNLOCK_NEXT:
            mutex_unlock(&p->thread_locker);
            TDEBUGF("[wait_sysmon.share.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor, 需要做的工作有
        // 1. 遍历 solo_processor 需要先获取 solo processor lock
        // 2. 如果 solo processor exit 需要进行清理
        // 3. 如果 solo processor.need_stw == true, 则需要辅助 processor 进入 safe_point
        TRACEF("[wait_sysmon.solo] wait solo_processor_locker");
        mutex_lock(&solo_processor_locker);
        TRACEF("[wait_sysmon.solo] get solo_processor_locker, solo_p_count=%d", solo_processor_count);

        processor_t *prev = NULL;
        processor_t *p = solo_processor_list;
        while (p) {
            if (p->status == P_STATUS_EXIT) {
                RDEBUGF("[wait_sysmon.solo] p=%p, index=%d, exited, prev=%p,  will remove from solo_processor_list=%p", p, p->index, prev,
                        solo_processor_list);

                processor_t *exited = p;
                if (prev) {
                    prev->next = p->next;
                    p = p->next;
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
            if (p->need_stw == 0) {
                TRACEF("[wait_sysmon.solo] processor_index=%d, current not need stw, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // 不知道是不是自己的 stw 锁，难道需要重复解锁？什么时候解锁比较合适？processor_stop_stw() 之后？
            // 什么时候将 safe_point 配置为 false 呢？也在 processor_stop_stw 之后？
            if (processor_safe(p)) {
                RDEBUGF("[wait_sysmon.solo] processor_index=%d, in safe_point, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_TPLCALL && p->status != P_STATUS_RTCALL) {
                RDEBUGF("[wait_sysmon.solo] p->status=%d, not running/tplcall/rtcall, p_index=%d, will skip", p->status, p->index);

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

            RDEBUGF("[wait_sysmon.solo] run timeout, will get thread_locker, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            int trylock = mutex_times_trylock(&p->thread_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon.solo] trylock failed, p_index_%d=%d(%lu), p_status=%d", p->share, p->index, (uint64_t)p->thread_id,
                        p->status);
                continue;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] success get thread_locker, p_index_%d=%d(%lu), p_status=%d", p->share, p->index,
                    (uint64_t)p->thread_id, p->status);

            if (p->need_stw == 0) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] not need stw, p_index_%d=%d(%lu), will goto unlock", p->share, p->index,
                        (uint64_t)p->thread_id);

                goto SOLO_UNLOCK_NEXT;
            }

            if (processor_safe(p)) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] already in safe_point, p_index_%d=%d(%lu), will goto unlock", p->share, p->index,
                        (uint64_t)p->thread_id);

                goto SOLO_UNLOCK_NEXT;
            }

            if (p->status != P_STATUS_RUNNING && p->status != P_STATUS_TPLCALL && p->status != P_STATUS_RTCALL) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p->status=%d, not running/syscall, p_index=%d, goto unlock", p->status, p->index);

                goto SOLO_UNLOCK_NEXT;
            }

            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, p_status=%d, co_stared_at = 0, goto unlock", p->index, p->status);
                goto SOLO_UNLOCK_NEXT;
            }
            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, not run timeout(%lu ms),  goto unlock", p->index, time / 1000 / 1000);
                goto SOLO_UNLOCK_NEXT;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d run timeout(%lu) need assist to safe_point, will get gc_stw_locker",
                    p->index, time / 1000 / 1000);

            // gc_stw_locker 禁止 tplcall -> running
            mutex_lock(&p->gc_stw_locker);
            TDEBUGF("[wait_sysmon.solo.thread_locker] success get gc_stw_locker, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            // rtcall 原则上不会阻塞，所以可以无线等待直到 rtcall 退出
            while (true) {
                // rtcall -> running/dispatch
                if (p->status != P_STATUS_RTCALL) {
                    break;
                }

                // 进入到 STW 安全状态
                if (p->status == P_STATUS_DISPATCH) {
                    TDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu), p_status=%d co=%p change dispatch, will unlock", p->index,
                            (uint64_t)p->thread_id, p->status, p->coroutine);
                    goto SOLO_UNLOCK_NEXT;
                }

                usleep(1 * 1000);
            }
            TDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu), p_status=%d co=%p not eq rtcall", p->index, (uint64_t)p->thread_id,
                    p->status, p->coroutine);

            // 当前可能的状态是 tplcall/dispatch/running
            // tplcall 是不稳定的，可能随时会切换到 dispatch 状态, 但是不会切换到 running 状态
            if (p->status != P_STATUS_RUNNING) {
                // tplcall or dispatch 状态, 都是可以安全抢占的状态
                TDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu), status=%d, co=%p in tplcall/dispatch, assist to safe point",
                        p->index, (uint64_t)p->thread_id, p->status, p->coroutine);

                p->safe_point = p->need_stw;
                goto SOLO_UNLOCK_NEXT;
            }

            assert(p->status == P_STATUS_RUNNING);

            // 通过抢占进入安全点(可以直接抢占)
            TDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu),p_status=%d co=%p running timeout will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->status, p->coroutine);

            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            TDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d send SIGURG success", p->index);
            p->status = P_STATUS_PREEMPT;
        SOLO_UNLOCK_NEXT:
            mutex_unlock(&p->thread_locker);

            TDEBUGF("[wait_sysmon.solo.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);

            prev = p;
            p = p->next;
        }

        mutex_unlock(&solo_processor_locker);
        TRACEF("[wait_sysmon.solo] solo_processor_locker unlock");

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

        // 每 5ms 进行一次 gc 验证
        usleep(WAIT_BRIEF_TIME * 5 * 1000); // 5ms
    }

    RDEBUGF("[wait_sysmon] wait sysmon exit success");
}