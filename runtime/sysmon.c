//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

void sysmon_run() {
    // 循环监控(每 10ms 监控一次)
    while (true) {
        // - 监控长时间(5ms)被占用的 share processor 进行抢占式调度
        SLICE_FOR(share_processor_list) {
            processor_t *p = SLICE_VALUE(share_processor_list);
            uint64_t co_start_at = p->co_started_at;
            uint64_t now = uv_hrtime();
            if (now - co_start_at < 10 * 1000 * 1000) { // 10ms
                continue;
            }

            // 发送信号强制中断线程
            // 像线程发送 sig
            // 向新创建的线程发送 SIGURG 信号
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                // 信号发送异常, 直接异常退出
                // continue;
                assertf(false, "error sending SIGURG to thread");
            }
        }

        // sleep 10ms
        uv_sleep(10);
    }
}