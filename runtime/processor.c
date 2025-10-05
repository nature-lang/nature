#include "processor.h"

#include <ucontext.h>

#include "nutils/errort.h"
#include "nutils/rt_signal.h"
#include "runtime.h"
#include "runtime/nutils/http.h"

int cpu_count;

n_processor_t *processor_index[1024] = {0};
n_processor_t *processor_list; // 共享协程列表的数量一般就等于线程数量

//n_processor_t *solo_processor_list; // 独享协程列表其实就是多线程
//mutex_t solo_processor_locker; // 删除 solo processor 需要先获取该锁

int64_t coroutine_count; // coroutine 累计数量
bool main_coroutine_exited = false;

rt_linked_fixalloc_t global_gc_worklist;

uv_key_t tls_processor_key = 0;
uv_key_t tls_coroutine_key = 0;

_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint1 = false;
_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint2 = false;
_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint3 = false;

_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint = false;

_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint4 = false;
_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint5 = false;
_Thread_local __attribute__((tls_model("local-exec"))) int64_t tls_yield_safepoint6 = false;

uint64_t assist_preempt_yield_ret_addr = 0;


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
    coroutine_t *co = coroutine_get();
    assert(co);

    // 首次 preempt yield, 记录 ret_addr, 用于后续的 scan_stack 识别
    if (assist_preempt_yield_ret_addr == 0) {
        assist_preempt_yield_ret_addr = CALLER_RET_ADDR(co);
    }

    n_processor_t *p = processor_get();
    assert(p);

    DEBUGF(
            "[runtime.co_preempt_yield] p_index=%d(%d), co=%p, p_status=%d,  will yield, co_start=%ld, assist_preempt_yield_ret_addr= %p",
            p->index, p->status, co, co->status, p->co_started_at / 1000 / 1000, (void *) assist_preempt_yield_ret_addr);

    p->status = P_STATUS_PREEMPT; // 抢占返回标志

    // 不需要等待，直接设置为 runnable 状态
    co->status = CO_STATUS_RUNNABLE;
    rt_linked_fixalloc_push(&p->runnable_list, co);

    *p->tls_yield_safepoint_ptr = false; // 清空状态
    DEBUGF("[runtime.co_preempt_yield] co=%p push and update status success", co);

    _co_yield(p, co);

    // 接下来将直接 return 到用户态，不经过 post_tpl_hook, 所以直接更新为允许抢占
    // yield 切换回了用户态，此时允许抢占，所以不能再使用 RDEBUG, 而是 DEBUG
    DEBUGF("[runtime.co_preempt_yield] yield resume end, will set running, p_index=%d, p_status=%d co=%p, p->co=%p, share_stack.base=%p, share_stack.top(sp)=%p, co_start_at=%ld",
           p->index,
           p->status, co, p->coroutine, p->share_stack.align_retptr, co->aco.reg[ACO_REG_IDX_SP],
           p->co_started_at / 1000 / 1000);

    //    co_set_status(p, co, CO_STATUS_RUNNING);
    //    processor_set_status(p, P_STATUS_RUNNING);
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
#else // darwin
#define CTX_RSP ctx->uc_mcontext->__ss.__rsp
#define CTX_RIP ctx->uc_mcontext->__ss.__rip
#endif
    // int REG_RBP = 10;
    int REG_RSP = 15;
    int REG_RIP = 16;
    uint64_t *rsp = (uint64_t *) CTX_RSP;
    uint64_t rip = CTX_RIP;
    // rip 已经存到了 rsp 里面,只要能定位到 bp_offset 就行了。
    fndef_t *fn = find_fn(rip, p);
    if (fn) {
        // 基于当前 rsp scan
        uint64_t sp_addr = (uint64_t) rsp;
        //        co->scan_ret_addr = rip;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - sp_addr;
    } else {
        // c 语言段被抢占，采取保守的扫描策略(使用 ret_addr = 0 来识别)
        //        co->scan_ret_addr = 0;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - (uint64_t) rsp;
    }

    // 由于被抢占的函数可以会在没有 sub 保留 rsp 的情况下使用 rsp-0x10 这样的空间地址
    // 所以需要为 rsp 预留足够的栈空间给被抢占的函数, 避免后续的操作污染被抢占的函数
    rsp -= 128; // 一个指针是 8byte, 所以这里是 128 * 8 = 1024 个字节
    // push rip
    rsp--; // 栈中预留返回地址
    *rsp = rip;
    RDEBUGF("[runtime.thread_handle_sig] rip=%p save to %p, co=%p, scan_ret_addr=%p, scan_offset=%lu, fn=%p",
            (void *) rip, rsp, co,
            (void *) co->scan_ret_addr, co->scan_offset, fn);

    CTX_RSP = (int64_t) rsp;
    CTX_RIP = (int64_t) assist_preempt_yield;

