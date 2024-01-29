#include "processor.h"

#include <ucontext.h>
#include <uv.h>

#include "builtin.h"
#include "nutils/nutils.h"
#include "runtime.h"

int cpu_count;
bool processor_need_stw;
bool processor_need_exit;

processor_t *share_processor_list; // 共享协程列表的数量一般就等于线程数量
processor_t *solo_processor_list;  // 独享协程列表其实就是多线程

int solo_processor_count;       // 累计数量
rt_linked_t global_gc_worklist; // 独享协程列表其实就是多线程
mutex_t global_gc_locker;       // 全局 gc locker

uv_key_t tls_processor_key = 0;
uv_key_t tls_coroutine_key = 0;

fixalloc_t coroutine_alloc;
fixalloc_t processor_alloc;
mutex_t cp_alloc_locker;

__attribute__((optimize(0))) void debug_ret(uint64_t rbp, uint64_t ret_addr) {
    DEBUGF("[runtime.debug_ret] rbp=%p, ret_addr=%p", (void *)rbp, (void *)ret_addr);
}

/**
 * 在 user_code 期间的超时抢占
 */
__attribute__((optimize(0))) void co_preempt_yield() {
    // 绝对不可抢占点，可以放心进行 RDEBUGF 操作
    RDEBUGF("[runtime.co_preempt_yield] start, %lu", (uint64_t)uv_thread_self());

    processor_t *p = processor_get();
    assert(p);
    coroutine_t *co = p->coroutine;
    assert(co);

    RDEBUGF("[runtime.co_preempt_yield] p_index_%d=%d, co=%p, status=%d, will yield", p->share, p->index, co, co->status);

    // wait sysmon 可能还没有来得及设置不可抢占，所以这里重复进行不可抢占声明
    // 不需要加锁，wait_sysmon 不会和当前 p 竞争
    p->can_preempt = false;
    p->co_started_at = 0;

    // 原来是 running 才需要 push 到可执行队列钟
    if (co->status == CO_STATUS_RUNNING) {
        co->status = CO_STATUS_RUNNABLE;

        mutex_lock(&p->co_locker);
        rt_linked_push(&p->runnable_list, co);
        mutex_unlock(&p->co_locker);

        RDEBUGF("[runtime.co_preempt_yield.thread_locker] co=%p push success", co);
    }

    _co_yield(p, co);

    // 接下来将直接 return 到用户态，不经过 post_tpl_hook, 所以直接更新为允许抢占
    // yield 切换回了用户态，此时允许抢占，所以不能再使用 RDEBUG, 而是 DEBUG
    DEBUGF("[runtime.co_preempt_yield] yield resume end, will set enable_preempt, p_index_%d=%d co=%p, p->co=%p", p->share, p->index, co,
           p->coroutine);

    co->status = CO_STATUS_RUNNING;
    enable_preempt(p);
}

/**
 * 不能在信号处理函数中调用不可重入函数，包括 malloc/free/printf 等
 * @param sig
 * @param info
 * @param ucontext
 */
static void thread_handle_sig(int sig, siginfo_t *info, void *ucontext) {
    ucontext_t *ctx = ucontext;

#ifdef __x86_64__
    int REG_RSP = 15;
    int REG_RIP = 16;
    int64_t *rsp = (int64_t *)ctx->uc_mcontext.gregs[REG_RSP];
    // return addr, 这个是 async_preempt 返回后应该去的地方
    int64_t rip = ctx->uc_mcontext.gregs[REG_RIP];

    // 由于被抢占的函数可以会在没有 sub 保留 rsp 的情况下使用 rsp-0x10 这样的空间地址
    // 所以需要为 rsp 预留足够的栈空间给被抢占的函数, 避免后续的操作污染被抢占的函数
    rsp -= 128; // 一个指针是 8byte, 所以这里是 128 * 8 = 1024 个字节

    // push rip
    rsp--; // 栈中预留返回地址
    *rsp = rip;

    RDEBUGF("[runtime.thread_handle_sig] rip=%p save to %p", (void *)rip, rsp);

    ctx->uc_mcontext.gregs[REG_RSP] = (int64_t)rsp;
    ctx->uc_mcontext.gregs[REG_RIP] = (int64_t)async_preempt;
#elif
#endif
}

