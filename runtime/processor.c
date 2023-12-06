#include "processor.h"

#include <uv.h>

#include "basic.h"
#include "utils/assertf.h"

int cpu_count;
slice_t* share_processor_list; // 共享协程列表的数量一般就等于线程数量
slice_t* solo_processor_list;  // 独享协程列表其实就是多线程

uv_key_t local_processor_key;

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
void coroutine_run(processor_t* p, coroutine_t* co) {
    assertf(co->aco, "coroutine aco not init");
    assertf(co->status == CO_STATUS_RUNNABLE, "coroutine status must be runnable");

    co->status = CO_STATUS_RUNNING;
    p->coroutine = co;

    // 将 RIP 指针移动用户代码片段中
    DEBUGF("[runtime.coroutine_run] aco_resume will start, co=%p, aco=%p", co, co->aco);
    aco_resume(co->aco);
    DEBUGF("[runtime.coroutine_run] aco_yield completed, co=%p, aco=%p", co, co->aco);

    if (co->status != CO_STATUS_DEAD) {
        co->status = CO_STATUS_WAITING;
    }
}

void io_init(processor_t* p) {
    uv_loop_t* loop = NEW(uv_loop_t);
    uv_loop_init(loop);

    p->uv_loop = loop;
}

// handle by thread
void processor_run(processor_t* p) {
    DEBUGF("[runtime.share_process_run] processor=%p", p);

    // 初始化事件循环
    io_init(p);

    // 将 p 存储在线程维度全局遍历中，方便直接在 coroutine 运行中读取相关的 processor
    uv_key_set(&local_processor_key, p);

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(p, 5);

        // - 处理 coroutine (找到 io 可用的 goroutine)
        LINKED_FOR(p->runnable_list) {
            coroutine_t* co = LINKED_VALUE();
            assertf(co->status == CO_STATUS_RUNNABLE, "coroutine status must be runnable");
            coroutine_run(p, co);
        }

        if (processor_need_stw()) {
            p->safe_point = true;
            while (processor_need_stw()) {
                usleep(50); // 每 50ms 检测一次 STW 是否解除
            }
        }

        if (processor_need_stop()) {
            DEBUGF("[runtime.share_process_run] processor=%p, need stop", p);
            return;
        }
    }
}

void coroutine_dispatch(coroutine_t* co) {
    if (co->solo) {
        processor_t* p = NEW(processor_t);
        slice_push(solo_processor_list, p);

        p->thread = NEW(pthread_t);
        if (pthread_create(p->thread, NULL, processor_run, p) != 0) {
            assertf(false, "pthread_create failed %s", strerror(errno));
        }

        DEBUGF("[runtime.coroutine_dispatch] solo processor create, thread=%p", p->thread);
        return;
    }

    // 遍历 shared_processor_list 找到 co_list->count 最小的 processor
    processor_t* min_p = NULL;
    SLICE_FOR(share_processor_list) {
        processor_t* p = SLICE_VALUE(share_processor_list);
        if (p->co_list->count < min_p->co_list->count) {
            min_p = p;
        }
    }

    assert(min_p);

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
    uv_key_create(&local_processor_key);

    DEBUGF("[runtime.processor_init] cpu_count=%d", cpu_count);

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    share_processor_list = slice_capacity_new(cpu_count);
    solo_processor_list = slice_new();

    for (int i = 0; i < cpu_count; ++i) {
        processor_t* p = NEW(processor_t);
        slice_push(share_processor_list, p);

        // 创建一个新的线程用来处理
        p->thread = NEW(pthread_t);

        if (pthread_create(p->thread, NULL, processor_run, p) != 0) {
            assertf(false, "pthread_create failed %s", strerror(errno));
        }

        DEBUGF("[runtime.processor_init] processor create, index=%d, thread=%p", i, p->thread);
    }
}

processor_t* processor_get() {
    processor_t* p = uv_key_get(&local_processor_key);
    return p;
}

void rt_processor_attach_errort(char* msg) {
    DEBUGF("[runtime.rt_processor_attach_errort] msg=%s", msg);
    processor_t* p = processor_get();
    n_errort* errort = n_errort_new(string_new(msg, strlen(msg)), 1);
    p->errort = errort;
}

void processor_dump_errort(n_errort* errort) {
    DEBUGF("[runtime.processor_dump_errort] errort base=%p", errort)
    n_string_t* msg = errort->msg;
    DEBUGF("[runtime.processor_dump_errort] memory_string len: %lu, base: %p", msg->length, msg->data);
    assert(errort->traces->length > 0);

    n_trace_t first_trace = {};
    vec_access(errort->traces, 0, &first_trace);
    char* dump_msg = dsprintf("catch error: '%s' at %s:%d:%d\n", (char*)errort->msg->data,
                              (char*)first_trace.path->data, first_trace.line, first_trace.column);

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (errort->traces->length > 1) {
        char* temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < errort->traces->length; ++i) {
            n_trace_t trace = {};
            vec_access(errort->traces, i, &trace);
            temp = dsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char*)trace.ident->data, (char*)trace.path->data,
                            trace.line, trace.column);
            VOID write(STDOUT_FILENO, temp, strlen(temp));
        }
    }
}
