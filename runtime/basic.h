#ifndef NATURE_RUNTIME_BASIC_H
#define NATURE_RUNTIME_BASIC_H

#include <pthread.h>

#include "aco/aco.h"
#include "utils/linked.h"
#include "utils/type.h"

typedef enum {
    CO_STATUS_SYSCALL,  // 陷入系统调用
    CO_STATUS_RUNNABLE, // 允许被调度
    CO_STATUS_RUNNING,  // 正在运行
    CO_STATUS_WAITING,  // 等待 IO 事件就绪
    CO_STATUS_DEAD,     // 死亡状态
} co_status_t;

typedef struct processor_t processor_t;

typedef struct coroutine_t {
    co_status_t status;
    bool solo; // 当前协程需要独享线程
    aco_t* aco;
    processor_t* p; // 当前 coroutine 绑定的 p
    void* result;   // coroutine 如果存在返回值，相关的值会放在 result 中
    // 默认为 0， 只有当 coroutine 独占整个线程时才会存在 thread_id
    // 1. solo coroutine 2. coroutine in block syscall
    uv_thread_t thread_id;
} coroutine_t;

/**
 * 位于 share_processor_t 中的协程，如果运行时间过长会被抢占式调度
 * 共享处理器的数量通畅等于线程的数量, 所以可以将线程维度的无锁内存分配器放置再这里
 */
struct processor_t {
    void* mcache;            // 线程维度无锁内存分配器
    uv_loop_t* uv_loop;      // uv loop 事件循环
    n_errort* errort;        // 当前线程
    uv_thread_t thread_id;   // 当前 processor 绑定的 pthread 线程
    coroutine_t* coroutine;  // 当前正在调度的 coroutine
    uint64_t started_at;     // 协程调度开始时间
    linked_t* co_list;       // 当前 processor 下的 coroutine 列表
    linked_t* runnable_list; // 当 io 时间就绪后会移动到 runnable_list 等待调度
    bool share;
    bool safe_point; // 当前是否处于安全点
};

#endif // NATURE_BASIC_H