static void processor_uv_close(processor_t *p) {
    // 关闭 uv_loop
    RDEBUGF("[runtime.processor_uv_close] will close loop=%p, loop_req_count=%u, p_index_%d=%d", &p->uv_loop, p->uv_loop.active_reqs.count,
            p->share, p->index);
    uv_close((uv_handle_t *)&p->timer, NULL); // io_run 等待 close 完成！

    uv_run(&p->uv_loop, UV_RUN_DEFAULT); // 等待上面注册的 uv_close 完成

    int result = uv_loop_close(&p->uv_loop);

    if (result != 0) {
        RDEBUGF("[runtime.processor_uv_close] uv loop close failed, code=%d, msg=%s, p_index_%d=%d", result, uv_strerror(result), p->share,
                p->index);
        assert(false && "uv loop close failed");
    }

    RDEBUGF("[runtime.processor_uv_close] processor uv close success p_index_%d=%d", p->share, p->index);
}

static void coroutine_wrapper() {
    coroutine_t *co = aco_get_arg();
    assert(co);
    processor_t *p = co->p;
    assert(p);

    TDEBUGF("[runtime.coroutine_wrapper] co=%p, main=%d, gc_work=%d", co, co->main, co->gc_work);

    // 设置为 true 不需要抢占锁，因为之前的状态是 false, 肯定不会发生抢占，和 wait_sysmon 不冲突
    p->can_preempt = true;

    // 调用并处理请求参数 TODO 改成内联汇编实现，需要 #ifdef 判定不通架构
    ((void_fn_t)co->fn)();

    if (co->main) {
        // 通知所有协程退出
        processor_set_exit();
        DEBUGF("[runtime.coroutine_wrapper] co=%p, main coroutine exit, set processor_need_exit=true", co);
    }

    // 即将退出，不再允许抢占
    disable_preempt(p);

    co->status = CO_STATUS_DEAD;
    TDEBUGF("[runtime.coroutine_wrapper] co=%p will dead", co);
    aco_exit1(&co->aco);
}

/**
 * 必须在用户态/或者 processor 还没有开始 run 之前初始化
 * @param p
 * @param co
 */
static void coroutine_aco_init(processor_t *p, coroutine_t *co) {
    assert(p);
    assert(co);
    if (co->aco.inited) {
        return;
    }

    assert(p->main_aco.inited);
    assert(p->share_stack.sz > 0);

    aco_create_init(&co->aco, &p->main_aco, &p->share_stack, 0, coroutine_wrapper, co);
}

bool processor_get_stw() {
    return processor_need_stw;
}

void processor_stop_the_world() {
    processor_need_stw = true;
}

void processor_start_the_world() {
    processor_need_stw = false;
}

void uv_stop_callback(uv_timer_t *timer) {
    processor_t *p = timer->data;
    // DEBUGF("[runtime.io_run.uv_stop_callback] loop=%p, p_index=%d", timer->loop, p->index);

    uv_timer_stop(timer);
    // uv_close((uv_handle_t *)timer, NULL);

    uv_stop(timer->loop);
}

/**
 * timeout_ms 推荐 5ms ~ 10ms
 */
