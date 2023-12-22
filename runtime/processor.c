#include "processor.h"

#include <uv.h>

#include "builtin.h"
#include "runtime.h"
#include "utils/assertf.h"

int cpu_count;
bool processor_need_stw;
bool processor_need_exit;
slice_t* share_processor_list; // 共享协程列表的数量一般就等于线程数量
slice_t* solo_processor_list;  // 独享协程列表其实就是多线程

uv_key_t tls_processor_key;
uv_key_t tls_coroutine_key;

static void coroutine_wrapper() {
    coroutine_t* co = aco_get_arg();

    // 调用并处理请求参数 TODO 改成内联汇编实现，需要 #ifdef 判定不通架构
    ((void_fn_t)co->fn)();

    if (co->main) {
        // 通知所有协程退出
        processor_set_exit();
    }

    // 退出协程，设置协程退出标识
    co->status = CO_STATUS_DEAD;
    aco_exit();
}

bool processor_get_stw() {
    return processor_need_stw;
}

void processor_set_stw() {
    processor_need_stw = true;
}

void uv_stop_callback(uv_timer_t* timer) {
    uv_stop(timer->loop);
}

/**
 * timeout_ms 推荐 5ms ~ 10ms
 */
int io_run(processor_t* p, uint64_t timeout_ms) {
    // 初始化计时器
    uv_timer_t timeout_timer;
    uv_timer_init(p->uv_loop, &timeout_timer);
    timeout_timer.data = p;

    // 设置计时器超时回调，这将在超时后停止事件循环
    uv_timer_start(&timeout_timer, uv_stop_callback, timeout_ms, 0); // 只触发一次

    return uv_run(p->uv_loop, UV_RUN_DEFAULT);
}

/**
 * 检测 coroutine 当前是否需要单独线程调度，如果不需要单独线程则直接在当前线程进行 aco_resume
 */
void coroutine_resume(processor_t* p, coroutine_t* co) {
    assertf(co->status == CO_STATUS_RUNNABLE, "coroutine status must be runnable");

    // resume 之前需要确保 main_co 的指向
    if (co->aco == NULL) {
        aco_share_stack_t* sstk = aco_share_stack_new(0); // 使用默认栈大小
        assert(sstk);
        co->aco = aco_create(p->main_aco, sstk, 0, coroutine_wrapper, co);
    } else {
        // 直接切换 main_aco, 用于切换
        co->aco->main_co = p->main_aco;
    }

    co->status = CO_STATUS_RUNNING;

    p->co_started_at = uv_hrtime();
    p->coroutine = co;

    // - 再 tls 中记录正在运行的协程
    uv_key_set(&tls_coroutine_key, co);

    // 将 RIP 指针移动用户代码片段中
    DEBUGF("[runtime.coroutine_resume] aco_resume will start, co=%p, aco=%p, p->co_started_at=%lu", co, co->aco, p->co_started_at);

    aco_resume(co->aco);

    DEBUGF("[runtime.coroutine_resume] aco_yield completed, co=%p, aco=%p, status=%d", co, co->aco, co->status);
    p->co_started_at = 0; // 调度完成后及时清空 coroutine，避免被 sysmon thread 抢占
}

// handle by thread
void processor_run(void* raw) {
    DEBUGF("[runtime.share_process_run] processor=%p", raw);
    processor_t* p = raw;

    // 初始化 aco 和 main_co
    aco_thread_init(NULL);
    p->main_aco = aco_create(NULL, NULL, 0, NULL, NULL);
    assert(p->main_aco);

    // 注册线程信号监听， 用于抢占式调度
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = thread_handle_sigurg;
    if (sigaction(SIGURG, &sa, NULL) == -1) {
        assertf(false, "sigaction failed");
    }

    // 将 p 存储在线程维度全局遍历中，方便直接在 coroutine 运行中读取相关的 processor
    uv_key_set(&tls_processor_key, p);

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        //        DEBUGF("[runtime.share_process_run] handle %lu", (uint64_t)uv_thread_self());

        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(p, 5);

        // - 处理 coroutine (找到 io 可用的 goroutine)
        while (true) {
            coroutine_t* co = linked_pop(p->runnable_list);
            if (!co) {
                // runnable list 已经处理完成
                break;
            }
            DEBUGF("[runtime.share_process_run] will handle coroutine, processor=%p, co=%p, status=%d", p, co, co->status);

            assertf(co->status == CO_STATUS_RUNNABLE, "coroutine status must be runnable");
            coroutine_resume(p, co);

            // 当前线程如果被 block syscall 独享占用，说明 processor 已经绑定了其他线程进行工作
            // 则此处不需要在进行后续的处理， 直接退出当前线程即可
            if (!processor_own(p)) {
                DEBUGF("[runtime.share_process_run] processor=%p, not own, thread_id=%lu, will exit this thread", p,
                       (uint64_t)p->thread_id);
                goto EXIT;
            }
        }

        // - stw
        if (processor_get_stw()) {
            p->safe_point = true;
            while (processor_get_stw()) {
                usleep(50); // 每 50ms 检测一次 STW 是否解除
            }
        }

        // exit
        if (processor_get_exit()) {
            DEBUGF("[runtime.share_process_run] processor=%p, need stop, goto exit", p);
            goto EXIT;
        }
    }

