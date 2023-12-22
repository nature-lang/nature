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
            if (p->exit) {
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                continue;
            }

            if ((uv_hrtime() - co_start_at) < 10 * 1000 * 1000) { // 10ms
                continue;
            }

            DEBUGF("[sysmon_run] processor %p coroutine run timeout(%lu), will send SIGURG", p, co_start_at);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                // 信号发送异常, 直接异常退出
                // continue;
                assertf(false, "error sending SIGURG to thread");
            }
        }

        if (processor_get_exit()) {
            DEBUGF("[sysmon_run] processor need exit, will exit");
            break;
        }

        // sleep 10ms
        uv_sleep(10);
    }
}