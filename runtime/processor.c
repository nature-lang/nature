#include "processor.h"
#include "basic.h"

#include <uv.h>

int cpu_count;
processor_t *processor_list;

/**
 * 阻塞特定时间的网络 io 时间, 如果有 io 事件就绪则立即返回
 * uv_run 有三种模式
 * UV_RUN_DEFAULT: 持续阻塞不返回
 * UV_RUN_ONCE: 处理至少活跃的 fd 后返回, 如果没有活跃的 fd 则一直阻塞 (基于此 + uv_timer 实现延迟阻塞)
 * UV_RUN_NOWAIT: 不阻塞, 如果没有事件就绪则立即返回
 * @param timeout_ms
 * @return
 */
static void *io_run(uint64_t timeout_ms) {

}

/**
 * 检测 coroutine 当前是否需要单独线程调度，如果不需要单独线程则直接在当前线程进行 aco_resume
 */
static void coroutine_run(processor_t *p, coroutine_t *c) {

}

// handle by thread
static void *processor_run(void *arg) {
    int i = *(int *) arg;
    free(arg);

    processor_t *p = &processor_list[i];

    // 对 p 进行调度处理(p 上面可能还没有 coroutine)
    while (true) {
        // - 处理 io 就绪事件(也就是 run 指定时间的 libuv)
        io_run(100);

        // - 处理 coroutine (找到 io 可用的 goroutine)
        LINKED_FOR(p->runnable_list) {
            coroutine_t *co = LINKED_VALUE();
            assertf(co->status == CO_STATUS_RUNNABLE, "coroutine status must be runnable");
            coroutine_run(p, co);
        }

        if (need_stw()) {
            p->safe_point = true;
            while (need_stw()) {
                usleep(50); // 每 50ms 检测一次 STW 是否解除
            }
        }

        // TODO 判断安全退出
    }
}

void processor_init() {
    // - 读取当前 cpu 线程数初始化相应数量的 p
    uv_cpu_info_t *info;
    uv_cpu_info(NULL, &cpu_count);
    uv_free_cpu_info(NULL, cpu_count);

    DEBUGF("[runtime.processor_init] cpu_count=%d", cpu_count);

    // - 为每一个 processor 创建对应的 thread 进行处理对应的 p
    processor_list = mallocz(cpu_count * sizeof(processor_t));
    for (int i = 0; i < cpu_count; ++i) {
        processor_t p = processor_list[i];
        p.thread = NEW(pthread_t);

        int *arg = NEW(int);
        *arg = i;

        if (pthread_create(p.thread, NULL, processor_run, arg) != 0) {
            assertf(false, "pthread_create failed %s", strerror(errno));
        }
        DEBUGF("[runtime.processor_init] processor create, index=%d, thread=%p", i, p.thread);
    }

    //    processor_list = mallocz(1 * sizeof(processor_t));
    //    processor_count = 1;
    //    processor_main_init(&processor_list[0]);
}

processor_t *processor_get() {
    //    assertf(processor_count > 0, "processor not init");
    processor_t *result = &processor_list[0];
    return result;
}

void rt_processor_attach_errort(char *msg) {
    DEBUGF("[runtime.rt_processor_attach_errort] msg=%s", msg);
    processor_t *p = processor_get();
    n_errort *errort = n_errort_new(string_new(msg, strlen(msg)), 1);
    p->errort = errort;
}

void processor_dump_errort(n_errort *errort) {
    DEBUGF("[runtime.processor_dump_errort] errort base=%p", errort)
    n_string_t *msg = errort->msg;
    DEBUGF("[runtime.processor_dump_errort] memory_string len: %lu, base: %p", msg->length, msg->data);
    assert(errort->traces->length > 0);

    n_trace_t first_trace = {};
    vec_access(errort->traces, 0, &first_trace);
    char *dump_msg = dsprintf("catch error: '%s' at %s:%d:%d\n", (char *) errort->msg->data,
                              (char *) first_trace.path->data, first_trace.line, first_trace.column);

    VOID write(STDOUT_FILENO, dump_msg, strlen(dump_msg));

    if (errort->traces->length > 1) {
        char *temp = "stack backtrace:\n";
        VOID write(STDOUT_FILENO, temp, strlen(temp));
        for (int i = 0; i < errort->traces->length; ++i) {
            n_trace_t trace = {};
            vec_access(errort->traces, i, &trace);
            temp = dsprintf("%d:\t%s\n\t\tat %s:%d:%d\n", i, (char *) trace.ident->data, (char *) trace.path->data,
                            trace.line, trace.column);
            VOID write(STDOUT_FILENO, temp, strlen(temp));
        }
    }
}
