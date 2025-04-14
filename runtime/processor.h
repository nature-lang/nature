#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include <include/uv.h>
#include <stdint.h>

#include "linkco.h"
#include "nutils/errort.h"
#include "nutils/vec.h"
#include "runtime.h"

#define GC_STW_WAIT_COUNT 25
#define GC_STW_SWEEP_COUNT (GC_STW_WAIT_COUNT * 2)

extern int cpu_count;
extern n_processor_t *processor_index[1024];
extern n_processor_t *processor_list; // 共享协程列表的数量一般就等于线程数量

extern n_processor_t *solo_processor_list; // 独享协程列表其实就是多线程
extern coroutine_t *main_coroutine;

extern mutex_t solo_processor_locker; // 删除 solo processor 需要先获取该锁
extern int solo_processor_count;
extern int64_t coroutine_count;
extern uv_key_t tls_processor_key;
extern uv_key_t tls_coroutine_key;

extern _Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint; // gc 全局 safepoint 标识，通常配合 stw 使用

// processor gc_finished 后新产生的 shade ptr 会存入到该全局工作队列中，在 gc_mark_done 阶段进行单线程处理
extern rt_linked_fixalloc_t global_gc_worklist; // 全局 gc worklist

extern fixalloc_t coroutine_alloc;
extern fixalloc_t processor_alloc;
extern mutex_t cp_alloc_locker;

typedef enum {
    CO_FLAG_SOLO = 1,
    CO_FLAG_SAME = 2,
    CO_FLAG_MAIN = 3,
    CO_FLAG_RTFN = 4, // runtime_fn 不需要扫描 stack
} co_flag_t;

#ifdef __LINUX
#define NO_OPTIMIZE __attribute__((optimize(0)))
#else
#define NO_OPTIMIZE __attribute__((optnone))
#endif

#ifdef __DARWIN
extern void assist_preempt_yield() __asm__("_assist_preempt_yield");
#else
extern void assist_preempt_yield() __asm__("assist_preempt_yield");
#endif

NO_OPTIMIZE void debug_ret(uint64_t rbp, uint64_t ret_addr);

NO_OPTIMIZE void co_preempt_yield();

#define PROCESSOR_FOR(list) for (n_processor_t *p = list; p; p = p->next)

static inline bool processor_need_stw(n_processor_t *p) {
    return p->need_stw > 0 && p->need_stw == p->in_stw;
}

static inline void co_set_status(n_processor_t *p, coroutine_t *co, co_status_t status) {
    assert(p);
    assert(co);

    // solo processor 在 stw 期间禁止切换 co 的状态
    co->status = status;
}

/**
 * yield 统一入口, 避免直接调用 aco_yield
 */
static inline void _co_yield(n_processor_t *p, coroutine_t *co) {
    assert(p);
    assert(co);

    aco_yield1(&co->aco);

    // yield 返回，继续 running
    p->status = P_STATUS_RUNNING;
    p->co_started_at = uv_hrtime();
}

static inline void co_ready(coroutine_t *co) {
    assert(co->p->status != P_STATUS_EXIT);
    co_set_status(co->p, co, CO_STATUS_RUNNABLE);
    rt_linked_fixalloc_push(&co->p->runnable_list, co);
}

static inline void co_yield_runnable(n_processor_t *p, coroutine_t *co) {
    DEBUGF("[runtime.co_yield_runnable] start");
    assert(p);
    assert(co);

    // syscall -> runnable
    co_set_status(p, co, CO_STATUS_RUNNABLE);
    rt_linked_fixalloc_push(&p->runnable_list, co);

    DEBUGF("[runtime.co_yield_runnable] p_index_%d=%d, co=%p, co_status=%d, will yield", p->share, p->index, co,
           co->status);

    _co_yield(p, co);

    // runnable -> syscall
    co_set_status(p, co, CO_STATUS_TPLCALL);
    DEBUGF("[runtime.co_yield_runnable] p_index_%d=%d, co=%p, co_status=%d, yield resume", p->share, p->index, co,
           co->status);
}

static inline void co_yield_waiting(coroutine_t *co, unlock_fn unlock_fn, void *lock_of) {
    assert(co->p);
    assert(co);
    assertf(co->status != CO_STATUS_WAITING, "co already waiting io");

    co->wait_unlock_fn = unlock_fn;
    co->wait_lock = lock_of;

    // 这里作为一个整体，不再允许抢占
    // syscall -> waiting
    co_set_status(co->p, co, CO_STATUS_WAITING);

    _co_yield(co->p, co);

    // waiting -> syscall
    co_set_status(co->p, co, CO_STATUS_TPLCALL);
    DEBUGF("[runtime.co_yield_waiting] p_index_%d=%d, co=%p, co_status=%d, yield resume", co->p->share, co->p->index, co,
           co->status);
}

// locker
void *global_gc_worklist_pop();

void processor_all_need_stop();

void processor_all_start();

bool processor_all_safe();

bool processor_all_wait_safe(int max_count);

void processor_stw_unlock();

void wait_all_gc_work_finished();

/**
 * 阻塞特定时间的网络 io 时间, 如果有 io 事件就绪则立即返回
 * uv_run 有三种模式
 * UV_RUN_DEFAULT: 持续阻塞不返回, 可以使用 uv_stop 安全退出
 * UV_RUN_ONCE: 处理至少活跃的 fd 后返回, 如果没有活跃的 fd 则一直阻塞
 * UV_RUN_NOWAIT: 不阻塞, 如果没有事件就绪则立即返回
 * @param timeout_ms
 * @return
 */
int io_run(n_processor_t *p, uint64_t timeout_ms);

/**
 * runtime_main 会负责调用该方法，该方法读取 cpu 的核心数，然后初始化对应数量的 share_processor
 * 每个 share_processor 可以通过 void* arg 直接找到自己的 share_processor_t 对象
 */
void sched_init();

void sched_run();

n_processor_t *processor_new(int index);

void coroutine_free(coroutine_t *co);

void processor_free(n_processor_t *p);

/**
 * @param fn
 * @param args 元素的类型是 n_union_t 联合类型
 * @param flag
 * @param arg
 * @return
 */
coroutine_t *rt_coroutine_new(void *fn, int64_t flag, n_future_t *fu, void *arg);

coroutine_t *rt_coroutine_async(void *fn, int64_t flag, n_future_t *fu);

void rt_coroutine_return(void *result_ptr);

/**
 * 为 coroutine 选择合适的 processor 绑定，如果是独享 coroutine 则创建一个 solo processor
 */
void rt_coroutine_dispatch(coroutine_t *co);

/**
 * 有 processor_run 调用
 * coroutine 的本质是一个指针，指向了需要执行的代码的 IP 地址。 (aco_create_init 会绑定对应的 fn)
 */
void coroutine_resume(n_processor_t *p, coroutine_t *co);

void co_migrate(aco_t *aco, aco_share_stack_t *new_st);

void rt_coroutine_sleep(int64_t ms);

void rt_coroutine_await(coroutine_t *co);

void rt_coroutine_yield();

void rt_select_block();

void *rt_coroutine_arg();

int64_t rt_processor_index();

void rt_processor_wake(n_processor_t *p);

// ------------ libuv 的一些回调 -----------------------
static void uv_on_timer(uv_timer_t *timer);

#endif // NATURE_PROCESSOR_H
