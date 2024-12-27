#include "processor.h"

#include <uv.h>
#include <ucontext.h>

#include "runtime.h"
#include "runtime/nutils/http.h"

int cpu_count;
bool processor_need_exit;

n_processor_t *share_processor_index[1024] = {0};
n_processor_t *share_processor_list; // 共享协程列表的数量一般就等于线程数量
n_processor_t *solo_processor_list; // 独享协程列表其实就是多线程
mutex_t solo_processor_locker; // 删除 solo processor 需要先获取该锁
int coroutine_count; // coroutine 累计数量

int solo_processor_count; // 累计数量
rt_linked_fixalloc_t global_gc_worklist;

uv_key_t tls_processor_key = 0;
uv_key_t tls_coroutine_key = 0;

fixalloc_t coroutine_alloc;
fixalloc_t processor_alloc;
mutex_t cp_alloc_locker;

linkco_t *global_linkco_cache;
mutex_t global_linkco_locker;

// 这里直接引用了 main 符号进行调整，ct 不需要在寻找 main 对应到函数位置了
extern int main();

NO_OPTIMIZE void debug_ret(uint64_t rbp, uint64_t ret_addr) {
    // 这里也不安全了呀，毕竟没有回去。。所以为什么会超时,为啥还需要抢占。这 co 都换新了吧
    // DEBUGF("[runtime.debug_ret] rbp=%p, ret_addr=%p", (void *)rbp, (void *)ret_addr);
}

/**
 * 在 user_code 期间的超时抢占
 */
NO_OPTIMIZE void co_preempt_yield() {
    RDEBUGF("[runtime.co_preempt_yield] start, %lu", (uint64_t) uv_thread_self());

    n_processor_t *p = processor_get();
    assert(p);
    coroutine_t *co = p->coroutine;
    assert(co);

    RDEBUGF(
        "[runtime.co_preempt_yield] p_index_%d=%d(%d), co=%p, p_status=%d, scan_ret_addr=%p, scan_offset=%lu, will yield",
        p->share,
        p->index, p->status, co, co->status, (void *) co->scan_ret_addr, co->scan_offset);

    p->status = P_STATUS_PREEMPT;

    co->status = CO_STATUS_RUNNABLE;
    rt_linked_fixalloc_push(&p->runnable_list, co);

    RDEBUGF("[runtime.co_preempt_yield.thread_locker] co=%p push and update status success", co);
    _co_yield(p, co);

    assert(p->status == P_STATUS_RUNNABLE);

    // 接下来将直接 return 到用户态，不经过 post_tpl_hook, 所以直接更新为允许抢占
    // yield 切换回了用户态，此时允许抢占，所以不能再使用 RDEBUG, 而是 DEBUG
    DEBUGF("[runtime.co_preempt_yield] yield resume end, will set running, p_index_%d=%d, p_status=%d co=%p, p->co=%p",
           p->share, p->index,
           p->status, co, p->coroutine);

    co_set_status(p, co, CO_STATUS_RUNNING);
    processor_set_status(p, P_STATUS_RUNNING);
}

/**
 * 不能在信号处理函数中调用不可重入函数，包括 malloc/free/printf 等
 * @param sig
 * @param info
 * @param ucontext
 */
NO_OPTIMIZE static void thread_handle_sig(int sig, siginfo_t *info, void *ucontext) {
    ucontext_t *ctx = ucontext;
    n_processor_t *p = processor_get();
    assert(p);
    coroutine_t *co = p->coroutine;
    assert(co);

#ifdef __AMD64


#ifdef __LINUX
#define CTX_RSP ctx->uc_mcontext.gregs[REG_RSP]
#define CTX_RIP ctx->uc_mcontext.gregs[REG_RIP]
#else  // darwin
#define CTX_RSP ctx->uc_mcontext->__ss.__rsp
#define CTX_RIP ctx->uc_mcontext->__ss.__rip
#endif

    // int REG_RBP = 10;
    int REG_RSP = 15;
    int REG_RIP = 16;

    uint64_t *rsp = (uint64_t *) CTX_RSP;
    uint64_t rip = CTX_RIP;

    // rip 已经存到了 rsp 里面,只要能定位到 bp_offset 就行了。
    fndef_t *fn = find_fn(rip);
    if (fn) {
        // 基于当前 rsp scan
        uint64_t sp_addr = (uint64_t) rsp;
        co->scan_ret_addr = rip;
        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - sp_addr;
    } else {
        // c 语言段被抢占，采取保守的扫描策略(使用 ret_addr = 0 来识别)
        co->scan_ret_addr = 0;
        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - (uint64_t) rsp;
    }

    // 由于被抢占的函数可以会在没有 sub 保留 rsp 的情况下使用 rsp-0x10 这样的空间地址
    // 所以需要为 rsp 预留足够的栈空间给被抢占的函数, 避免后续的操作污染被抢占的函数
    rsp -= 128;// 一个指针是 8byte, 所以这里是 128 * 8 = 1024 个字节

    // push rip
    rsp--;// 栈中预留返回地址
    *rsp = rip;

    RDEBUGF("[runtime.thread_handle_sig] rip=%p save to %p, co=%p, scan_ret_addr=%p, scan_offset=%lu, fn=%p",
            (void *) rip, rsp, co,
            (void *) co->scan_ret_addr, co->scan_offset, fn);

    CTX_RSP = (int64_t) rsp;
    CTX_RIP = (int64_t) async_preempt;

#elif defined(__ARM64)

#ifdef __LINUX
#define CTX_SP ctx->uc_mcontext.sp
#define CTX_PC ctx->uc_mcontext.pc
#define CTX_LR ctx->uc_mcontext.regs[30]  // x30 是链接寄存器(LR)
#else
#define CTX_SP ctx->uc_mcontext->__ss.__sp
#define CTX_PC ctx->uc_mcontext->__ss.__pc
#define CTX_LR ctx->uc_mcontext->__ss.__lr
#endif

    uint64_t *sp = (uint64_t *) CTX_SP;
    uint64_t pc = CTX_PC;
    uint64_t lr = CTX_LR; // 保存返回地址的链接寄存器

    // 查找当前执行的函数
    fndef_t *fn = find_fn(pc);
    if (fn) {
        // 基于当前 sp 计算扫描偏移
        uint64_t sp_addr = (uint64_t) sp;
        co->scan_ret_addr = pc;
        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - sp_addr;
    } else {
        // c 语言段被抢占,采用保守扫描策略
        co->scan_ret_addr = 0;
        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - (uint64_t) sp;
    }

    // 为被抢占的函数预留栈空间
    sp -= 128; // 1024 字节的安全区域

    // ARM64 中保存返回地址和链接寄存器
    sp -= 2; // 预留两个位置,一个给 pc,一个给 lr
    sp[0] = pc;
    sp[1] = lr;

    RDEBUGF("[runtime.thread_handle_sig] pc=%p lr=%p save to %p, co=%p, scan_ret_addr=%p, scan_offset=%lu, fn=%p",
            (void *)pc, (void *)lr, sp, co,
            (void *)co->scan_ret_addr, co->scan_offset, fn);

    // 更新上下文
    CTX_SP = (uint64_t) sp;
    CTX_PC = (uint64_t) async_preempt;
    CTX_LR = (uint64_t) async_preempt; // 确保返回地址也指向抢占处理函数

#else
#error "platform no support yet"
#endif
}

