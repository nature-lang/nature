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
            if (p->exit) {
                continue;
            }

            coroutine_t *co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon.share] p_index=%d no coroutine, will skip", p->index);
                continue;
            }

            if (co->gc_work) {
                RDEBUGF("[wait_sysmon.share] p_index=%d, co=%p is gc_work, will skip", p->index, p->coroutine);
                continue;
            }

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
            int trylock = mutex_times_trylock(&p->disable_preempt_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon.share] trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);
                continue;
            }

            // yield/exit 首先就设置 can_preempt = false, 这里就无法完成。所以 p->coroutine 一旦设置就无法被清空或者切换
            RDEBUGF("[wait_sysmon.share.thread_locker] try disable_preempt_locker success, p_index_%d=%d", p->share, p->index);

            // 避免获取锁期间切换到 gc work 发生抢占
            co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, not co, will skip", p->index);
                goto SHARE_UNLOCK_NEXT;
            }

            if (co->gc_work) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, co=%d is gc_work, will skip", p->index, co->gc_work);
                goto SHARE_UNLOCK_NEXT;
            }

            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                goto SHARE_UNLOCK_NEXT;
            }

            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine,
                        co, time / 1000 / 1000);
                goto SHARE_UNLOCK_NEXT;
            }

            RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu) wait preempt", p->index, (uint64_t)p->thread_id);

            // 基于协作式的调度，必须要等到协程进入代码段才进行抢占, 否则一直等待，最长等待 1s 钟, 然后异常
            // share processor run 可能在不可抢占状态下直接退出, 此时直接结束
            int wait_count = 0;
            while (!p->can_preempt) {
                usleep(1 * 1000);
                wait_count++;
                if (wait_count > PREEMPT_TIMEOUT) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu) deadlock", p->index, (uint64_t)p->thread_id);
                    assert(false && "processor deadlock");
                }

                if (p->exit) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), processor exit, goto unlock", p->index,
                            (uint64_t)p->thread_id);
                    goto SHARE_UNLOCK_NEXT;
                }
            }

            // 允许抢占
            RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu) co=%p run timeout(%lu ms), will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->coroutine, time / 1000 / 1000);

            // 发送信号强制中断线程
            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            // 直接更新 can_preempt 的值以及清空 co_start_t 避免被重复抢占
            RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d send SIGURG success", p->index);
            p->can_preempt = false;
            p->co_started_at = 0;
        SHARE_UNLOCK_NEXT:
            mutex_unlock(&p->disable_preempt_locker);
            RDEBUGF("[wait_sysmon.share.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);
        }

        // - 监控状态处于 running solo processor
        // solo processor 独享线程所以不需要抢占，仅仅是需要 STW 时才进行抢占，由于抢占的目的是 GC 相关, 如果当前 coroutine
        // status=syscall(tpl call) 则不需要抢占，因为在 tpl call 中是不会调整栈空间的，所以 STW 可以放心扫描已经开启写屏障。
        // 在 tpl post hook 中如果检测到 STW 此时 solo processor 延迟进入 STW 状态即可
        processor_t *prev = NULL;
        processor_t *p = solo_processor_list;
        while (p) {
            if (p->exit) {
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
                RDEBUGF("[wait_sysmon.solo] p=%p remove from solo processor list, prev=%p, solo_processor_list=%p", p, prev,
                        solo_processor_list);

                continue;
            }

            // solo processor 仅需要 stw 的时候进行抢占
            if (!processor_get_stw()) {
                RDEBUGF("[wait_sysmon.solo] processor_index=%d, current not need stw, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // need stw ---
            coroutine_t *co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon.solo] p_index=%d no coroutine, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            uint64_t co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.solo] p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);

                prev = p;
                p = p->next;
                continue;
            }

            uint64_t time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.solo] p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine, co,
                        time / 1000 / 1000);

                prev = p;
                p = p->next;
                continue;
            }

            // 既然已经超时了，那就先获取锁，避免从 can_preempt true 变成 false
            RDEBUGF("[wait_sysmon.solo] need stw and runtime timeout, will get locker and preempt, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            int trylock = mutex_times_trylock(&p->disable_preempt_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon.solo] trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

                prev = p;
                p = p->next;
                continue;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] get locker, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

            co = p->coroutine;
            if (!co) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] processor_index=%d, not co, will skip", p->index);
                goto SOLO_UNLOCK_NEXT;
            }

            // 只有 running 状态才需要进行抢占, 由于获取了锁，所以不会进入到 syscall 状态
            if (co->status != CO_STATUS_RUNNING) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] processor_index=%d, co=%p, co_status=%d not running, will skip", p->index, co,
                        co->status);
                goto SOLO_UNLOCK_NEXT;
            }

            assert(p->can_preempt == true);

            // 上面虽然判断过超时了，但是这期间 p->coroutine 可以已经替换过了，所以再次判断一下
            co_start_at = p->co_started_at;
            if (co_start_at == 0) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, co=%p/%p co_stared_at = 0, will skip", p->index, p->coroutine, co);
                goto SOLO_UNLOCK_NEXT;
            }

            time = (uv_hrtime() - co_start_at);
            if (time < CO_TIMEOUT) {
                RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d, co=%p/%p run not timeout(%lu ms), will skip", p->index, p->coroutine,
                        co, time / 1000 / 1000);
                goto SOLO_UNLOCK_NEXT;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu) co=%p run timeout(%lu ms), will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->coroutine, time / 1000 / 1000);

            if (pthread_kill(p->thread_id, SIGURG) != 0) {
                assert(false && "error sending SIGURG to thread");
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d send SIGURG success", p->index);
            p->can_preempt = false;
            p->co_started_at = 0;

        SOLO_UNLOCK_NEXT:
            mutex_unlock(&p->disable_preempt_locker);

            RDEBUGF("[wait_sysmon.solo.thread_locker] unlocker, p_index_%d=%d", p->share, p->index);
           
            prev = p;
            p = p->next;
        }

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