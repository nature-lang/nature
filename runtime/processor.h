#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include <include/uv.h>
#include <stdint.h>

#include "linkco.h"
#include "nutils/errort.h"
#include "nutils/vec.h"
#include "runtime.h"

#define ASYNC_HANDLE_FREELIST_MAX 1000
#define ASYNC_HANDLE_FREELIST_MIN 100

#define GC_STW_WAIT_COUNT 25
#define GC_STW_SWEEP_COUNT (GC_STW_WAIT_COUNT * 2)

typedef void (*async_fn)(void *, void *, void *);

typedef struct async_handle_t {
    struct async_handle_t *next;
    void *arg1;
    void *arg2;
    void *arg3;
    async_fn fn;
} async_handle_t;

typedef struct {
    async_handle_t *async_queue; // 单向链表
    pthread_mutex_t async_lock; // 链表 lock
    _Atomic int32_t loop_owner;
    uv_timer_t timer;

    uv_async_t async_handle;
    async_handle_t *freelist; // 空闲的 handle 列表
    pthread_mutex_t freelist_lock;
    int freelist_count;

    _Atomic int64_t async_handle_count;
    uv_async_t async_weak; // 使用 send 快速唤醒
} global_handle_t;

extern global_handle_t global;

// 全局 async 队列变量
extern uv_loop_t global_loop; // uv loop 事件循环

extern _Atomic uint64_t race_detector_counter;

void global_loop_run(int loop_timeout_ms);

void global_loop_init();

void global_async_queue_push(async_fn fn, void *arg);

void global_async_queue_process_cb(uv_async_t *handle);

void global_async_weak_cb(uv_async_t *handle);

extern int cpu_count;
extern n_processor_t *processor_index[1024];
extern n_processor_t *processor_list; // 共享协程列表的数量一般就等于线程数量

//extern n_processor_t *solo_processor_list; // 独享协程列表其实就是多线程
extern bool main_coroutine_exited;

//extern mutex_t solo_processor_locker; // 删除 solo processor 需要先获取该锁
extern int64_t coroutine_count;
extern uv_key_t tls_processor_key;
extern uv_key_t tls_coroutine_key;

extern _Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint; // gc 全局 safepoint 标识，通常配合 stw 使用

typedef struct {
    uint64_t value; // 8 bytes
    uint8_t pad[120]; // 56 bytes padding
} aligned_page_t;

extern __attribute__((aligned(128))) aligned_page_t global_safepoint;

// processor gc_finished 后新产生的 shade ptr 会存入到该全局工作队列中，在 gc_mark_done 阶段进行单线程处理
extern rt_linked_fixalloc_t global_gc_worklist; // 全局 gc worklist

extern fixalloc_t coroutine_alloc;
extern fixalloc_t processor_alloc;
extern mutex_t cp_alloc_locker;

extern uint64_t assist_preempt_yield_ret_addr;

typedef enum {
    //    CO_FLAG_SOLO = 1, // 暂时取消
    CO_FLAG_SAME = 2,
    CO_FLAG_MAIN = 3,
    CO_FLAG_RTFN = 4, // runtime_fn 不需要扫描 stack
    CO_FLAG_DIRECT = 5, // 直接调用 fn, 而不需要通过 n_fn_t 进行提取
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

static inline void co_set_status(n_processor_t *p, coroutine_t *co, co_status_t status) {
    assert(p);
    assert(co);

    //    TDEBUGF("[co_set_status] co=%p set status %d", co, status);
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
    if (co->p->status == P_STATUS_EXIT) {
        return;
    }
    co_set_status(co->p, co, CO_STATUS_RUNNABLE);
    rt_linked_fixalloc_push(&co->p->runnable_list, co);
}

//#define co_ready(co)                                          \
//    {                                                         \
//        coroutine_t *_co = (coroutine_t *) co;                \
//        TDEBUGF("[co_ready] ready %p, fn %s", co, __func__);   \
//        assert(_co->p->status != P_STATUS_EXIT);              \
//        co_set_status(_co->p, _co, CO_STATUS_RUNNABLE);       \
//        rt_linked_fixalloc_push(&_co->p->runnable_list, _co); \
//    }

static inline void co_yield_runnable(n_processor_t *p, coroutine_t *co) {
    DEBUGF("[runtime.co_yield_runnable] start");
    assert(p);
    assert(co);

    // syscall -> runnable
    co_set_status(p, co, CO_STATUS_RUNNABLE);
    rt_linked_fixalloc_push(&p->runnable_list, co);

    DEBUGF("[runtime.co_yield_runnable] p_index=%d, co=%p, co_status=%d, will yield", p->index, co,
           co->status);

    _co_yield(p, co);

    // runnable -> syscall
    co_set_status(p, co, CO_STATUS_TPLCALL);
    DEBUGF("[runtime.co_yield_runnable] p_index=%d, co=%p, co_status=%d, yield resume", p->index, co,
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
    DEBUGF("[runtime.co_yield_waiting] p_index=%d, co=%p, set co_status=%d, will yield", co->p->index, co,
           co->status);

    _co_yield(co->p, co);

    // waiting -> syscall
    co_set_status(co->p, co, CO_STATUS_RUNNING);
    DEBUGF("[runtime.co_yield_waiting] p_index=%d, co=%p, co_status=%d, yield resume", co->p->index, co,
           co->status);
}

/**
 * 竞态检测器：使用 CAS 操作检测并发竞争
 * 如果 CAS 失败，说明其他线程修改了计数器，存在竞争
 */
static inline void race_detector_check(const char *func, const char *file, int line) {
    uint64_t expected = atomic_load(&race_detector_counter);
    uint64_t desired = expected + 1;

    // 如果 CAS 失败，说明有其他线程在同时修改这个值
    if (!atomic_compare_exchange_strong(&race_detector_counter, &expected, desired)) {
        assertf(false, "[RACE DETECTED] at %s() [%s:%d] - expected: %lu, actual: %lu",
                func, file, line, desired, expected);
    }
}

#define RACE() race_detector_check(__func__, __FILE__, __LINE__);

// locker
void *global_gc_worklist_pop();

void processor_all_need_stop();

void processor_all_start();

bool processor_all_safe();

bool processor_all_wait_safe(int max_count);

void wait_all_gc_work_finished();

/**
 * runtime_main 会负责调用该方法，该方法读取 cpu 的核心数，然后初始化对应数量的 share_processor
 * 每个 share_processor 可以通过 void* arg 直接找到自己的 share_processor_t 对象
 */
void sched_init(bool use_t0);

void fn_depend_init(bool use_t0);

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

void rt_coroutine_async2(void *fn, int64_t flag, bool is_direct);

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

// ------------ libuv 的一些回调 -----------------------
static void sleep_timer_cb(uv_timer_t *timer);

void global_async_send(void *fn, void *arg1, void *arg2, void *arg3);

void global_waiting_send(void *fn, void *arg1, void *arg2, void *arg3);

#endif // NATURE_PROCESSOR_H