EXIT:
    if (!p->share) {
        processor_free(p); // 独享 processor 进行释放清理
    } else {
        p->exit = true;
    }
    DEBUGF("[runtime.processor_run] thread %lu exited", (uint64_t)uv_thread_self());
}

void coroutine_dispatch(coroutine_t* co) {
    DEBUGF("[runtime.coroutine_dispatch] co=%p, solo=%d, share_processor_list=%d", co, co->solo, share_processor_list->count);

    if (co->solo) {
        processor_t* p = processor_new();
        slice_push(solo_processor_list, p);

        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assertf(false, "pthread_create failed %s", strerror(errno));
        }

        DEBUGF("[runtime.coroutine_dispatch] solo processor create, thread_id=%ld", (uint64_t)p->thread_id);
        return;
    }

    // goroutine 默认状态是 runnable
    assert(co->status == CO_STATUS_RUNNABLE);

    // - 遍历 shared_processor_list 找到 co_list->count 最小的 processor 进行调度
    processor_t* min_p = NULL;
    SLICE_FOR(share_processor_list) {
        processor_t* p = SLICE_VALUE(share_processor_list);

        if (!min_p || p->co_list->count < min_p->co_list->count) {
            min_p = p;
        }
    }

    assert(min_p);
    DEBUGF("[runtime.coroutine_dispatch] min_p=%p, co_list=%p, runnable_list=%p", min_p, min_p->co_list, min_p->runnable_list);

    linked_push(min_p->co_list, co);
    linked_push(min_p->runnable_list, co);
    DEBUGF("[runtime.coroutine_dispatch] co=%p to processor=%p", co, min_p);
}

void processor_init() {
    // - 读取当前 cpu 线程数初始化相应数量的 p
    uv_cpu_info_t* info;
    uv_cpu_info(&info, &cpu_count);
    uv_free_cpu_info(info, cpu_count);

    // - 初始化线程维度遍历
    uv_key_create(&tls_processor_key);
    uv_key_create(&tls_coroutine_key);

    // 初始化 aco 需要的 tls 变量(不能再线程中 create)
    uv_key_create(&aco_gtls_co);
    uv_key_create(&aco_gtls_last_word_fp);
    uv_key_create(&aco_gtls_fpucw_mxcsr);

    DEBUGF("[runtime.processor_init] cpu_count=%d", cpu_count);

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    share_processor_list = slice_capacity_new(cpu_count);
    solo_processor_list = slice_new();

    for (int i = 0; i < cpu_count; ++i) {
        processor_t* p = processor_new();
        p->share = true;
        slice_push(share_processor_list, p);

        // 创建一个新的线程用来处理
        if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
            assertf(false, "pthread_create failed %s", strerror(errno));
        }

        DEBUGF("[runtime.processor_init] processor create, index=%d, thread_id=%ld", i, (uint64_t)p->thread_id);
    }
}

processor_t* processor_get() {
    processor_t* p = uv_key_get(&tls_processor_key);
    return p;
}

coroutine_t* coroutine_get() {
    return uv_key_get(&tls_coroutine_key);
}

void rt_processor_attach_errort(char* msg) {
    DEBUGF("[runtime.rt_processor_attach_errort] msg=%s", msg);
    //    processor_t* p = processor_get();
    //    n_errort* errort = n_errort_new(string_new(msg, strlen(msg)), 1);
    //    p->errort = errort;
}

void processor_dump_errort(n_errort* errort) {
    DEBUGF("[runtime.processor_dump_errort] errort base=%p", errort)
    n_string_t* msg = errort->msg;
    DEBUGF("[runtime.processor_dump_errort] memory_string len: %lu, base: %p", msg->length, msg->data);
    assert(errort->traces->length > 0);

    n_trace_t first_trace = {};
    vec_access(errort->traces, 0, &first_trace);
    char* dump_msg = dsprintf("catch error: '%s' at %s:%d:%d\n", (char*)errort->msg->data, (char*)first_trace.path->data, first_trace.line,
                              first_trace.column);

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (errort->traces->length > 1) {
        char* temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < errort->traces->length; ++i) {
            n_trace_t trace = {};
            vec_access(errort->traces, i, &trace);
            temp = dsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char*)trace.ident->data, (char*)trace.path->data, trace.line, trace.column);
            VOID write(STDOUT_FILENO, temp, strlen(temp));
        }
    }
}