static void processor_uv_close(n_processor_t *p) {
    // 关闭 uv_loop
    RDEBUGF("[runtime.processor_uv_close] will close loop=%p, loop_req_count=%u, p_index_%d=%d", &p->uv_loop,
            p->uv_loop.active_reqs.count,
            p->share, p->index);
    uv_close((uv_handle_t *) &p->timer, NULL); // io_run 等待 close 完成！

    uv_run(&p->uv_loop, UV_RUN_DEFAULT); // 等待上面注册的 uv_close 完成

    int result = uv_loop_close(&p->uv_loop);

    if (result != 0) {
        RDEBUGF("[runtime.processor_uv_close] uv loop close failed, code=%d, msg=%s, p_index_%d=%d", result,
                uv_strerror(result), p->share,
                p->index);
        assert(false && "uv loop close failed");
    }

    RDEBUGF("[runtime.processor_uv_close] processor uv close success p_index_%d=%d", p->share, p->index);
}

NO_OPTIMIZE static void coroutine_wrapper() {
    coroutine_t *co = aco_get_arg();
    assert(co);
    n_processor_t *p = processor_get();
    assert(p);

    DEBUGF("[runtime.coroutine_wrapper] p_index_%d=%d, p_status=%d co=%p, fn=%p main=%d, gc_work=%d", p->share,
           p->index, p->status, co, co->fn, co->main, co->gc_work);

    co_set_status(p, co, CO_STATUS_RUNNING);
    processor_set_status(p, P_STATUS_RUNNING);

    if (in_heap((addr_t) co->fn)) {
        assertf(span_of((addr_t) co->fn), "co=%p fn=%p not found in heap, maybe swept", co, co->fn);
    }

    assert((addr_t) co->fn > 0);

    ((void_fn_t) co->fn)();

    DEBUGF(
        "[runtime.coroutine_wrapper] user fn completed, p_index_%d=%d co=%p, main=%d, gc_work=%d,err=%p, will set status to rtcall",
        p->share, p->index, co,
        co->main, co->gc_work, co->error);
    processor_set_status(p, P_STATUS_RTCALL);

    if (co->main) {
        // 通知所有协程退出
        processor_set_exit();
        DEBUGF("[runtime.coroutine_wrapper] p_index_%d=%d co=%p, main coroutine exit, set processor_need_exit=true",
               p->share, p->index,
               co);
    }

    // coroutine 即将退出，讲错误保存在 co->future 中
    if (co->error && co->error->has && co->future) {
        co->future->error = co->error; // 将 co error 赋值给 co->future 避免被 gc
    }

    // co->await_co 可能是随时写入的，所以需要 dead_locker 保证同步
    mutex_lock(&co->dead_locker);
    if (co->await_co) {
        coroutine_t *await_co = co->await_co;

        co_set_status(p, await_co, CO_STATUS_RUNNABLE);
        rt_linked_fixalloc_push(&await_co->p->runnable_list, await_co);
    } else {
        if (co->error && co->error->has) {
            coroutine_dump_error(co, co->error);
            exit(EXIT_FAILURE);
        }
    }

    co_set_status(p, co, CO_STATUS_DEAD);
    mutex_unlock(&co->dead_locker);

    DEBUGF("[runtime.coroutine_wrapper] co=%p will dead", co);
    aco_exit1(&co->aco);
}