int io_run(processor_t *p, uint64_t timeout_ms) {
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
void coroutine_resume(processor_t *p, coroutine_t *co) {
    assert(co->status == CO_STATUS_RUNNABLE && "coroutine status must be runnable");

    // 首次 resume 需要进行初始化
    if (!co->aco.inited) {
        coroutine_aco_init(p, co);
    }

    // - 再 tls 中记录正在运行的协程
    uv_key_set(&tls_coroutine_key, co);
    co->p = p; // 运行前进行绑定，让 coroutine 在运行中可以准确的找到 processor

    // 将 RIP 指针移动用户代码片段中
    RDEBUGF("[runtime.coroutine_resume] aco_resume will start, co=%p, is_main=%d, aco=%p, save_stack=%p(%zu)", co, co->main, &co->aco,
            co->aco.save_stack.ptr, co->aco.save_stack.sz);

    // 获取锁成功再进行数据更新
    p->coroutine = co;
    p->co_started_at = uv_hrtime();

    aco_resume(&co->aco);

    RDEBUGF("[runtime.coroutine_resume] resume backend, co=%p, aco=%p, status=%d, gc_work=%d", co, &co->aco, co->status, co->gc_work);

    assert(co->status != CO_STATUS_RUNNING);
    uint64_t time = (uv_hrtime() - p->co_started_at) / 1000 / 1000;

    p->co_started_at = 0;
    p->coroutine = NULL;

    RDEBUGF("[runtime.coroutine_resume] aco_yield completed, co=%p, aco=%p, run_time=%lu ms, gc_work=%d", co, &co->aco, time, co->gc_work);
}

// handle by thread
static void processor_run(void *raw) {
    processor_t *p = raw;
    RDEBUGF("[runtime.processor_run] start, p_index=%d, addr=%p, share=%d, loop=%p", p->index, p, p->share, &p->uv_loop);

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

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        RDEBUGF("[runtime.processor_run] handle, p_index_%d=%d", p->share, p->index);
        // - stw
        if (processor_get_stw()) {
        STW_WAIT:
            RDEBUGF("[runtime.processor_run] need stw, set safe_point=true,  p_index_%d=%d", p->share, p->index);
            p->safe_point = true;

            while (processor_get_stw()) {
                RDEBUGF("[runtime.processor_run] p_index_%d=%d, stw loop....", p->share, p->index);
                usleep(WAIT_SHORT_TIME * 1000); // 每 10ms 检测一次 STW 是否解除 TODO 等的事件有点长，影响运行了
            }

            p->safe_point = false;
            RDEBUGF("[runtime.processor_run] p_index_%d=%d, stw completed, set safe_point=false", p->share, p->index);
        }

        // - exit
        if (processor_get_exit()) {
            write(STDOUT_FILENO, "---pr_0\n", 8);
            RDEBUGF("[runtime.processor_run] p_index_%d=%d, need stop, goto exit", p->share, p->index);
            goto EXIT;
        }

        // - 处理 coroutine (找到 io 可用的 goroutine)
        while (true) {
            mutex_lock(&p->co_locker);
            coroutine_t *co = rt_linked_pop(&p->runnable_list);
            mutex_unlock(&p->co_locker);

            if (!co) {
                // runnable list 已经处理完成
                RDEBUGF("[runtime.processor_run] runnable is empty, p_index_%d=%d", p->share, p->index);
                break;
            }

            RDEBUGF("[runtime.processor_run] will handle coroutine, p_index_%d=%d, co=%p, status=%d", p->share, p->index, co, co->status);

            assert(co->status == CO_STATUS_RUNNABLE && "coroutine status must be runnable");

            coroutine_resume(p, co);
            assert(p->can_preempt == false);

            if (processor_get_stw()) {
                RDEBUGF("[runtime.processor_run] coroutine resume and p need stw, will goto stw, p_index_%d=%d, co=%p, status=%d", p->share,
                        p->index, co, co->status);
                goto STW_WAIT;
            }

            RDEBUGF("[runtime.processor_run] coroutine resume completed, p_index_%d=%d, co=%p, status=%d", p->share, p->index, co,
                    co->status);
        }

        // solo processor exit logic
        if (!p->share) {
            assert(p->co_list.count == 1);
            coroutine_t *solo_co = rt_linked_first(&p->co_list)->value;
            if (solo_co->status == CO_STATUS_DEAD) {
                RDEBUGF("[runtime.processor_run] solo processor co exit, will exit processor run, p_index=%d, co=%p, status=%d", p->index,
                        solo_co, solo_co->status);
                write(STDOUT_FILENO, "---pr_1\n", 8);
                goto EXIT;
            }
        }

        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(p, WAIT_SHORT_TIME);
    }

EXIT:
    p->exit = true;
    p->thread_id = 0;
    processor_uv_close(p);

    RDEBUGF("[runtime.processor_run] exited, p_index_%d=%d", p->share, p->index);
}