#elif defined(__ARM64)
#ifdef __LINUX
#define CTX_SP ctx->uc_mcontext.sp
#define CTX_PC ctx->uc_mcontext.pc
#define CTX_LR ctx->uc_mcontext.regs[30] // x30 是链接寄存器(LR)
#else
#define CTX_SP ctx->uc_mcontext->__ss.__sp
#define CTX_PC ctx->uc_mcontext->__ss.__pc
#define CTX_LR ctx->uc_mcontext->__ss.__lr
#endif

    uint64_t *sp = (uint64_t *) CTX_SP;
    uint64_t pc = CTX_PC;
    uint64_t lr = CTX_LR; // 保存返回地址的链接寄存器

    // 查找当前执行的函数(协作式调度，必然能够被找到才行)
    fndef_t *fn = find_fn(pc, p);
    if (fn) {
        // 基于当前 sp 计算扫描偏移
        uint64_t sp_addr = (uint64_t) sp;
        //        co->scan_ret_addr = pc;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - sp_addr;
    } else {
        // c 语言段被抢占,采用保守扫描策略
        //        co->scan_ret_addr = 0;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - (uint64_t) sp;
    }

    // 为被抢占的函数预留栈空间
    sp -= 128; // 1024 字节的安全区域

    // ARM64 中保存返回地址和链接寄存器
    sp -= 2; // 预留两个位置,一个给 pc,一个给 lr
    sp[0] = pc;
    sp[1] = lr;

    RDEBUGF("[runtime.thread_handle_sig] pc=%p lr=%p save to %p, co=%p, fn=%p",
            (void *) pc, (void *) lr, sp, co, fn);

    // 更新上下文
    CTX_SP = (uint64_t) sp;
    CTX_PC = (uint64_t) assist_preempt_yield;
    CTX_LR = (uint64_t) assist_preempt_yield; // 确保返回地址也指向抢占处理函数

#elif defined(__RISCV64)
    // RISCV64 架构支持
    // 在 RISCV64 Linux 下，寄存器通过 __gregs 数组访问
#define CTX_SP ctx->uc_mcontext.__gregs[2] // x2 是栈指针 (sp)
#define CTX_PC ctx->uc_mcontext.__gregs[0] // pc 寄存器
#define CTX_RA ctx->uc_mcontext.__gregs[1] // x1 是返回地址寄存器 (ra)

    uint64_t *sp = (uint64_t *) CTX_SP;
    uint64_t pc = CTX_PC;
    uint64_t ra = CTX_RA; // 保存返回地址寄存器

    // 查找当前执行的函数
    fndef_t *fn = find_fn(pc, p);
    if (fn) {
        // 基于当前 sp 计算扫描偏移
        uint64_t sp_addr = (uint64_t) sp;
        //        co->scan_ret_addr = pc;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - sp_addr;
    } else {
        // c 语言段被抢占,采用保守扫描策略
        //        co->scan_ret_addr = 0;
        //        co->scan_offset = (uint64_t) co->p->share_stack.align_retptr - (uint64_t) sp;
    }

    // 为被抢占的函数预留栈空间
    sp -= 128; // 1024 字节的安全区域

    // RISCV64 中保存程序计数器和返回地址
    sp -= 2; // 预留两个位置,一个给 pc,一个给 ra
    sp[0] = pc;
    sp[1] = ra;

    RDEBUGF("[runtime.thread_handle_sig] pc=%p ra=%p save to %p, co=%p, fn=%p",
            (void *) pc, (void *) ra, sp, co, fn);

    // 更新上下文
    CTX_SP = (uint64_t) sp;
    CTX_PC = (uint64_t) assist_preempt_yield;
    CTX_RA = (uint64_t) assist_preempt_yield; // 确保返回地址也指向抢占处理函数

#else
#error "platform no support yet"
#endif
}