/**
 * 必须在用户态/或者 processor 还没有开始 run 之前初始化
 * @param p
 * @param co
 */
static void coroutine_aco_init(n_processor_t *p, coroutine_t *co) {
    assert(p);
    assert(co);
    if (co->aco.inited) {
        return;
    }

    assert(p->main_aco.inited);
    assert(p->share_stack.sz > 0);

    aco_create_init(&co->aco, &p->main_aco, &p->share_stack, 0, coroutine_wrapper, co);
}

void processor_all_stop_the_world() {
    uint64_t stw_time = uv_hrtime();
    PROCESSOR_FOR(share_processor_list) {
        p->need_stw = stw_time;
    }

    mutex_lock(&solo_processor_locker);
    PROCESSOR_FOR(solo_processor_list) {
        p->need_stw = stw_time;
    }
    mutex_unlock(&solo_processor_locker);
}

void processor_all_start_the_world() {
    PROCESSOR_FOR(share_processor_list) {
        p->need_stw = 0;
        p->safe_point = 0;
        RDEBUGF("[runtime_gc.processor_all_start_the_world] p_index_%d=%d, thread_id=%lu set safe_point=false",
                p->share, p->index,
                (uint64_t) p->thread_id);
    }

    mutex_lock(&solo_processor_locker);
    PROCESSOR_FOR(solo_processor_list) {
        p->need_stw = 0;
        p->safe_point = 0;
        mutex_unlock(&p->gc_stw_locker);

        TRACEF(
            "[runtime_gc.processor_all_start_the_world] p_index_%d=%d, thread_id=%lu set safe_point=false and unlock gc_stw_locker",
            p->share, p->index, (uint64_t) p->thread_id);
    }
    mutex_unlock(&solo_processor_locker);

    DEBUGF("[runtime_gc.processor_all_start_the_world] all processor stw completed");
}

void uv_stop_callback(uv_timer_t *timer) {
    n_processor_t *p = timer->data;
    // DEBUGF("[runtime.io_run.uv_stop_callback] loop=%p, p_index=%d", timer->loop, p->index);

    uv_timer_stop(timer);
    // uv_close((uv_handle_t *)timer, NULL);

    uv_stop(timer->loop);
}

/**
 * timeout_ms 推荐 5ms ~ 10ms
 */
int io_run(n_processor_t *p, uint64_t timeout_ms) {
    // 初始化计时器 (这里不会发生栈切换，所以可以在栈上直接分配)
    p->timer.data = p;

    // 设置计时器超时回调，这将在超时后停止事件循环
    uv_timer_start(&p->timer, uv_stop_callback, timeout_ms, 0); // 只触发一次


    // DEBUGF("[runtime.io_run] uv_run start, p_index=%d, loop=%p", p->index, p->uv_loop);
    return uv_run(&p->uv_loop, UV_RUN_DEFAULT);
}

/**
 * 检测 coroutine 当前是否需要单独线程调度，如果不需要单独线程则直接在当前线程进行 aco_resume
 */
void coroutine_resume(n_processor_t *p, coroutine_t *co) {
    assert(co->status == CO_STATUS_RUNNABLE && "coroutine status must be runnable");

    // 首次 resume 需要进行初始化
    if (!co->aco.inited) {
        coroutine_aco_init(p, co);
    }

    // 将 RIP 指针移动用户代码片段中
    RDEBUGF("[coroutine_resume] aco_resume will start,co=%p,main=%d,aco=%p, sstack=%p(%zu), p_index_%d=%d(%d)", co,
            co->main, &co->aco,
            co->aco.save_stack.ptr, co->aco.save_stack.sz, p->share, p->index, p->status);

    // 获取锁才能切换协程并更新状态
    mutex_lock(&p->thread_locker);

    p->coroutine = co;
    // - 再 tls 中记录正在运行的协程
    uv_key_set(&tls_coroutine_key, co);
    co->p = p; // 运行前进行绑定，让 coroutine 在运行中可以准确的找到 processor
    p->status = P_STATUS_RUNNABLE;
    mutex_unlock(&p->thread_locker);

    aco_resume(&co->aco);

    // rtcall/tplcall 都可以无锁进入到 dispatch 状态，dispatch 状态是一个可以安全 stw 的状态
    TRACEF(
        "[coroutine_resume] resume back, p_index_%d=%d(%d), co=%p, status=%d, gc_work=%d, scan_ret_addr=%p, scan_offset=%lu",
        p->share, p->index, p->status, co, co->status, co->gc_work, (void *) co->scan_ret_addr, co->scan_offset);

    // running -> dispatch
    assert(co->status != CO_STATUS_RUNNING);

    if (co->yield_lock) {
        pthread_mutex_unlock(co->yield_lock);
        co->yield_lock = NULL;
    }

    p->co_started_at = 0;
    processor_set_status(p, P_STATUS_DISPATCH);

    uint64_t time = (uv_hrtime() - p->co_started_at) / 1000 / 1000;
    RDEBUGF("[coroutine_resume] resume back, co=%p, aco=%p, run_time=%lu ms, gc_work=%d", co, &co->aco, time,
            co->gc_work);
}