void coroutine_dispatch(coroutine_t *co) {
    DEBUGF("[runtime.coroutine_dispatch] co=%p, solo=%d, share_processor_count=%d", co, co->solo, cpu_count);

    // 分配 coroutine 之前需要给 coroutine 确认初始颜色, 如果是新增的 coroutine，默认都是黑色
    if (gc_stage == GC_STAGE_MARK) {
        co->gc_black = memory->gc_count;
    }

    // - 协程独享线程
    if (co->solo) {
        processor_t *p = processor_new(solo_processor_count++);
        rt_linked_push(&p->co_list, co);
        rt_linked_push(&p->runnable_list, co);

        RT_LIST_PUSH_HEAD(solo_processor_list, p);

        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assert(false && "pthread_create failed");
        }

        DEBUGF("[runtime.coroutine_dispatch] solo processor create, thread_id=%ld", (uint64_t)p->thread_id);
        return;
    }

    // goroutine 默认状态是 runnable
    assert(co->status == CO_STATUS_RUNNABLE);

    // - 遍历 shared_processor_list 找到 co_list->count 最小的 processor 进行调度
    processor_t *select_p = NULL;

    PROCESSOR_FOR(share_processor_list) {
        if (!select_p || p->co_list.count < select_p->co_list.count) {
            select_p = p;
        }
    }

    assert(select_p);
    DEBUGF("[runtime.coroutine_dispatch] select_p_index_%d=%d will push co=%p", select_p->share, select_p->index, co);

    mutex_lock(&select_p->co_locker);
    rt_linked_push(&select_p->co_list, co);
    rt_linked_push(&select_p->runnable_list, co);
    mutex_unlock(&select_p->co_locker);

    DEBUGF("[runtime.coroutine_dispatch] co=%p to p_index_%d=%d, end", co, select_p->share, select_p->index);
}

/**
 * 各种全局变量初始化都通过该方法
 */
void processor_init() {
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
    processor_need_stw = false;
    gc_barrier = false;
    gc_stage_locker = mutex_new(false);
    gc_stage = GC_STAGE_OFF;
    solo_processor_count = 0;

    // - 初始化 processor 和 coroutine 分配器
    fixalloc_init(&coroutine_alloc, sizeof(coroutine_t));
    fixalloc_init(&processor_alloc, sizeof(processor_t));
    mutex_init(&cp_alloc_locker, false);

    DEBUGF("[runtime.processor_init] cpu_count=%d", cpu_count);

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    share_processor_list = NULL;
    solo_processor_list = NULL;

    rt_linked_init(&global_gc_worklist, NULL, NULL);
    mutex_init(&global_gc_locker, false);

    for (int i = 0; i < cpu_count; ++i) {
        processor_t *p = processor_new(i);
        p->share = true;

        RT_LIST_PUSH_HEAD(share_processor_list, p);

        // 创建一个新的线程用来处理
        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assert(false && "pthread_create failed %s");
        }

        DEBUGF("[runtime.processor_init] processor create, index=%d, thread_id=%ld", i, (uint64_t)p->thread_id);
    }
}

processor_t *processor_get() {
    processor_t *p = uv_key_get(&tls_processor_key);
    return p;
}

coroutine_t *coroutine_get() {
    return uv_key_get(&tls_coroutine_key);
}