static void processor_uv_close(n_processor_t *p) {
    // 关闭 uv_loop
    RDEBUGF("[runtime.processor_uv_close] will close loop=%p, loop_req_count=%u, p_index=%d", &p->uv_loop,
            p->uv_loop.active_reqs.count, p->index);

    // TODO The process has been shut down and doesn't need to be concerned about overflow?
    //    uv_close((uv_handle_t *) &p->timer, NULL); // io_run 等待 close 完成！

    //    uv_run(&p->uv_loop, UV_RUN_DEFAULT); // 等待上面注册的 uv_close 完成

    //    int result = uv_loop_close(&p->uv_loop);
    //    if (result) {
    //        DEBUGF("[runtime.processor_uv_close] uv loop close failed, code=%d, msg=%s, p_index=%d", result,
    //               uv_strerror(result), p->index);
    //
    //        assert(false && "uv loop close failed");
    //    }

    RDEBUGF("[runtime.processor_uv_close] processor uv close success p_index=%d", p->index);
}

NO_OPTIMIZE static void coroutine_wrapper() {
    coroutine_t *co = aco_get_arg();
    assert(co);
    n_processor_t *p = processor_get();
    assert(p);

    DEBUGF("[runtime.coroutine_wrapper] p_index=%d, p_status=%d co=%p, fn=%p main=%d, rt_co=%d", p->index, p->status,
           co, co->fn, co->main, co->flag & FLAG(CO_FLAG_RTFN));

    co_set_status(p, co, CO_STATUS_RUNNING);
    processor_set_status(p, P_STATUS_RUNNING);

    if (in_heap((addr_t) co->fn)) {
        assertf(span_of((addr_t) co->fn), "co=%p fn=%p not found in heap, maybe swept", co, co->fn);
    }

    assert((addr_t) co->fn > 0);

    // 调用到用户函数
    if (co->flag & FLAG(CO_FLAG_DIRECT)) {
        ((void_fn_t) co->fn)();
    } else {
        n_fn_t *runtime_fn = co->fn;
        ((env_fn_t) runtime_fn->fn_addr)(runtime_fn->envs);
    }


    DEBUGF(
            "[runtime.coroutine_wrapper] user fn completed, p_index=%d co=%p, main=%d, rt_fn=%d,err=%p",
            p->index, co,
            co->main, co->flag & FLAG(CO_FLAG_RTFN), co->error);


    // coroutine 即将退出，避免被 gc 清理，所以将 error 保存在 co->future 中?
    if (co->error && co->future) {
        co->future->error = union_casting(throwable_rtype.hash, &co->error); // 将 co error 赋值给 co->future 避免被 gc
    }

    // co->await_co 可能是随时写入的，所以需要 dead_locker 保证同步
    mutex_lock(&co->dead_locker);
    if (co->await_co) {
        coroutine_t *await_co = co->await_co;

        co_set_status(p, await_co, CO_STATUS_RUNNABLE);
        rt_linked_fixalloc_push(&await_co->p->runnable_list, await_co);
    } else {
        if (co->has_error) {
            coroutine_dump_error(co);
            exit(EXIT_FAILURE);
        }
    }

    co_set_status(p, co, CO_STATUS_DEAD);
    if (co->main) {
        main_coroutine_exited = true;
        DEBUGF("[runtime.coroutine_wrapper] co=%p main exited", co);
    }

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

void processor_all_need_stop() {
    uint64_t stw_time = uv_hrtime();
    PROCESSOR_FOR(processor_list) {
        p->need_stw = stw_time;
    }

    //    mutex_lock(&solo_processor_locker);
    //    PROCESSOR_FOR(solo_processor_list) {
    //        p->need_stw = stw_time;
    //    }
    //    mutex_unlock(&solo_processor_locker);
}

void processor_all_start() {
    PROCESSOR_FOR(processor_list) {
        p->need_stw = 0;
        p->in_stw = 0;
        RDEBUGF("[runtime_gc.processor_all_start] p_index=%d, thread_id=%lu set safe_point=false",
                p->index,
                (uint64_t) p->thread_id);
    }

    //    mutex_lock(&solo_processor_locker);
    //    PROCESSOR_FOR(solo_processor_list) {
    //        p->need_stw = 0;
    //        p->in_stw = 0;
    //        //        mutex_unlock(&p->gc_solo_stw_locker);
    //
    //        TRACEF(
    //                "[runtime_gc.processor_all_start] p_index_%d=%d, thread_id=%lu set safe_point=false and unlock gc_stw_locker",
    //                p->share, p->index, (uint64_t) p->thread_id);
    //    }
    //    mutex_unlock(&solo_processor_locker);

    DEBUGF("[runtime_gc.processor_all_start] all processor stw completed");
}

void on_timer_stop_cb(uv_timer_t *timer) {
    n_processor_t *p = timer->data;
    uv_timer_stop(timer);
    uv_stop(timer->loop);
}

/**
 * timeout_ms 推荐 5ms ~ 10ms
 */
int io_run(n_processor_t *p, uint64_t timeout_ms) {
    // 初始化计时器 (这里不会发生栈切换，所以可以在栈上直接分配)
    p->timer.data = p;

    // 设置计时器超时回调，这将在超时后停止事件循环
    uv_timer_start(&p->timer, on_timer_stop_cb, timeout_ms, 0); // 只触发一次

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
    RDEBUGF("[coroutine_resume] aco_resume will start,co=%p,main=%d,aco=%p, sstack=%p(%zu), p_index=%d(%d)", co,
            co->main, &co->aco,
            co->aco.save_stack.ptr, co->aco.save_stack.sz, p->index, p->status);

    // 获取锁才能切换协程并更新状态
    mutex_lock(&p->thread_locker);

    p->coroutine = co;

    // - 再 tls 中记录正在运行的协程
    uv_key_set(&tls_coroutine_key, co);

    co->p = p; // 运行前进行绑定，让 coroutine 在运行中可以准确的找到 processor

    p->status = P_STATUS_RUNNABLE; // yield 返回之后才会设置 co_started_at

    mutex_unlock(&p->thread_locker);

    // 核心 resume
    aco_resume(&co->aco);

    TRACEF(
            "[coroutine_resume] resume back, p_index=%d(%d), co=%p, status=%d, rt_fn=%d",
            p->index, p->status, co, co->status, co->flag & FLAG(CO_FLAG_RTFN));

    // running -> dispatch
    assert(co->status != CO_STATUS_RUNNING);

    if (co->wait_unlock_fn) {
        co->wait_unlock_fn(co, co->wait_lock);
        co->wait_unlock_fn = NULL;
        co->wait_lock = NULL;
    }

    p->co_started_at = 0;

    // running -> dispatch
    // preempt -> dispatch
    processor_set_status(p, P_STATUS_DISPATCH);

    uint64_t time = (uv_hrtime() - p->co_started_at) / 1000 / 1000;
    RDEBUGF("[coroutine_resume] resume back, co=%p, aco=%p, run_time=%lu ms, rt_co=%d", co, &co->aco, time,
            co->flag & FLAG(CO_FLAG_RTFN));
}

// handle by thread
static void processor_run(void *raw) {
    n_processor_t *p = raw;
    DEBUGF("[runtime.processor_run] start, p_index=%d, addr=%p, loop=%p, yield_safepoint_ptr=%p(%ld)", p->index, p,
           &p->uv_loop, &tls_yield_safepoint, tls_yield_safepoint);

    DEBUGF("[runtime.processor_run] tls1 %p, tls2 %p, tls3 %p tls4 %p, tls5 %p, tls6 %p", &tls_yield_safepoint1, &tls_yield_safepoint2, &tls_yield_safepoint3, &tls_yield_safepoint4, &tls_yield_safepoint5, &tls_yield_safepoint6);

    processor_set_status(p, P_STATUS_DISPATCH);

    // 初始化 aco 和 main_co
    aco_thread_init(NULL);
    aco_create_init(&p->main_aco, NULL, NULL, 0, NULL, NULL);

    aco_share_stack_init(&p->share_stack, 0);

    // - 初始化 libuv
    uv_loop_init(&p->uv_loop);
    uv_timer_init(&p->uv_loop, &p->timer);

    p->tls_yield_safepoint_ptr = &tls_yield_safepoint;

    // 注册线程信号监听, 用于抢占式调度
    // 将 p 存储在线程维度全局遍历中，方便直接在 coroutine 运行中读取相关的 processor
    uv_key_set(&tls_processor_key, p);

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        TRACEF("[runtime.processor_run] handle, p_index=%d, main_exited=%d, running_count=%ld", p->index, main_coroutine_exited,
               p->runnable_list.count);

        // - stw
        if (p->need_stw > 0) {
        STW_WAIT:
            DEBUGF("[runtime.processor_run] need stw, set safe_point=need_stw(%lu), p_index=%d, main_exited=%d", p->need_stw, p->index, main_coroutine_exited);
            p->in_stw = p->need_stw;

            // runtime_gc 线程会解除 safe 状态，所以这里一直等待即可
            while (processor_need_stw(p)) {
                TRACEF("[runtime.processor_run] p_index=%d, need_stw=%lu, safe_point=%lu stw loop....", p->index,
                       p->need_stw, p->in_stw);
                usleep(WAIT_BRIEF_TIME * 1000); // 1ms
            }

            DEBUGF("[runtime.processor_run] p_index=%d, stw completed, need_stw=%lu, safe_point=%lu, main_exited=%d",
                   p->index, p->need_stw,
                   p->in_stw, main_coroutine_exited);
        }

        // - exit
        if (main_coroutine_exited) {
            DEBUGF("[runtime.processor_run] main coroutine dead, goto exit");
            goto EXIT;
        }

        // - 处理 coroutine (找到 io 可用的 goroutine)
        int64_t handle_limit = 100;
        while (p->runnable_list.count > 0 && handle_limit > 0) {
            coroutine_t *co = rt_linked_fixalloc_pop(&p->runnable_list);
            assert(co);

            RDEBUGF("[runtime.processor_run] will handle coroutine, p_index=%d, co=%p, status=%d", p->index, co,
                    co->status);

            coroutine_resume(p, co);
            handle_limit--;

            if (p->need_stw > 0) {
                RDEBUGF("[runtime.processor_run] coroutine resume and p need stw, will goto stw, p_index=%d, co=%p",
                        p->index, co);
                goto STW_WAIT;
            }

            if (main_coroutine_exited) {
                goto EXIT;
            }

            RDEBUGF("[runtime.processor_run] coroutine resume back, p_index=%d, co=%p",
                    p->index, co);
        }

        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(p, WAIT_BRIEF_TIME * 1);
    }

EXIT:
    processor_uv_close(p);
    p->thread_id = 0;
    processor_set_status(p, P_STATUS_EXIT);

    DEBUGF("[runtime.processor_run] exited, p_index=%d", p->index);
}