// handle by thread
static void processor_run(void *raw) {
    n_processor_t *p = raw;
    RDEBUGF("[runtime.processor_run] start, p_index=%d, addr=%p, share=%d, loop=%p", p->index, p, p->share,
            &p->uv_loop);

    processor_set_status(p, P_STATUS_DISPATCH);

    // 初始化 aco 和 main_co
    aco_thread_init(NULL);
    aco_create_init(&p->main_aco, NULL, NULL, 0, NULL, NULL);

    aco_share_stack_init(&p->share_stack, 0);

    // - 初始化 libuv
    uv_loop_init(&p->uv_loop);
    uv_timer_init(&p->uv_loop, &p->timer);

    // 注册线程信号监听, 用于抢占式调度
    // 分配备用信号栈
    //    stack_t *ss = NEW(stack_t);
    //    ss->ss_sp = mallocz(SIGSTKSZ);
    //    ss->ss_size = SIGSTKSZ;
    //    ss->ss_flags = 0;
    //    sigaltstack(ss, NULL); // 配置为信号处理函数使用栈

    p->sig.sa_flags = SA_SIGINFO | SA_RESTART;
    p->sig.sa_sigaction = thread_handle_sig;

    if (sigaction(SIGURG, &p->sig, NULL) == -1) {
        assert(false && "sigaction failed");
    }

    // 将 p 存储在线程维度全局遍历中，方便直接在 coroutine 运行中读取相关的 processor
    uv_key_set(&tls_processor_key, p);

    int64_t run_count = 0;

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        // TRACEF("[runtime.processor_run] handle, p_index_%d=%d", p->share, p->index);
        // - stw
        if (p->need_stw > 0) {
        STW_WAIT:
            RDEBUGF("[runtime.processor_run] need stw, set safe_point=need_stw(%lu), p_index_%d=%d", p->need_stw,
                    p->share,
                    p->index);
            p->safe_point = p->need_stw;

            // runtime_gc 线程会解除 safe 状态，所以这里一直等待即可
            while (processor_safe(p)) {
                TRACEF("[runtime.processor_run] p_index_%d=%d, need_stw=%lu, safe_point=%lu stw loop....", p->share,
                       p->index, p->need_stw,
                       p->safe_point);
                usleep(WAIT_BRIEF_TIME * 1000); // 1ms
            }

            RDEBUGF("[runtime.processor_run] p_index_%d=%d, stw completed, need_stw=%lu, safe_point=%lu", p->share,
                    p->index, p->need_stw,
                    p->safe_point);
        }

        // - exit
        if (processor_get_exit()) {
            RDEBUGF("[runtime.processor_run] p_index_%d=%d, need stop, goto exit", p->share, p->index);
            goto EXIT;
        }

        // - 处理 coroutine (找到 io 可用的 goroutine)
        uint64_t time_start = uv_hrtime();
        while (p->runnable_list.count > 0) {
            coroutine_t *co = rt_linked_fixalloc_pop(&p->runnable_list);
            assert(co);

            RDEBUGF("[runtime.processor_run] will handle coroutine, p_index_%d=%d, co=%p, status=%d", p->share,
                    p->index, co, co->status);

            coroutine_resume(p, co);
            run_count++;

            if (p->need_stw > 0) {
                RDEBUGF("[runtime.processor_run] coroutine resume and p need stw, will goto stw, p_index_%d=%d, co=%p",
                        p->share,
                        p->index, co);
                goto STW_WAIT;
            }

            RDEBUGF("[runtime.processor_run] coroutine resume back, p_index_%d=%d, co=%p", p->share,
                    p->index, co);
        }

        // solo processor exit check
        if (!p->share) {
            assert(p->co_list.count == 1);
            coroutine_t *solo_co = rt_linked_fixalloc_first(&p->co_list)->value;
            assertf(solo_co, "solo_co is null, p_index_%d=%d, co_list linked_node=%p", p->share, p->index,
                    rt_linked_fixalloc_first(&p->co_list));

            if (solo_co->status == CO_STATUS_DEAD) {
                RDEBUGF(
                    "[runtime.processor_run] solo processor co exit, will exit processor run, p_index=%d, co=%p, status=%d",
                    p->index,
                    solo_co, solo_co->status);
                goto EXIT;
            }
        }

        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(p, WAIT_BRIEF_TIME * 5);
    }

EXIT:
    processor_uv_close(p);
    p->thread_id = 0;
    processor_set_status(p, P_STATUS_EXIT);

    RDEBUGF("[runtime.processor_run] exited, p_index_%d=%d", p->share, p->index);
}


void rt_coroutine_to_processor(n_processor_t *p, coroutine_t *co) {
    if (gc_stage == GC_STAGE_MARK) {
        co->gc_black = memory->gc_count;
    }

    // goroutine 默认状态是 runnable
    assert(co->status == CO_STATUS_RUNNABLE);
    rt_linked_fixalloc_push(&p->co_list, co);
    rt_linked_fixalloc_push(&p->runnable_list, co);
}

