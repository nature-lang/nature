#ifndef NATURE_RUNTIME_BASIC_H
#define NATURE_RUNTIME_BASIC_H

#include "aco/aco.h"
#include "utils/type.h"


typedef enum {
    CO_STATUS_SYSCALL, // 陷入系统调用
    CO_STATUS_RUNNABLE, // 允许被调度
    CO_STATUS_RUNNING, // 正在运行
    CO_STATUS_WAITING, // 等待 IO 事件就绪
    CO_STATUS_DEAD, // 死亡状态
} co_status_t;

typedef struct {
    co_status_t status;
    aco_t aco;
} coroutine_t;

/**
 * linux 线程由自己的系统栈，多个线程就有多个 system stack
 * 由于进入到 runtime 的大多数情况中都需要切换栈区，所以必须知道当前线程的栈区
 * 如果 processor 是第一个线程，那么其 system_stack 和 linux main stack 相同,
 * copy linux stack 时并不需要精准的范围，base 地址直接使用当前 rsp, end 地址使用自定义大小比如 64kb 就行了
 * 关键是切换到 user stack 时记录下 rsp 和 rbp pointer
 */
typedef struct processor_t {
//    mmode_t temp_mode;
//    mmode_t user_mode;
//    mmode_t system_mode;
    void *mcache; // mcache_t
    n_errort *errort;
    pthread_t *thread; // 当前绑定的 thread
    coroutine_t *coroutine; // 当前正在执行的 coroutine
    linked_t *co_list; // 当前 processor 中的 coroutine 列表
    linked_t *runnable_list; // 当前 processor 中的 coroutine 列表
    bool safe_point;
} processor_t;

extern int cpu_count;
extern processor_t *processor_list;

bool need_stw();

#endif //NATURE_BASIC_H