void rt_processor_attach_errort(char *msg) {
    DEBUGF("[runtime.rt_processor_attach_errort] msg=%s", msg);
    // processor_t* p = processor_get();
    // n_errort* errort = n_errort_new(string_new(msg, strlen(msg)), 1);
    // p->errort = errort;
}

void processor_dump_errort(n_errort *errort) {
    DEBUGF("[runtime.processor_dump_errort] errort base=%p", errort);
    n_string_t *msg = errort->msg;
    DEBUGF("[runtime.processor_dump_errort] memory_string len: %lu, base: %p", msg->length, msg->data);
    assert(errort->traces->length > 0);

    n_trace_t first_trace = {};
    vec_access(errort->traces, 0, &first_trace);
    char *dump_msg = dsprintf("catch error: '%s' at %s:%d:%d\n", (char *)errort->msg->data, (char *)first_trace.path->data,
                              first_trace.line, first_trace.column);

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (errort->traces->length > 1) {
        char *temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < errort->traces->length; ++i) {
            n_trace_t trace = {};
            vec_access(errort->traces, i, &trace);
            temp = dsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char *)trace.ident->data, (char *)trace.path->data, trace.line, trace.column);
            VOID write(STDOUT_FILENO, temp, strlen(temp));
        }
    }
}

void mark_ptr_black(void *ptr) {
    addr_t addr = (addr_t)ptr;
    // get mspan by ptr
    mspan_t *span = span_of(addr);

    // get span index
    uint64_t obj_index = (addr - span->base) / span->obj_size;
    mark_obj_black_with_lock(span, obj_index);
}

/**
 * TODO target use debug, can delete
 * @param target
 */
__attribute__((optimize(0))) void pre_tpl_hook(char *target) {
    coroutine_t *co = coroutine_get();
    assert(co);
    processor_t *p = processor_get();

    // 这里需要抢占到锁再进行更新，否则和 wait_sysmon 存在冲突。
    // 如果 wait_sysmon 已经获取了锁，则会阻塞在此处等待 wait_symon 进行抢占, 避免再次进入 tpl
    write(STDOUT_FILENO, "___pre_0\n", 9);
    disable_preempt(p);
    co->status = CO_STATUS_SYSCALL;

    write(STDOUT_FILENO, "___pre_1\n", 9);
    DEBUGF("[runtime.pre_tpl_hook] will set can_preempt=false, co=%p, status=%d ,target=%s", co, co->status, target);
    write(STDOUT_FILENO, "___pre_2\n", 9);

    aco_t *aco = &co->aco;
    uint64_t rbp_value;
#ifdef __x86_64__
    asm("mov %%rbp, %0" : "=r"(rbp_value));
#elif
    assert(false && "not support");
#endif

    aco->bp_offset = (uint64_t)aco->share_stack->align_retptr - rbp_value;

#ifdef DEBUG
    addr_t ret_addr = fetch_addr_value(rbp_value + POINTER_SIZE);
    fndef_t *fn = find_fn(ret_addr);
    if (fn) {
        DEBUGF("[runtime.pre_tpl_hook] ret_addr=%p, fn=%s -> %s, path=%s:%lu", (void *)ret_addr, fn->name, target, fn->rel_path, fn->line);
    }
    // 基于 share stack 计算 offset
    DEBUGF("[runtime.pre_tpl_hook] aco->align_retptr=%p, rbp=%p, bp_offset=%lu", aco->share_stack->align_retptr, (void *)rbp_value,
           aco->bp_offset);
#endif
}

__attribute__((optimize(0))) void post_tpl_hook(char *target) {
    processor_t *p = processor_get();
    assert(p);
    coroutine_t *co = coroutine_get();
    assert(co);

    assert(p->can_preempt == false);

    // 阻塞的系统调用期间强行开启了 stw，此时直接退出到 yield, 不能进入用户流程
    if (processor_get_stw()) {
        TDEBUGF("[runtime.post_tpl_hook] co=%p, need stw will yield", co);
        co_yield_runnable(p, co);
    }

    DEBUGF("[runtime.post_tpl_hook] co=%p, will set can_preempt=true", co);

    co->status = CO_STATUS_RUNNING;
    enable_preempt(p);
}