void rt_coroutine_dispatch(coroutine_t *co) {
    DEBUGF("[runtime.rt_coroutine_dispatch] co=%p, fn=%p, solo=%d, share_processor_count=%d", co, co->fn, co->solo,
           cpu_count);

    // 分配 coroutine 之前需要给 coroutine 确认初始颜色, 如果是新增的 coroutine，默认都是黑色
    if (gc_stage == GC_STAGE_MARK) {
        co->gc_black = memory->gc_count;
    }

    // - 协程独享线程
    if (co->solo) {
        n_processor_t *p = processor_new(solo_processor_count++);
        rt_linked_fixalloc_push(&p->co_list, co);
        rt_linked_fixalloc_push(&p->runnable_list, co);

        mutex_lock(&solo_processor_locker);
        RT_LIST_PUSH_HEAD(solo_processor_list, p);
        mutex_unlock(&solo_processor_locker);

        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assert(false && "pthread_create failed");
        }

        DEBUGF("[runtime.rt_coroutine_dispatch] solo processor create, thread_id=%ld, co=%p, fn=%p",
               (uint64_t) p->thread_id, co, co->fn);
        return;
    }

    // goroutine 默认状态是 runnable
    assert(co->status == CO_STATUS_RUNNABLE);

    // - 遍历 shared_processor_list 找到 co_list->count 最小的 processor 进行调度
    n_processor_t *select_p = NULL;

    // TODO 直接采用 next 方案, 按顺序匹配
    if (co->main) {
        select_p = share_processor_index[0];
    } else {
        PROCESSOR_FOR(share_processor_list) {
            if (!select_p || p->co_list.count < select_p->co_list.count) {
                select_p = p;
            }
        }
    }

    assert(select_p);
    DEBUGF("[runtime.rt_coroutine_dispatch] select_p_index_%d=%d will push co=%p", select_p->share, select_p->index,
           co);

    rt_linked_fixalloc_push(&select_p->co_list, co);
    rt_linked_fixalloc_push(&select_p->runnable_list, co);

    DEBUGF("[runtime.rt_coroutine_dispatch] co=%p to p_index_%d=%d, end", co, select_p->share, select_p->index);
}

/**
 * 各种全局变量初始化都通过该方法
 */
void sched_init() {
    // - 初始化 aco 需要的 tls 变量(不能再线程中 create)
    aco_init();

    // - tls 全局变量初始化
    uv_key_create(&tls_processor_key);
    uv_key_create(&tls_coroutine_key);

    // - 读取当前 cpu 线程数初始化相应数量的 p
    uv_cpu_info_t *info;
    uv_cpu_info(&info, &cpu_count);
    uv_free_cpu_info(info, cpu_count);

    // - 初始化全局标识
    processor_need_exit = false;
    gc_barrier = false;
    mutex_init(&gc_stage_locker, false);
    mutex_init(&solo_processor_locker, false);
    gc_stage = GC_STAGE_OFF;
    solo_processor_count = 0;
    coroutine_count = 0;

    // - libuv 线程锁
    uv_loop_init(&uv_global_loop);
    mutex_init(&uv_thread_locker, false);

    // - 初始化 global linkco
    mutex_init(&global_linkco_locker, false);
    global_linkco_cache = NULL;

    rt_linked_fixalloc_init(&global_gc_worklist);

    // - 初始化 processor 和 coroutine 分配器
    fixalloc_init(&coroutine_alloc, sizeof(coroutine_t));
    fixalloc_init(&processor_alloc, sizeof(n_processor_t));
    mutex_init(&cp_alloc_locker, false);

    DEBUGF("[runtime.sched_init] cpu_count=%d", cpu_count);

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    // 初始化 share_processor_index 数组为 0
    share_processor_list = NULL;
    solo_processor_list = NULL;

    for (int i = 0; i < cpu_count; ++i) {
        n_processor_t *p = processor_new(i);
        // 仅 share processor 需要 gc worklist
        p->share = true;
        rt_linked_fixalloc_init(&p->gc_worklist);
        p->gc_work_finished = memory->gc_count;
        share_processor_index[p->index] = p;

        RT_LIST_PUSH_HEAD(share_processor_list, p);

        // 创建一个新的线程用来处理
        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assert(false && "pthread_create failed %s");
        }

        DEBUGF("[runtime.sched_init] processor create, index=%d, thread_id=%ld", i, (uint64_t) p->thread_id);
    }
}

n_processor_t *processor_get() {
    n_processor_t *p = uv_key_get(&tls_processor_key);
    return p;
}

coroutine_t *coroutine_get() {
    return uv_key_get(&tls_coroutine_key);
}

void rt_default_co_error(char *msg, bool panic) {
    DEBUGF("[runtime.rt_default_co_error] msg=%s", msg);
    coroutine_t *co = coroutine_get();
    n_error_t *error = n_error_new(string_new(msg, strlen(msg)), panic);
    co->error = error;
}

void rt_co_error(coroutine_t *co, char *msg, bool panic) {
    DEBUGF("[runtime.rt_co_error] msg=%s", msg);
    n_error_t *error = n_error_new(string_new(msg, strlen(msg)), panic);
    co->error = error;
}

void coroutine_dump_error(coroutine_t *co, n_error_t *error) {
    DEBUGF("[runtime.coroutine_dump_error] co=%p, errort base=%p", co, error);

    n_string_t *msg = error->msg;
    DEBUGF("[runtime.coroutine_dump_error] memory_string len: %lu, base: %p", msg->length, msg->data);
    assert(error->traces->length > 0);

    n_trace_t first_trace = {};
    rt_vec_access(error->traces, 0, &first_trace);
    char *dump_msg;
    if (co->main) {
        dump_msg = dsprintf("coroutine 'main' uncaught error: '%s' at %s:%d:%d\n", (char *) error->msg->data,
                            (char *) first_trace.path->data, first_trace.line,
                            first_trace.column);
    } else {
        dump_msg = dsprintf("coroutine %ld uncaught error: '%s' at %s:%d:%d\n", co->id, (char *) error->msg->data,
                            (char *) first_trace.path->data, first_trace.line,
                            first_trace.column);
    }

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (error->traces->length > 1) {
        char *temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < error->traces->length; ++i) {
            n_trace_t trace = {};
            rt_vec_access(error->traces, i, &trace);
            temp = dsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char *) trace.ident->data, (char *) trace.path->data,
                            trace.line, trace.column);
            VOID write(STDOUT_FILENO, temp, strlen(temp));
        }
    }
}