void pre_block_syscall() {
    processor_t* p = processor_get();
    coroutine_t* c = coroutine_get();
    c->status = CO_STATUS_SYSCALL;

    // solo coroutine 占用整个线程，所以即使是阻塞的 syscall 也不需要进行什么操作
    if (c->solo) {
        return;
    }

    // 接下来 coroutine 将会独占当前线程, 将 thread 绑定在 coroutine 上
    c->thread_id = p->thread_id;

    // 创建一个新的线程用于其他 processor 调度
    if (uv_thread_create(&p->thread_id, processor_run, p) != 0) {
        assertf(false, "pthread_create failed %s", strerror(errno));
    }
}

void post_block_syscall() {
    coroutine_t* co = coroutine_get();
    processor_t* p = processor_get();

    if (co->solo) {
        // 只有独享线程在遇到阻塞调用时需要延迟处理 stw, 共享线程遇到阻塞 io 时会启动一个新的线程进行处理
        // 处理完成后直接就退出了，不需要担心后续的操作
        if (processor_get_stw()) {
            p->safe_point = true;
            while (processor_get_stw()) {
                usleep(50); // 每 50ms 检测一次 STW 是否解除
            }
        }
        co->status = CO_STATUS_RUNNING; // coroutine 继续运行
        return;
    }

    // block syscall thread 必定已经解绑。
    assert(co->thread_id != p->thread_id);

    co->thread_id = 0;
    co->status = CO_STATUS_RUNNABLE;
    linked_push(p->runnable_list, co);

    // 让出当前线程控制权即可, processor_run 会做后续的处理
    aco_yield();
}

bool processor_own(processor_t* p) {
    return uv_thread_self() == p->thread_id;
}

coroutine_t* coroutine_new(void* fn, n_vec_t* args, bool solo, bool main) {
    coroutine_t* co = NEW(coroutine_t);
    co->fn = fn;
    co->solo = solo;
    co->status = CO_STATUS_RUNNABLE;
    co->args = args;
    co->aco = NULL;
    co->p = NULL;
    co->result = NULL;
    co->thread_id = 0;
    co->main = main;

    return co;
}

processor_t* processor_new() {
    processor_t* p = NEW(processor_t);

    uv_loop_t* loop = NEW(uv_loop_t);
    uv_loop_init(loop);
    p->uv_loop = loop;
    //    p->main_aco = aco_create(NULL, NULL, 0, NULL, NULL);
    p->thread_id = 0;
    //    p->errort = n_errort_new(string_new("", 0), 0);
    p->coroutine = NULL;
    p->co_started_at = 0;
    p->mcache.flush_gen = 0; // 线程维度缓存，避免内存分配锁
    p->runnable_list = linked_new();
    p->co_list = linked_new();

    return p;
}

void thread_handle_sigurg(int sig) {
    DEBUGF("[runtime.thread_handle_sigurg] sig=%d, thread_id=%lu", sig, (uint64_t)uv_thread_self());

    // 由于是被抢占，所以当前 coroutine 可以直接继续运行，而不需要等待什么 io 就绪
    coroutine_yield_with_status(CO_STATUS_RUNNABLE);

    DEBUGF("[runtime.thread_handle_sigurg] aco_resume, coroutine=%p", coroutine_get());
}

void coroutine_yield_with_status(co_status_t status) {
    processor_t* p = processor_get();
    assert(p);

    // 读取当前线程正在运行的 coroutine
    coroutine_t* co = coroutine_get();
    assert(co);

    // 如果是抢占式调度 status 应该是 runnable, 表示 coroutine 可以被立即调度
    // 如果是等待网络 io status 应该是 waiting 表示等待 io 事件就绪
    co->status = status;

    if (status == CO_STATUS_RUNNABLE) {
        linked_push(p->runnable_list, co);
    }

    aco_yield1(co->aco);
}

/**
 * @return
 */
bool processor_get_exit() {
    return processor_need_exit;
}
void processor_set_exit() {
    processor_need_exit = true;
}

void processor_free(processor_t* p) {
    linked_free(p->runnable_list);
    linked_free(p->co_list);
    aco_destroy(p->main_aco);
    free(p);
}