coroutine_t *coroutine_new(void *fn, n_vec_t *args, bool solo, bool main) {
    mutex_lock(&cp_alloc_locker);
    coroutine_t *co = fixalloc_alloc(&coroutine_alloc);
    mutex_unlock(&cp_alloc_locker);

    co->fn = fn;
    co->solo = solo;
    co->gc_black = 0;
    co->status = CO_STATUS_RUNNABLE;
    co->args = args;
    co->p = NULL;
    co->result = NULL;
    co->main = main;
    co->next = NULL;
    co->aco.inited = 0; // 标记为为初始化

    TDEBUGF("[coroutine_new] co=%p new success", co);
    return co;
}

// 如果被抢占会导致全局队列卡死，所以 linked 和 processor 绑定好了, 这就关系到 fixalloc_t 的释放问题
// 除非在这期间不进行抢占，
processor_t *processor_new(int index) {
    mutex_lock(&cp_alloc_locker);
    processor_t *p = fixalloc_alloc(&processor_alloc);
    mutex_unlock(&cp_alloc_locker);

    // uv_loop_init(&p->uv_loop);
    mutex_init(&p->gc_barrier_locker, false);
    mutex_init(&p->disable_preempt_locker, false);
    mutex_init(&p->co_locker, false);
    p->sig.sa_flags = 0;
    p->thread_id = 0;
    p->coroutine = NULL;
    p->co_started_at = 0;
    p->mcache.flush_gen = 0; // 线程维度缓存，避免内存分配锁
    rt_linked_init(&p->co_list, NULL, NULL);
    rt_linked_init(&p->gc_worklist, NULL, NULL);
    rt_linked_init(&p->runnable_list, NULL, NULL);
    p->gc_work_finished = false;
    p->index = index;
    p->can_preempt = false;
    p->next = NULL;

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

void processor_free(processor_t *p) {
    RDEBUGF("[wait_sysmon.processor_free] start p=%p, p_index_%d=%d, loop=%p, share_stack=%p|%p", p, p->share, p->index, &p->uv_loop,
            &p->share_stack, p->share_stack.ptr);

    aco_share_stack_destroy(&p->share_stack);
    rt_linked_destroy(&p->co_list);
    aco_destroy(&p->main_aco);

    RDEBUGF("[wait_sysmon.processor_free] will free uv_loop p_index_%d=%d, loop=%p", p->share, p->index, &p->uv_loop);
    // uv_loop_close(&p->uv_loop); // TODO close 会导致 segmentation fault

    int share = p->share;
    int index = p->index;

    RDEBUGF("[wait_sysmon.processor_free] will free processor p_index_%d=%d", share, index);
    fixalloc_free(&processor_alloc, p);
    RDEBUGF("[wait_sysmon.processor_free] end p_index_%d=%d", share, index);
}

/**
 * 是否所有的 processor 都到达了安全点
 * @return
 */
bool processor_all_safe() {
    processor_t *p = share_processor_list;
    while (p) {
        if (p->exit) {
            p = p->next;
            continue;
        }

        if (p->safe_point) {
            p = p->next;
            continue;
        }

        RDEBUGF("[runtime.processor_all_safe] share processor p_index_%d=%d, thread_id=%lu not safe", p->share, p->index,
                (uint64_t)p->thread_id);
        return false;
    }

    p = solo_processor_list;
    while (p) {
        if (p->exit) {
            p = p->next;
            continue;
        }

        if (p->safe_point) {
            p = p->next;
            continue;
        }

        // 如果 coroutine 已经开始调度，并且处于 syscall 状态，则认为当前 solo processor 已经进入了安全点
        if (p->coroutine && p->coroutine->status == CO_STATUS_SYSCALL) {
            p = p->next;
            continue;
        }

        RDEBUGF("[runtime.processor_all_safe] solo processor p_index_%d=%d not safe", p->share, p->index);
        return false;
    }

    return true;
}

void processor_wait_all_safe() {
    while (!processor_all_safe()) {
        usleep(WAIT_SHORT_TIME * 1000);
        RDEBUGF("[runtime.processor_wait_all_safe] wait...");
    }

    RDEBUGF("[runtime.processor_wait_all_safe] end");
}

/**
 * 遍历所有的 share processor 和 solo processor 判断 gc 是否全部完成
 * @return
 */
static bool all_gc_work_finished() {
    PROCESSOR_FOR(share_processor_list) {
        if (p->gc_work_finished == false) {
            return false;
        }
    }

    PROCESSOR_FOR(solo_processor_list) {
        if (p->gc_work_finished == false) {
            return false;
        }
    }

    return true;
}

/**
 * 需要等待独享和共享协程的 gc_work 全部完成 mark
 */
void wait_all_gc_work_finished() {
    RDEBUGF("[runtime_gc.wait_all_gc_work_finished] start");

    while (all_gc_work_finished() == false) {
        usleep(WAIT_SHORT_TIME * 1000);
    }

    DEBUGF("[runtime_gc.wait_all_gc_work_finished] all processor gc work finish");

    // 已经等到了所有的 gc_work_finish, 则重置为 false，避免影响下一次 GC 判断
    PROCESSOR_FOR(share_processor_list) {
        p->gc_work_finished = false;
    }

    PROCESSOR_FOR(solo_processor_list) {
        p->gc_work_finished = false;
    }

    RDEBUGF("[runtime_gc.wait_all_gc_work_finished] share/solo processor to gc_work_finished=false, end");
}

void *global_gc_worklist_pop() {
    return rt_linked_pop(&global_gc_worklist);
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
    addr_t size = (addr_t)old_st->align_retptr - (addr_t)sp;

    // 原则上应该从 old_share_stack->align_retptr(最大值)
    memmove(new_st->align_retptr - size, sp, size);

    // 更新栈里面 prev bp 值
    addr_t bp_ptr = (addr_t)aco->reg[ACO_REG_IDX_BP];

    while (true) {
        addr_t prev_bp_value = fetch_addr_value((addr_t)bp_ptr); // 可能压根没有报错 bp 的值，所以必须有一个中断条件

        // 边界情况处理
        if (prev_bp_value <= bp_ptr) {
            break;
        }

        if (prev_bp_value < (addr_t)sp) {
            break;
        }

        if (prev_bp_value > (addr_t)old_st->align_retptr) {
            break;
        }

        addr_t prev_bp_offset = (addr_t)old_st->align_retptr - (addr_t)bp_ptr;

        addr_t new_prev_bp_value = (addr_t)new_st->align_retptr - ((addr_t)old_st->align_retptr - prev_bp_value);

        // 更新相关位置的值
        memmove((void *)((addr_t)new_st->align_retptr - prev_bp_offset), &new_prev_bp_value, POINTER_SIZE);

        // 更新 rbp_ptr 的指向
        bp_ptr = prev_bp_value;
    }

    // 更新 bp/sp 寄存器
    aco->reg[ACO_REG_IDX_BP] = (void *)((addr_t)new_st->align_retptr - ((addr_t)old_st->align_retptr - (addr_t)aco->reg[ACO_REG_IDX_BP]));
    aco->reg[ACO_REG_IDX_SP] = (void *)((addr_t)new_st->align_retptr - ((addr_t)old_st->align_retptr - (addr_t)aco->reg[ACO_REG_IDX_SP]));

    // 更新 co share_stack 指向
    aco->share_stack = new_st;
    new_st->owner = aco;
}