void mark_ptr_black(void *value) {
    addr_t addr = (addr_t) value;
    // get mspan by ptr
    mspan_t *span = span_of(addr);
    assert(span);
    mutex_lock(&span->gcmark_locker);
    // get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;
    bitmap_set(span->gcmark_bits, obj_index);
    DEBUGF("[runtime.mark_ptr_black] addr=%p, span=%p, spc=%d, span_base=%p, obj_index=%lu marked", value, span,
           span->spanclass,
           (void *) span->base, obj_index);
    mutex_unlock(&span->gcmark_locker);
}

/**
 * TODO target use debug, can delete
 * yield 的入口也是这里
 * @param target
 */
void pre_tplcall_hook() {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();

    // 这里需要抢占到锁再进行更新，否则和 wait_sysmon 存在冲突。
    // 如果 wait_sysmon 已经获取了锁，则会阻塞在此处等待 wait_symon 进行抢占, 避免再次进入 is_tpl
    processor_set_status(p, P_STATUS_TPLCALL);

    CO_SCAN_REQUIRE(co)
}

void post_tplcall_hook() {
    n_processor_t *p = processor_get();
    TRACEF("[runtime.post_tplcall_hook] p=%p, p_index_%d=%d will set processor_status, running",
           processor_get(),
           p->share, p->index);
    processor_set_status(p, P_STATUS_RUNNING);
}

void post_rtcall_hook(char *target) {
    n_processor_t *p = processor_get();
    DEBUGF("[runtime.post_rtcall_hook] p=%p, target=%s, p_index_%d=%d will set processor_status, running",
           processor_get(), target,
           p->share, p->index);
    processor_set_status(p, P_STATUS_RUNNING);
}

/**
 * 一定是开启了 gc_barrier 才会进行 scan co_list, 如果 rti_gc_malloc 在 gc_barrier 之前调用
 * 且 co 没有赋值给 co_list, 则 new co 不能正确的被 mark, 会被错误清理, result 同理
 * @param fn
 * @param flag
 * @param arg
 * @param result_size
 * @return
 */
coroutine_t *rt_coroutine_new(void *fn, int64_t flag, n_future_t *fu, void *arg) {
    mutex_lock(&cp_alloc_locker);
    coroutine_t *co = fixalloc_alloc(&coroutine_alloc);
    co->id = coroutine_count++;
    mutex_unlock(&cp_alloc_locker);

    if (in_heap((addr_t) fn)) {
        rt_shade_obj_with_barrier(fn);
    }

    if (in_heap((addr_t) arg)) {
        rt_shade_obj_with_barrier(arg);
    }

    // 通过 shade 避免 fu 在本轮中被 gc
    rt_shade_obj_with_barrier(fu);

    co->future = fu;
    co->fn = fn;
    co->solo = FLAG(CO_FLAG_SOLO) & flag;
    co->main = FLAG(CO_FLAG_MAIN) & flag;
    co->arg = arg;
    co->data = NULL;
    co->gc_black = 0;
    co->yield_lock = NULL;
    co->ticket = false;
    co->status = CO_STATUS_RUNNABLE;
    co->p = NULL;
    co->next = NULL;
    co->aco.inited = 0; // 标记为为初始化
    co->scan_ret_addr = 0;
    co->scan_offset = 0;

    DEBUGF("[rt_coroutine_new] co=%p, fn=%p new success, gc_barrier=%d", co, co->fn, gc_barrier_get());
    return co;
}

// 如果被抢占会导致全局队列卡死，所以 linked 和 processor 绑定好了, 这就关系到 fixalloc_t 的释放问题
// 除非在这期间不进行抢占，
n_processor_t *processor_new(int index) {
    mutex_lock(&cp_alloc_locker);
    n_processor_t *p = fixalloc_alloc(&processor_alloc);
    mutex_unlock(&cp_alloc_locker);

    // uv_loop_init(&p->uv_loop);
    mutex_init(&p->gc_stw_locker, false);
    p->need_stw = 0;
    p->safe_point = 0;

    mutex_init(&p->thread_locker, false);
    p->status = P_STATUS_INIT;
    p->sig.sa_flags = 0;
    p->thread_id = 0;
    p->coroutine = NULL;
    p->co_started_at = 0;
    p->mcache.flush_gen = 0; // 线程维度缓存，避免内存分配锁
    rt_linked_fixalloc_init(&p->co_list);
    rt_linked_fixalloc_init(&p->runnable_list);
    p->index = index;
    p->next = NULL;

    p->linkco_count = 0;

    return p;
}

/**
 * 当 main processor 退出时，会收到该值
 * @return
 */
bool processor_get_exit() {
    return processor_need_exit;
}

void processor_set_exit() {
    processor_need_exit = true;
}

void coroutine_free(coroutine_t *co) {
    mutex_lock(&cp_alloc_locker);

    aco_destroy(&co->aco);
    co->id = 0;
    co->fn = NULL;
    co->aco.save_stack.ptr = 0;
    co->status = CO_STATUS_DEAD;

    fixalloc_free(&coroutine_alloc, co);
    mutex_unlock(&cp_alloc_locker);
}