void rt_coroutine_dispatch(coroutine_t *co) {
    DEBUGF("[runtime.rt_coroutine_dispatch] co=%p, fn=%p, share_processor_count=%d", co, co->fn, cpu_count);

    // 分配 coroutine 之前需要给 coroutine 确认初始颜色, 如果是新增的 coroutine，默认都是黑色
    if (gc_stage == GC_STAGE_MARK) {
        co->gc_black = memory->gc_count;
    }

    // goroutine 默认状态是 runnable
    assert(co->status == CO_STATUS_RUNNABLE);

    // - 遍历 shared_processor_list 找到 co_list->count 最小的 processor 进行调度
    n_processor_t *select_p = NULL;

    if (co->main) {
        select_p = processor_index[0];
    } else if (co->flag & FLAG(CO_FLAG_SAME)) {
        select_p = processor_get();
    } else {
        PROCESSOR_FOR(processor_list) {
            if (!select_p || p->co_list.count < select_p->co_list.count) {
                select_p = p;
            }
        }
    }

    assert(select_p);
    DEBUGF("[runtime.rt_coroutine_dispatch] select_p_index=%d will push co=%p", select_p->index,
           co);

    rt_linked_fixalloc_push(&select_p->co_list, co);
    rt_linked_fixalloc_push(&select_p->runnable_list, co);

    DEBUGF("[runtime.rt_coroutine_dispatch] co=%p to p_index=%d, end", co, select_p->index);
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
    gc_barrier = false;
    mutex_init(&gc_stage_locker, false);
    gc_stage = GC_STAGE_OFF;
    coroutine_count = 0;

    // - 初始化 global linkco
    mutex_init(&global_linkco_locker, false);
    global_linkco_cache = NULL;

    rt_linked_fixalloc_init(&global_gc_worklist);

    // - 初始化 processor 和 coroutine 分配器
    fixalloc_init(&coroutine_alloc, sizeof(coroutine_t));
    fixalloc_init(&processor_alloc, sizeof(n_processor_t));
    mutex_init(&cp_alloc_locker, false);

    DEBUGF("[runtime.sched_init] cpu_count=%d", cpu_count);

    // 全局信号监控
    signal_init();

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    // 初始化 share_processor_index 数组为 0
    processor_list = NULL;

    for (int i = 0; i < cpu_count; ++i) {
        n_processor_t *p = processor_new(i);
        rt_linked_fixalloc_init(&p->gc_worklist);
        p->gc_work_finished = memory->gc_count;
        processor_index[p->index] = p;

        RT_LIST_PUSH_HEAD(processor_list, p);
    }

    // - set first
    uv_key_set(&tls_processor_key, processor_index[0]);
}

