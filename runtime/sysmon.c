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
                RDEBUGF("[wait_sysmon.share] p_index=%d(%lu), co=%p is gc_work, will skip", p->index, (uint64_t)p->thread_id, p->coroutine);
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
            RDEBUGF("[wait_sysmon.share.thread_locker] try disable_preempt_locker success wait preempt, p_index_%d=%d", p->share, p->index);

            // 基于协作式的调度，必须要等到协程进入代码段才进行抢占, 否则一直等待，最长等待 1s 钟, 然后异常
            // 1s 可能发生很多事情，所以需要关注一些关键的状态变化
            // - 没有可用的 runnable, 导致一致在 processor run 循环
            // - main exit, processor 退出
            // - 可能调度到 gc work(但是 gc work 肯定运行不到 1s 钟)
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

                if (!p->coroutine) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), no coroutine and maybe exit, goto unlock", p->index,
                            (uint64_t)p->thread_id);
                    goto SHARE_UNLOCK_NEXT;
                }
            }

            // 成功获取到 locker 并且当前 can_preempt = true 允许抢占，
            // 但是这期间可能已经发生了 co 切换，所以再次进行 co_start 超时的判断
            coroutine_t *post_co = p->coroutine;
            if (!post_co) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, not co, will skip", p->index);
                goto SHARE_UNLOCK_NEXT;
            }

            // 直接使用新的 co 于原始 co 进行对比
            if (post_co != co) {
                RDEBUGF("[wait_sysmon.share.thread_locker] processor_index=%d, co=%d is gc_work, will skip", p->index, co->gc_work);
                goto SHARE_UNLOCK_NEXT;
            }

            // 可能发生了连续切换, 所以重复进行时间的判定
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
        RDEBUGF("[wait_sysmon.solo] wait locker");
        mutex_lock(&solo_processor_locker);
        RDEBUGF("[wait_sysmon.solo] get locker");

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
                RDEBUGF("[wait_sysmon.solo] p=%p remove from solo processor list, prev=%p, solo_processor_list=%p", exited, prev,
                        solo_processor_list);

                continue;
            }

            // 虽然有锁的方式了，但是普通 STW 也是需要的，避免 solo processor 在 user code 期间的 stack/reg 轮换 obj
            // 导致 scan_stack 异常, 所以现在是三重安全
            // - 持有 stw locker, syscall 期间如果操作了内存同样也会被拦截
            // - 位于 syscall, 此时只要退出 syscall 就会触发 post_tpl_lock stw
            // - yield 到 processor 进入到真实的 stw, 此处能抢占的点只有进入到 user code 时

            // solo processor 仅需要 stw 的时候进行抢占
            if (!processor_get_stw()) {
                RDEBUGF("[wait_sysmon.solo] processor_index=%d, current not need stw, will skip", p->index);

                prev = p;
                p = p->next;
                continue;
            }

            // need stw ---
            // coroutine_t *co = p->coroutine;
            // if (!co) {
            //     RDEBUGF("[wait_sysmon.solo] p_index=%d no coroutine, will skip", p->index);
            //
            //     prev = p;
            //     p = p->next;
            //     continue;
            // }

            // 当前有 co 需要调度
            RDEBUGF("[wait_sysmon.solo] need stw, will get locker and preempt, p_index_%d=%d(%lu)", p->share, p->index,
                    (uint64_t)p->thread_id);

            int trylock = mutex_times_trylock(&p->disable_preempt_locker, 10);
            if (trylock == -1) {
                RDEBUGF("[wait_sysmon.solo] trylock failed, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

                prev = p;
                p = p->next;
                continue;
            }

            RDEBUGF("[wait_sysmon.solo.thread_locker] get locker, p_index_%d=%d(%lu)", p->share, p->index, (uint64_t)p->thread_id);

            // 等待可以抢占
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
                    goto SOLO_UNLOCK_NEXT;
                }

                coroutine_t *co = p->coroutine;
                if (!co) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), no coroutine maybe exit, goto unlock", p->index,
                            (uint64_t)p->thread_id);
                    goto SOLO_UNLOCK_NEXT;
                }

                if (co->status == CO_STATUS_SYSCALL) {
                    RDEBUGF("[wait_sysmon.share.thread_locker] p_index=%d(%lu), goroutine in syscall, this is a safe status, goto unlock",
                            p->index, (uint64_t)p->thread_id);
                    goto SOLO_UNLOCK_NEXT;
                }
            }

            // 当前属于可以抢占状态, 由于获取了 disable_preempt_locker, 所以此时无法在切换到 can_preempt=true
            assert(p->can_preempt == true);

            // 当前是可抢占状态则必定存在一个 co, 且 stw 期间不会发生轮换
            // 既然已经发生了 stw, 那 co 就不可能发生切换，除非 stw 已经停止了
            assert(p->coroutine);

            RDEBUGF("[wait_sysmon.solo.thread_locker] p_index=%d(%lu) co=%p run in user code will send SIGURG", p->index,
                    (uint64_t)p->thread_id, p->coroutine);

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