/**
 * processor_free 不受 stw 影响，所以获取一下 memory locker 避免 uncache 冲突
 * @param p
 */
void processor_free(n_processor_t *p) {
    DEBUGF("[wait_sysmon.processor_free] start p=%p, p_index_%d=%d, loop=%p, share_stack=%p|%p", p, p->share, p->index,
           &p->uv_loop,
           &p->share_stack, p->share_stack.ptr);

    coroutine_t *co = rt_linked_fixalloc_pop(&p->co_list);
    coroutine_free(co);

    aco_share_stack_destroy(&p->share_stack);
    rt_linked_fixalloc_free(&p->co_list);
    rt_linked_fixalloc_free(&p->runnable_list);
    aco_destroy(&p->main_aco);

    // 归还 mcache span
    for (int j = 0; j < SPANCLASS_COUNT; ++j) {
        mspan_t *span = p->mcache.alloc[j];
        if (!span) {
            continue;
        }

        p->mcache.alloc[j] = NULL; // uncache
        mcentral_t *mcentral = &memory->mheap->centrals[span->spanclass];
        uncache_span(mcentral, span);
        DEBUGF("[wait_sysmon.processor_free] uncache span=%p, span_base=%p, spc=%d, alloc_count=%lu", span,
               (void *) span->base,
               span->spanclass, span->alloc_count);
    }

    RDEBUGF("[wait_sysmon.processor_free] will free uv_loop p_index_%d=%d, loop=%p", p->share, p->index, &p->uv_loop);
    int share = p->share;
    int index = p->index;

    RDEBUGF("[wait_sysmon.processor_free] will free processor p_index_%d=%d", share, index);
    mutex_lock(&cp_alloc_locker);
    fixalloc_free(&processor_alloc, p);
    mutex_unlock(&cp_alloc_locker);
    DEBUGF("[wait_sysmon.processor_free] succ free p_index_%d=%d", share, index);
}

/**
 * 是否所有的 processor 都到达了安全点
 * @return
 */
bool processor_all_safe() {
    PROCESSOR_FOR(share_processor_list) {
        if (p->status == P_STATUS_EXIT) {
            continue;
        }

        if (processor_safe(p)) {
            continue;
        }

        RDEBUGF(
            "[runtime_gc.processor_all_safe] share processor p_index_%d=%d, thread_id=%lu not safe, need_stw=%lu, safe_point=%lu",
            p->share, p->index, (uint64_t) p->thread_id, p->need_stw, p->safe_point);
        return false;
    }

    // safe_point 或者获取到 gc_stw_locker
    mutex_lock(&solo_processor_locker);
    PROCESSOR_FOR(solo_processor_list) {
        if (p->status == P_STATUS_EXIT) {
            continue;
        }

        if (processor_safe(p)) {
            continue;
        }

        // 获取了锁不代表进入了安全状态,此时可能还在 user code 中, 必须要以 safe_point 为准
        // if (mutex_trylock(&p->gc_stw_locker) != 0) {
        //     continue;
        // }
        RDEBUGF("[runtime_gc.processor_all_safe] solo processor p_index_%d=%d, thread_id=%lu not safe", p->share,
                p->index,
                (uint64_t) p->thread_id);
        mutex_unlock(&solo_processor_locker);
        return false;
    }
    mutex_unlock(&solo_processor_locker);

    return true;
}

void processor_all_wait_safe() {
    RDEBUGF("[processor_all_wait_safe] start");
    while (!processor_all_safe()) {
        usleep(WAIT_BRIEF_TIME * 1000);
    }

    RDEBUGF("[processor_all_wait_safe] end");
}

void processor_stw_unlock() {
    mutex_lock(&solo_processor_locker);
    PROCESSOR_FOR(solo_processor_list) {
        mutex_unlock(&p->gc_stw_locker);
        RDEBUGF("[runtime.processor_all_safe_or_lock] solo processor p_index_%d=%d gc_stw_locker unlock", p->share,
                p->index);
    }
    mutex_unlock(&solo_processor_locker);
}

/**
 * 遍历所有的 share processor 和 solo processor 判断 gc 是否全部完成
 * @return
 */
static bool all_gc_work_finished() {
    PROCESSOR_FOR(share_processor_list) {
        if (p->gc_work_finished < memory->gc_count) {
            return false;
        }
    }

    return true;
}

/**
 * 需要等待独享和共享协程的 gc_work 全部完成 mark,
 */
void wait_all_gc_work_finished() {
    RDEBUGF("[runtime_gc.wait_all_gc_work_finished] start");

    while (all_gc_work_finished() == false) {
        usleep(WAIT_SHORT_TIME * 1000);
    }

    DEBUGF("[runtime_gc.wait_all_gc_work_finished] all processor gc work finish");
}

void *global_gc_worklist_pop() {
    void *value = rt_linked_fixalloc_pop(&global_gc_worklist);
    return value;
}

/**
 * 1. 只要没有进行新的 resume, 那及时 yield 了，当前 aco 信息就还是存储在 share stack 中
 * 2. 可以从 rsp 寄存器中读取栈顶
 * @param aco
 * @param stack
 */