void sched_run() {
    // index 0 是 main processor, 直接使用当前主线程运行
    for (int i = 1; i < cpu_count; ++i) {
        n_processor_t *p = processor_index[i];
        // 创建一个新的线程用来处理
        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assert(false && "pthread_create failed %s");
        }

        DEBUGF("[runtime.sched_run] processor run, index=%d, thread_id=%ld", i, (uint64_t) p->thread_id);
    }

    processor_run(processor_index[0]);
    DEBUGF("[runtime.sched_run] main processor exited");
}

n_processor_t *processor_get() {
    n_processor_t *p = uv_key_get(&tls_processor_key);
    return p;
}

coroutine_t *coroutine_get() {
    return uv_key_get(&tls_coroutine_key);
}

void rti_throw(char *msg, bool panic) {
    DEBUGF("[runtime.rti_throw] msg=%s", msg);
    coroutine_t *co = coroutine_get();
    n_interface_t *error = n_error_new(string_new(msg, strlen(msg)), panic);

    co->has_error = true;
    if (co->traces == NULL) {
        n_vec_t *traces = rti_vec_new(&errort_trace_rtype, 0, 0);
        rti_write_barrier_ptr(&co->traces, traces, false);
    }
    rti_write_barrier_ptr(&co->error, error, false);
}

