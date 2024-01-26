//
// Created by weiwenhao on 2023/11/16.
//

#include "sysmon.h"

#define CO_TIMEOUT (1 * 1000 * 1000)

void wait_sysmon() {
    // 每 50 * 10ms 进行 eval 一次
    int gc_eval_count = WAIT_MID_TIME;

    // 循环监控(每 10ms 监控一次)
    while (true) {
        // - 监控长时间被占用的 share processor 进行抢占式调度
        PROCESSOR_FOR(share_processor_list) {
            if (p->exit) {
                continue;
            }

            // TODO 可能永远都无法抢占。必须卡点到 can preempt, 不如一开始就尝试获取锁，只要获取到锁，can_preempt 就无法被设置为 false
            //  if (!p->can_preempt) {
            //      RDEBUGF("[wait_sysmon] share p_index=%d cannot preempt, will skip", p->index);
            //      continue;
            // }
            coroutine_t *co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon] share p_index=%d no coroutine, will skip", p->index);
                continue;
            }

            if (co->gc_work) {
                RDEBUGF("[wait_sysmon] share p_index=%d, co=%p is gc_work, will skip", p->index, p->coroutine);
                continue;
            }

            // syscall(tpl) 期间不可抢占
            // if (co->status == CO_STATUS_SYSCALL) {
            // RDEBUGF("[wait_sysmon] share p_index=%d, co=%p in syscall, will skip", p->index, p->coroutine);
            // continue;
            //}

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon] share p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                continue;
            }

            // 当前未超时，不需要抢占
            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon] share p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine, co,
                        time / 1000 / 1000);
                continue;
            }

            RDEBUGF("[wait_sysmon] runtime timeout, wait locker, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

            // 尝试 10ms 获取 lock, 获取不到就跳过
            int trylock = mutex_times_trylock(&p->preempt_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon] trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);
                continue;
            }

            RDEBUGF("[wait_sysmon.thread_locker] try lock success, p_index_%d=%d, locker_count=%d|%d", p->share, p->index,
                    p->preempt_locker.locker_count, p->preempt_locker.unlocker_count);

            // 再次判断相关信息, 获取锁后 coroutine 不会再进行 resume/yield 状态修改
            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d cannot preempt, will skip", p->index);
                goto SHARE_UNLOCK_NEXT;
            }

            if (!p->coroutine) {
                RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d no coroutine(may resume), will skip", p->index);
                goto SHARE_UNLOCK_NEXT;
            }

            if (p->coroutine->status == CO_STATUS_SYSCALL) {
                RDEBUGF("[wait_sysmon] share p_index=%d, co=%p in syscall, will skip", p->index, p->coroutine);
                goto SHARE_UNLOCK_NEXT;
            }

            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                goto SHARE_UNLOCK_NEXT;
            }

            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine,
                        co, time / 1000 / 1000);
                goto SHARE_UNLOCK_NEXT;
            }

            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d(%lu) co=%p run timeout(%lu ms), will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->coroutine, time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            write(STDOUT_FILENO, "___ws_2\n", 8);
            RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d send SIGURG success, wait preempt success", p->index);

            // 循环等待直到信号处理完成(p->can_preempt 会被设置为 false(不可抢占) 就是成功了
            for (int i = 0; i <= WAIT_MID_TIME; i++) {
                if (!p->can_preempt) {
                    write(STDOUT_FILENO, "___ws_0\n", 8);
                    RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d preempt success, will goto unlocker", p->index);
                    goto SHARE_UNLOCK_NEXT;
                }

                write(STDOUT_FILENO, "___ws_1\n", 8);
                RDEBUGF("[wait_sysmon.thread_locker] share p_index=%d preempt success, will goto unlocker", p->index);
                usleep(1 * 1000); // 每 1 ms 探测一次
            }

            // 解锁异常, 信号处理一直没有成功
            assert(false && "error sending SIGURG to thread");
        SHARE_UNLOCK_NEXT:
            mutex_unlock(&p->preempt_locker);
            RDEBUGF("[wait_sysmon.thread_locker] share unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor
        // solo processor 独享线程所以不需要抢占，仅仅是需要 STW 时才进行抢占，由于抢占的目的是 GC 相关, 如果当前 coroutine
        // status=syscall(tpl call) 则不需要抢占，因为在 tpl call 中是不会调整栈空间的，所以 STW 可以放心扫描已经开启写屏障。
        // 在 tpl post hook 中如果检测到 STW 此时 solo processor 延迟进入 STW 状态即可
        processor_t *prev = NULL;
        processor_t *p = solo_processor_list;
        while (p) {
            break; // TODO 暂时不搞
            if (p->exit) {
                if (prev) {
                    prev->next = p->next;
                    p = p->next; // 不需要更新 prev 了
                } else {
                    solo_processor_list = p->next;
                    p = p->next;
                }

                processor_free(p);
                RDEBUGF("[wait_sysmon] solo p=%p remove from solo processor list, prev=%p, solo_processor_list=%p", p, prev,
                        solo_processor_list);

                continue;
            }

            if (!processor_get_stw()) {
                RDEBUGF("[wait_sysmon] solo processor_index=%d, current not need stw, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon] solo p_index=%d cannot preempt, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            coroutine_t *co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon] solo p_index=%d no coroutine, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // - solo processor 仅在 running 时才需要抢占， blocking syscall 虽然也是阻塞的，但是不会进行 GC 等操作, 可以延迟进入 STW
            if (p->coroutine->status != CO_STATUS_RUNNING) {
                RDEBUGF("[wait_sysmon] solo p_index=%d, co=%p/%p status=%d, will skip", p->index, p->coroutine, co, p->coroutine->status);

                prev = p;
                p = p->next;
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon] solo p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);

                prev = p;
                p = p->next;
                continue;
            }

            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon] solo p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine, co,
                        time / 1000 / 1000);

                prev = p;
                p = p->next;
                continue;
            }

            RDEBUGF("[wait_sysmon] solo need stw and runtime timeout, will get locker and preempt, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            int trylock = mutex_times_trylock(&p->preempt_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon] solo trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

                prev = p;
                p = p->next;
                continue;
            }

            RDEBUGF("[wait_sysmon.thread_locker] solo get locker, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

            if (!processor_get_stw()) {
                RDEBUGF("[wait_sysmon.thread_locker] solo processor_index=%d, current not need stw, will skip", p->index);
                goto SOLO_UNLOCK_NEXT;
            }

            // - 判断是否可以抢占
            if (!p->can_preempt) {
                RDEBUGF("[wait_sysmon.thread_locker] solo processor_index=%d cannot preempt, will skip", p->index);
                goto SOLO_UNLOCK_NEXT;
            }

            if (!p->coroutine) {
                RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d no coroutine(may resume), will skip", p->index);
                goto SOLO_UNLOCK_NEXT;
            }

            // - solo processor 仅在 running 时才需要抢占， blocking syscall 虽然也是阻塞的，但是不会进行 GC 等操作可以放心抢占
            if (p->coroutine->status != CO_STATUS_RUNNING) {
                RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d, co=%p/%p status=%d, will skip", p->index, p->coroutine, co,
                        p->coroutine->status);
                goto SOLO_UNLOCK_NEXT;
            }

            // 上面虽然判断过超时了，但是这期间 p->coroutine 可以已经替换过了，所以再次判断一下
            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                goto SOLO_UNLOCK_NEXT;
            }

            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine,
                        co, time / 1000 / 1000);
                goto SOLO_UNLOCK_NEXT;
            }

            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d send SIGURG success, wait preempt success", p->index);

            for (int i = 0; i <= 1000; i++) {
                if (!p->can_preempt) {
                    RDEBUGF("[wait_sysmon.thread_locker] solo p_index=%d preempt success, will goto unlocker", p->index);
                    goto SHARE_UNLOCK_NEXT;
                }

                usleep(1 * 100); // 每 0.1 ms 探测一次
            }

        SOLO_UNLOCK_NEXT:
            mutex_unlock(&p->preempt_locker);

            prev = p;
            p = p->next;
            RDEBUGF("[wait_sysmon.thread_locker] solo unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - GC 判断 (每 100ms 进行一次)
        if (gc_eval_count <= 0) {
            runtime_eval_gc();
            gc_eval_count = WAIT_MID_TIME;
        }

        // processor exit 主要和 main coroutine 相关，当 main coroutine 退出后，则整个 wait sysmon 进行退出
        if (processor_get_exit()) {
            RDEBUGF("[wait_sysmon] processor need exit, will exit");
            break;
        }

        gc_eval_count--;

        usleep(5 * 1000); // 5ms
    }

    RDEBUGF("[wait_sysmon] wait sysmon exit success");
}