void co_migrate(aco_t *aco, aco_share_stack_t *new_st) {
    // 起始迁移点(最小值)
    void *sp = aco->reg[ACO_REG_IDX_SP];
    aco_share_stack_t *old_st = aco->share_stack;

    // 需要迁移的 size
    addr_t size = (addr_t) old_st->align_retptr - (addr_t) sp;

    // 原则上应该从 old_share_stack->align_retptr(最大值)
    memmove(new_st->align_retptr - size, sp, size);

    // 更新栈里面 prev bp 值
    addr_t bp_ptr = (addr_t) aco->reg[ACO_REG_IDX_BP];

    while (true) {
        addr_t prev_bp_value = fetch_addr_value((addr_t) bp_ptr); // 可能压根没有报错 bp 的值，所以必须有一个中断条件

        // 边界情况处理
        if (prev_bp_value <= bp_ptr) {
            break;
        }

        if (prev_bp_value < (addr_t) sp) {
            break;
        }

        if (prev_bp_value > (addr_t) old_st->align_retptr) {
            break;
        }

        addr_t prev_bp_offset = (addr_t) old_st->align_retptr - (addr_t) bp_ptr;

        addr_t new_prev_bp_value = (addr_t) new_st->align_retptr - ((addr_t) old_st->align_retptr - prev_bp_value);

        // 更新相关位置的值
        memmove((void *) ((addr_t) new_st->align_retptr - prev_bp_offset), &new_prev_bp_value, POINTER_SIZE);

        // 更新 rbp_ptr 的指向
        bp_ptr = prev_bp_value;
    }

    // 更新 bp/sp 寄存器
    aco->reg[ACO_REG_IDX_BP] = (void *) ((addr_t) new_st->align_retptr -
                                         ((addr_t) old_st->align_retptr - (addr_t) aco->reg[ACO_REG_IDX_BP]));
    aco->reg[ACO_REG_IDX_SP] = (void *) ((addr_t) new_st->align_retptr -
                                         ((addr_t) old_st->align_retptr - (addr_t) aco->reg[ACO_REG_IDX_SP]));

    // 更新 co share_stack 指向
    aco->share_stack = new_st;
    new_st->owner = aco;
}

void processor_set_status(n_processor_t *p, p_status_t status) {
    assert(p);
    assert(p->status != status);

    // rtcall 是不稳定状态，可以随时切换到任意状态
    if (p->status == P_STATUS_RTCALL) {
        p->status = status;
        return;
    }

    // 对于 share processor 来说， tplcall 和 rtcall 是不稳定的，需要切换走的，所以不需要被 thread_locker 锁住
    if (p->share && p->status == P_STATUS_TPLCALL) {
        p->status = status;
        return;
    }

    // solo 状态下 tplcall 可以切换到 dispatch, 但是不能进入到 running
    if (!p->share && p->status == P_STATUS_TPLCALL && status == P_STATUS_DISPATCH) {
        p->status = status;
        return;
    }

    // 必须先获取 thread_locker 再获取 gc_stw_locker
    mutex_lock(&p->thread_locker);

    // 特殊处理 2， solo 切换成 running 时需要获取 gc_stw_locker, 如果 gc_stw_locker 阻塞说明当前正在 stw
    // 必须获取到 stw locker 才能切换到 running 状态(runnable -> running/ tpl_call -> running)
    if (!p->share && status == P_STATUS_RUNNING) {
        mutex_lock(&p->gc_stw_locker);
    }

    p->status = status;

    if (!p->share && status == P_STATUS_RUNNING) {
        mutex_unlock(&p->gc_stw_locker);
    }

    mutex_unlock(&p->thread_locker);

    if (status == P_STATUS_RUNNING) {
        p->co_started_at = uv_hrtime();
    }
}

void rt_coroutine_return(void *result_ptr) {
    coroutine_t *co = coroutine_get();
    DEBUGF("[runtime.rt_coroutine_return] co=%p, fu=%p, size=%ld", co, co->future, co->future->size);

    assert(co->future);
    assert(co->future->size > 0);

    memmove(co->future->result, result_ptr, co->future->size);
    DEBUGF("[runtime.rt_coroutine_return] co=%p, result=%p, int_result=%ld, result_size=%ld", co, co->future->result,
           *(int64_t *) co->future->result, co->future->size);
}

void *rt_coroutine_error(coroutine_t *co) {
    DEBUGF("[runtime.rt_coroutine_error] co=%p, error=%p, msg=%s",
           co, co->error, co->error ? (char *) co->error->msg->data : "-");
    return co->error;
}

void rt_coroutine_await(coroutine_t *target_co) {
    mutex_lock(&target_co->dead_locker);
    coroutine_t *src_co = coroutine_get();
    if (target_co->status == CO_STATUS_DEAD) {
        mutex_unlock(&target_co->dead_locker);
        return;
    }
    target_co->await_co = src_co;
    co_set_status(src_co->p, src_co, CO_STATUS_WAITING);
    mutex_unlock(&target_co->dead_locker);

    // 一旦 unlock 就可能会发生 runnable push 以及 status set, 但是不影响当前 yield
    _co_yield(src_co->p, src_co);

    // waiting -> syscall
    co_set_status(src_co->p, src_co, CO_STATUS_TPLCALL);
}

void rt_processor_wake(n_processor_t *p) {
    n_processor_t *current_p = processor_get();
    if (current_p == p) {
        return;
    }

    if (p->runnable_list.count != 1) {
        return;
    }

    uv_stop(&p->uv_loop);
}
