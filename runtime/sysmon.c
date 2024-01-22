//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

// TODO no wait locker, use try lock
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

            RDEBUGF("[wait_sysmon.thread_locker] wait locker, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t) p->thread_id);
//            write(STDOUT_FILENO, "-----0\n", 7);
            mutex_lock(p->thread_preempt_locker);
//            write(STDOUT_FILENO, "-----1\n", 7);
            RDEBUGF("[wait_sysmon.thread_locker] get locker, p_index_%d=%d", p->share, p->index);

            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon] share p_index=%d cannot preempt, will skip", p->index);
                goto SHARE_NEXT;
            }

            assert(p->coroutine);

            if (p->coroutine->gc_work) {
                RDEBUGF("[wait_sysmon] share p_index=%d, co=%p is gc_work, will skip", p->index, p->coroutine);
                goto SHARE_NEXT;
            }

//            write(STDOUT_FILENO, "-----2\n", 7);
            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                goto SHARE_NEXT;
            }

            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < 1 * 1000 * 1000) { // 1ms
                goto SHARE_NEXT;
            }

            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d(%lu) co=%p run timeout(%lu ms), will send SIGURG",
                    p->index,
                    (uint64_t) p->thread_id, p->coroutine, time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d send SIGURG success, wait preempt success", p->index);

            // 循环等待直到信号处理完成(p->can_preempt 会被设置为 false 就是成功了, 等待 100 ms， 等不到就报错)
            for (int i = 0; i <= 1000; i++) {
                if (!p->can_preempt) {
                    RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d preempt success, will goto unlocker",
                            p->index);
                    goto SHARE_NEXT;
                }

                usleep(1 * 100); // 每 0.1 ms 探测一次
            }

            // 解锁异常, 信号处理一直没有成功
            assert(false && "error sending SIGURG to thread");
            SHARE_NEXT:
            mutex_unlock(p->thread_preempt_locker);
            RDEBUGF("[wait_sysmon.thread_locker] share unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor
        // - 当需要 stw 时，如果 coroutine 在 running 阶段，则进行抢占式调用
        LINKED_FOR(solo_processor_list) {
            processor_t *p = LINKED_VALUE();

            if (p->exit) {
                processor_free(p);
                safe_linked_remove_free(solo_processor_list, node);
                continue;
            }

            // - 获取抢占锁
            mutex_lock(p->thread_preempt_locker);

            // - 判断是否可以抢占
            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon] solo processor_index=%d cannot preempt, will skip", p->index);
                goto SOLO_NEXT;
            }

            // 没有 coroutine 就标识还没有开始调度
            if (!p->coroutine) {
                goto SOLO_NEXT;
            }

            // - solo processor 只有在 STW(gc) 时才会进行抢占式调度
            if (!processor_get_stw()) {
                goto SOLO_NEXT;
            }

            // - solo processor 仅在 running 时才需要抢占， blocking syscall 虽然也是阻塞的，但是不会进行 GC 等操作可以放心抢占
            if (p->coroutine->status != CO_STATUS_RUNNING) {
                goto SOLO_NEXT;
            }

            // - 判断运行时长, 即使是 solo processor 也尽量不进行抢占，除非运行时间过长
            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                goto SOLO_NEXT;
            }

            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < 1 * 1000 * 1000) { // 1ms
                goto SOLO_NEXT;
            }


            RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d(%lu) co=%p run timeout(%lu ms), will send SIGURG",
                    p->index, (uint64_t) p->thread_id, p->coroutine, time / 1000 / 1000);

            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d send SIGURG success, wait preempt success", p->index);

            for (int i = 0; i <= 1000; i++) {
                if (!p->can_preempt) {
                    RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d preempt success, will goto unlocker",
                            p->index);
                    goto SHARE_NEXT;
                }

                usleep(1 * 100); // 每 0.1 ms 探测一次
            }

            SOLO_NEXT:
            mutex_unlock(p->thread_preempt_locker);
            RDEBUGF("[wait_sysmon.thread_locker] solo unlocker, p_index_%d=%d", p->share, p->index);
        }

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

    RDEBUGF("[wait_sysmon] wait sysmon exit success");
}