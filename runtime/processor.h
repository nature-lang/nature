#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include <stdint.h>
#include <uv.h>

#include "nutils/errort.h"
#include "nutils/vec.h"
#include "runtime.h"

extern int cpu_count;
extern processor_t *share_processor_list; // 共享协程列表的数量一般就等于线程数量
extern processor_t *solo_processor_list;  // 独享协程列表其实就是多线程
extern mutex_t solo_processor_locker;     // 删除 solo processor 需要先获取该锁
extern int solo_processor_count;          // 累计数量
extern uv_key_t tls_processor_key;
extern uv_key_t tls_coroutine_key;

// processor gc_finished 后新产生的 shade ptr 会存入到该全局工作队列中，在 gc_mark_done 阶段进行单线程处理
extern rt_linked_t global_gc_worklist; // 全局 gc worklist
extern mutex_t global_gc_locker;       // 全局 gc locker

extern bool processor_need_stw;  // 全局 STW 标识
extern bool processor_need_exit; // 全局 STW 标识

extern fixalloc_t coroutine_alloc;
extern fixalloc_t processor_alloc;
extern mutex_t cp_alloc_locker;

extern void async_preempt() __asm__("async_preempt");

__attribute__((optimize(0))) void debug_ret();

__attribute__((optimize(0))) void co_preempt_yield();

#define PROCESSOR_FOR(list) for (processor_t *p = list; p; p = p->next)

static inline void processor_set_status(processor_t *p, p_status_t status) {
    assert(p);

    mutex_lock(&p->thread_locker);

    if (!p->share && status == P_STATUS_RUNNING) {
        // 如果想要切换到 running 状态，则还需要获取 gc_stw_locker
        // 避免在 gc_stw_locker 期间进入到 user_code
        mutex_lock(&p->gc_stw_locker);
    }

    p->status = status;

    if (!p->share && status == P_STATUS_RUNNING) {
        // 如果想要切换到 running 状态，则还需要获取 gc_stw_locker
        // 避免在 gc_stw_locker 期间进入到 user_code
        mutex_unlock(&p->gc_stw_locker);
    }

    mutex_unlock(&p->thread_locker);
}

static inline void co_set_status(processor_t *p, coroutine_t *co, co_status_t status) {
    assert(p);
    assert(co);

    // solo processor 在 stw 期间禁止切换 co 的状态
    co->status = status;
}
/**
 * 设置为可以抢占没有什么竞态关系，直接可以抢占
 * @param p
 */
// static inline void enable_preempt(processor_t *p) {
//     p->can_preempt = true;
// }

/**
 * 调用者希望设置为不可抢占后再进行后续的工作，如果已经是不可抢占状态则直接返回，所以当前函数可以重入
 * 由于 sysmon 会持有锁的情况下，等待 preempt = true, 所以如果当前 p.preempt = true 想要更新为 false 时，必须要抢到锁
 * 避免在 sysmon 抢占期间，更新 preempt = false 导致抢占异常
 * @param p
 */
// static inline void disable_preempt(processor_t *p) {
//     if (p->can_preempt == false) {
//         return;
//     }
//
//     assert(p->can_preempt == true);
//     // 如果 sysmon 持有了锁，则 mutex_lock 会阻塞，此时可以进行安全的抢占
//     mutex_lock(&p->thread_locker);
//     p->can_preempt = false;
//     mutex_unlock(&p->thread_locker);
// }

/**
 * yield 统一入口, 避免直接调用 aco_yield
 */
static inline void _co_yield(processor_t *p, coroutine_t *co) {
    assert(p);
    assert(co);

    aco_yield1(&co->aco);
}

static inline void co_yield_runnable(processor_t *p, coroutine_t *co) {
    DEBUGF("[runtime.co_yield_runnable] start");
    assert(p);
    assert(co);

    // syscall -> runnable
    mutex_lock(&p->thread_locker);
    co->status = CO_STATUS_RUNNABLE;
    rt_linked_push(&p->runnable_list, co);
    mutex_unlock(&p->thread_locker);

    DEBUGF("[runtime.co_yield_runnable] p_index_%d=%d, co=%p, co_status=%d, will yield", p->share, p->index, co, co->status);

    _co_yield(p, co);

    // runnable -> syscall
    co_set_status(p, co, CO_STATUS_SYSCALL);
    DEBUGF("[runtime.co_yield_runnable] p_index_%d=%d, co=%p, co_status=%d, yield resume", p->share, p->index, co, co->status);
}

static inline void co_yield_waiting(processor_t *p, coroutine_t *co) {
    assert(p);
    assert(co);

    // 这里作为一个整体，不再允许抢占
    // syscall -> waiting
    co_set_status(p, co, CO_STATUS_WAITING);

    _co_yield(p, co);

    // waiting -> syscall
    co_set_status(p, co, CO_STATUS_SYSCALL);
}

// locker
void *global_gc_worklist_pop();

void processor_all_stop_the_world();

void processor_all_start_the_world();

bool processor_all_safe();

void processor_all_wait_safe();

void processor_stw_unlock();

void wait_all_gc_work_finished();

/**
 *  processor 停止调度
 */
bool processor_get_exit();

void processor_set_exit();

/**
 * 阻塞特定时间的网络 io 时间, 如果有 io 事件就绪则立即返回
 * uv_run 有三种模式
 * UV_RUN_DEFAULT: 持续阻塞不返回, 可以使用 uv_stop 安全退出
 * UV_RUN_ONCE: 处理至少活跃的 fd 后返回, 如果没有活跃的 fd 则一直阻塞
 * UV_RUN_NOWAIT: 不阻塞, 如果没有事件就绪则立即返回
 * @param timeout_ms
 * @return
 */
int io_run(processor_t *p, uint64_t timeout_ms);

/**
 * runtime_main 会负责调用该方法，该方法读取 cpu 的核心数，然后初始化对应数量的 share_processor
 * 每个 share_processor 可以通过 void* arg 直接找到自己的 share_processor_t 对象
 */
void processor_init();

processor_t *processor_new(int index);

void processor_free(processor_t *);

/**
 * @param fn
 * @param args 元素的类型是 n_union_t 联合类型
 * @param solo
 * @return
 */
coroutine_t *coroutine_new(void *fn, n_vec_t *args, bool solo, bool main);

/**
 * 为 coroutine 选择合适的 processor 绑定，如果是独享 coroutine 则创建一个 solo processor
 */
void coroutine_dispatch(coroutine_t *co);

/**
 * 有 processor_run 调用
 * coroutine 的本质是一个指针，指向了需要执行的代码的 IP 地址。 (aco_create_init 会绑定对应的 fn)
 */
void coroutine_resume(processor_t *p, coroutine_t *co);

void pre_tpl_hook(char *target);

void post_tpl_hook(char *target);

void co_migrate(aco_t *aco, aco_share_stack_t *new_st);

#endif // NATURE_PROCESSOR_H
