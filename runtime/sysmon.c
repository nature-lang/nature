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
            continue; // TODO 暂时不做处理
            processor_t *p = SLICE_VALUE(share_processor_list);
            if (p->exit) {
                continue;
            }

            if (p->no_preempt) {
                DEBUGF("[wait_sysmon] share processor_index=%d cannot preempt, will skip", p->index);
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                continue;
            }

            if ((uv_hrtime() - co_start_at) < 10 * 1000 * 1000) { // 10ms
                continue;
            }

            DEBUGF("[wait_sysmon] share processor_index=%d coroutine run timeout(%lu), will send SIGURG", p->index, co_start_at);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                // 信号发送异常, 直接异常退出
                // continue;
                assertf(false, "error sending SIGURG to thread");
            }
        }

        // - 监控状态处于 running solo processor, 如果不是 block 在系统调用，则同样需要抢占式调度
        LINKED_FOR(solo_processor_list) {
            processor_t *p = LINKED_VALUE();
            if (p->exit) {
                // 顺便进行一个清理, 避免下次遍历再次遇到
                processor_free(p);
                linked_remove_free(solo_processor_list, node);
                continue;
            }

            if (p->no_preempt) {
                DEBUGF("[wait_sysmon] solo processor_index=%d cannot preempt, will skip", p->index);
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                continue;
            }

            if ((uv_hrtime() - co_start_at) < 10 * 1000 * 1000) { // 10ms
                continue;
            }

            // 独享线程只有进入到计算密集运行时才需要进行抢占式调度，从而让 GC 可以安全的进行处理
            if (p->coroutine->status != CO_STATUS_RUNNING) {
                continue;
            }

            DEBUGF("[wait_sysmon] solo processor %p coroutine run timeout(%lu), will send SIGURG", p, co_start_at);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                // 信号发送异常, 直接异常退出
                // continue;
                assertf(false, "error sending SIGURG to thread");
            }
        }

        // - GC 判断 (每 100ms 进行一次)
        if (gc_eval_count <= 0) {
            runtime_eval_gc();
            gc_eval_count = 10;
        }

        // processor exit 主要和 main coroutine 相关，当 main coroutine 退出后，则整个 wait sysmon 进行退出
        if (processor_get_exit()) {
            DEBUGF("[wait_sysmon] processor need exit, will exit");
            break;
        }

        gc_eval_count--;

        uv_sleep(WAIT_SHORT_TIME);
    }
}