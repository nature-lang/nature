//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

void wait_sysmon() {
    // 每 50 * 10ms 进行 eval 一次
    int gc_eval_count = 50;

    // 循环监控(每 10ms 监控一次)
    while (true) {
        // - 监控长时间被占用的 share processor 进行抢占式调度
        SLICE_FOR(share_processor_list) {
            processor_t *p = SLICE_VALUE(share_processor_list);
            if (p->exit) {
                continue;
            }

            // TODO 使用 trylock 获取不到锁就等下一次
            RDEBUGF("[wait_sysmon.thread_locker] wait locker, p_index_%d=%d", p->share, p->index);
            mutex_lock(p->thread_preempt_locker);
            RDEBUGF("[wait_sysmon.thread_locker] get locker, p_index_%d=%d", p->share, p->index);

            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon] share p_index=%d cannot preempt, will skip", p->index);
                goto SHARE_NEXT;
            }

            assert(p->coroutine);

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                goto SHARE_NEXT;
            }

            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < 1 * 1000 * 1000) { // 1ms
                goto SHARE_NEXT;
            }

            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d co=%p run timeout(%lu ms), will send SIGURG",
                    p->index, p->coroutine,
                    time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d send SIGURG success, handle next", p->index);

            // 抢占信号发送成功之后不能解锁，将解锁的工作交给线程信号处理, 如果直接解锁，在信号发送之前 coroutine 可能会发生状态切换,
            // 导致抢占异常
            continue;
            SHARE_NEXT:
            mutex_unlock(p->thread_preempt_locker);
            RDEBUGF("[wait_sysmon.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor
        // - 当需要 stw 时，如果 coroutine 在 running 阶段，则进行抢占式调用
        //        LINKED_FOR(solo_processor_list) {
        //            continue; // TODO 暂时不做处理看看
        //            processor_t *p = LINKED_VALUE();
        //            if (p->exit) {
        //                // 顺便进行一个清理, 避免下次遍历再次遇到
        //                processor_free(p);
        //                linked_remove_free(solo_processor_list, node);
        //                continue;
        //            }
        //
        //            // safe point 是会添加 no preempt 选项
        //            if (!p->can_preempt) {
        //                RDEBUGF("[wait_sysmon] solo processor_index=%d cannot preempt, will skip", p->index);
        //                continue;
        //            }
        //
        //            // 没有 coroutine 就标识还没有开始调度
        //            if (!p->coroutine) {
        //                continue;
        //            }
        //
        //            // solo processor 只有在 STW 时才会进行抢占式调度
        //            if (!processor_get_stw()) {
        //                continue;
        //            }
        //
        //            // blocking syscall 可以正常进行 stw，不需要抢占
        //            if (p->coroutine->status != CO_STATUS_RUNNING) {
        //                continue;
        //            }
        //
        //            RDEBUGF("[wait_sysmon] solo p_index=%d need stw(%d), will send SIGURG", p->index, processor_get_stw());
        //
        //            // 发送信号强制中断线程
        //            if (pthread_kill(p->thread_id, SIGURG) != 0) {
        //                // 信号发送异常, 直接异常退出
        //                // continue;
        //                assert(false && "error sending SIGURG to thread");
        //            }
        //        }

        // - GC 判断 (每 100ms 进行一次)
        if (gc_eval_count <= 0) {
            runtime_eval_gc();
            gc_eval_count = 10;
        }

        // processor exit 主要和 main coroutine 相关，当 main coroutine 退出后，则整个 wait sysmon 进行退出
        if (processor_get_exit()) {
            RDEBUGF("[wait_sysmon] processor need exit, will exit");
            break;
        }

        gc_eval_count--;

        usleep(1 * 1000); // 10ms
    }
}