void rti_co_throw(coroutine_t *co, char *msg, bool panic) {
    n_interface_t *error = n_error_new(string_new(msg, strlen(msg)), panic);
    co->has_error = true;
    if (co->traces == NULL) {
        n_vec_t *traces = rti_vec_new(&errort_trace_rtype, 0, 0);
        rti_write_barrier_ptr(&co->traces, traces, false);
    }
    rti_write_barrier_ptr(&co->error, error, false);
}

void coroutine_dump_error(coroutine_t *co) {
    DEBUGF("[runtime.coroutine_dump_error] co=%p, errort base=%p", co, co->error);

    n_string_t *msg = rti_error_msg(co->error);
    DEBUGF("[runtime.coroutine_dump_error] memory_string len: %lu, base: %p", msg->length, msg->data);

    assert(co->traces->length > 0);

    n_trace_t first_trace = {};
    rti_vec_access(co->traces, 0, &first_trace);
    char *dump_msg;
    if (co->main) {
        dump_msg = tlsprintf("coroutine 'main' uncaught error: '%s' at %s:%d:%d\n", (char *) rt_string_ref(msg),
                             (char *) first_trace.path->data, first_trace.line,
                             first_trace.column);
    } else {
        dump_msg = tlsprintf("coroutine %ld uncaught error: '%s' at %s:%d:%d\n", co->id, (char *) rt_string_ref(msg),
                             (char *) first_trace.path->data, first_trace.line,
                             first_trace.column);
    }

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (co->traces->length > 1) {
        char *temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < co->traces->length; ++i) {
            n_trace_t trace = {};
            rti_vec_access(co->traces, i, &trace);
            temp = tlsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char *) trace.ident->data, (char *) trace.path->data,
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
    co->main = FLAG(CO_FLAG_MAIN) & flag;
    co->flag = flag;
    co->arg = arg;
    co->data = NULL;
    co->gc_black = 0;
    co->wait_unlock_fn = NULL;
    co->wait_lock = NULL;
    co->ticket = false;
    co->status = CO_STATUS_RUNNABLE;
    co->p = NULL;
    co->next = NULL;
    co->aco.inited = 0; // 标记为为初始化

    return co;
}

// 如果被抢占会导致全局队列卡死，所以 linked 和 processor 绑定好了, 这就关系到 fixalloc_t 的释放问题
// 除非在这期间不进行抢占，
n_processor_t *processor_new(int index) {
    mutex_lock(&cp_alloc_locker);
    n_processor_t *p = fixalloc_alloc(&processor_alloc);
    mutex_unlock(&cp_alloc_locker);

    // uv_loop_init(&p->uv_loop);
    //    mutex_init(&p->gc_solo_stw_locker, false);
    p->need_stw = 0;
    p->in_stw = 0;

    sc_map_init_64v(&p->caller_cache, 100, 0);
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
    DEBUGF("[wait_sysmon.processor_free] start p=%p, p_index=%d, loop=%p, share_stack=%p|%p", p, p->index,
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

    RDEBUGF("[wait_sysmon.processor_free] will free uv_loop p_index=%d, loop=%p", p->index, &p->uv_loop);
    int index = p->index;

    RDEBUGF("[wait_sysmon.processor_free] will free processor p_index=%d", index);
    mutex_lock(&cp_alloc_locker);
    fixalloc_free(&processor_alloc, p);
    mutex_unlock(&cp_alloc_locker);
    DEBUGF("[wait_sysmon.processor_free] succ free p_index=%d", index);
}

/**
 * 是否所有的 processor 都到达了安全点
 * @return
 */
bool processor_all_safe() {
    PROCESSOR_FOR(processor_list) {
        if (p->status == P_STATUS_EXIT) {
            continue;
        }

        if (processor_need_stw(p)) {
            continue;
        }

        RDEBUGF(
                "[runtime_gc.processor_all_safe] share processor p_index=%d, thread_id=%lu not safe, need_stw=%lu, safe_point=%lu",
                p->index, (uint64_t) p->thread_id, p->need_stw, p->in_stw);
        return false;
    }

    // safe_point 或者获取到 gc_stw_locker
    //    mutex_lock(&solo_processor_locker);
    //    PROCESSOR_FOR(solo_processor_list) {
    //        if (p->status == P_STATUS_EXIT) {
    //            continue;
    //        }
    //
    //        if (processor_need_stw(p)) {
    //            continue;
    //        }
    //
    //        // 获取了锁不代表进入了安全状态,此时可能还在 user code 中, 必须要以 safe_point 为准
    //        // if (mutex_trylock(&p->gc_stw_locker) != 0) {
    //        //     continue;
    //        // }
    //        RDEBUGF("[runtime_gc.processor_all_safe] solo processor p_index_%d=%d, thread_id=%lu not safe", p->share,
    //                p->index,
    //                (uint64_t) p->thread_id);
    //        mutex_unlock(&solo_processor_locker);
    //        return false;
    //    }
    //    mutex_unlock(&solo_processor_locker);

    return true;
}

bool processor_all_wait_safe(int max_count) {
    int count = 0;
    RDEBUGF("[processor_all_wait_safe] start");
    while (!processor_all_safe()) {
        if (count >= max_count) {
            return false; // T
        }

        count += 1;
        usleep(WAIT_BRIEF_TIME * 1000); // 每 1ms 检测一次
    }

    RDEBUGF("[processor_all_wait_safe] end");
    return true;
}

/**
 * 遍历所有的 share processor 和 solo processor 判断 gc 是否全部完成
 * @return
 */
static bool all_gc_work_finished() {
    PROCESSOR_FOR(processor_list) {
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
    DEBUGF("[runtime_gc.wait_all_gc_work_finished] start");

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
    //    assert(p->status != status);

    // rtcall 是不稳定状态，可以随时切换到任意状态
    //    if (p->status == P_STATUS_RTCALL) {
    //        p->status = status;
    //        return;
    //    }

    // 对于 share processor 来说， tplcall 和 rtcall 是不稳定的，需要切换走的，所以不需要被 thread_locker 锁住
    //    if (p->share && p->status == P_STATUS_TPLCALL) {
    //        p->status = status;
    //        return;
    //    }

    // solo 状态下 tplcall 可以切换到 dispatch, 但是不能进入到 running
    //    if (!p->share && p->status == P_STATUS_TPLCALL && status == P_STATUS_DISPATCH) {
    //        p->status = status;
    //        return;
    //    }

    // 必须先获取 thread_locker 再获取 gc_stw_locker
    mutex_lock(&p->thread_locker);

    // 特殊处理 2， solo 切换成 running 时需要获取 gc_stw_locker, 如果 gc_stw_locker 阻塞说明当前正在 stw
    // 必须获取到 stw locker 才能切换到 running 状态(runnable -> running/ tpl_call -> running)
    //    if (!p->share && status == P_STATUS_RUNNING) {
    //        mutex_lock(&p->gc_solo_stw_locker);
    //    }

    p->status = status;

    //    if (!p->share && status == P_STATUS_RUNNING) {
    //        mutex_unlock(&p->gc_solo_stw_locker);
    //    }

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
