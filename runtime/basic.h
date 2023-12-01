#ifndef NATURE_RUNTIME_BASIC_H
#define NATURE_RUNTIME_BASIC_H

#include <pthread.h>

#include "aco/aco.h"
#include "utils/linked.h"
#include "utils/type.h"

typedef enum {
    CO_STATUS_SYSCALL,  // 陷入系统调用
    CO_STATUS_TEMPCALL, // 陷入 temp call
    CO_STATUS_RUNNABLE, // 允许被调度
    CO_STATUS_RUNNING,  // 正在运行
    CO_STATUS_WAITING,  // 等待 IO 事件就绪
    CO_STATUS_DEAD,     // 死亡状态
} co_status_t;

typedef struct {
    co_status_t status;
    bool exclusive; // 独享线程(独享但是不阻塞)
    aco_t* aco;
} coroutine_t;

/**
 * 位于 share_processor_t 中的协程，如果运行时间过长会被抢占式调度
 * 共享处理器的数量通畅等于线程的数量, 所以可以将线程维度的无锁内存分配器放置再这里
 */
typedef struct {
    void* mcache;            // 线程维度无锁内存分配器
    n_errort* error;         // 当前线程
    pthread_t* thread;       // pthread 线程
    coroutine_t* coroutine;  // 当前正在调度的 coroutine
    uint64_t started_at;     // 协程调度开始时间
    linked_t* co_list;       // 当前 processor 下的 coroutine 列表
    linked_t* runnable_list; // 当 io 时间就绪后会移动到 runnable_list 等待调度
    bool safe_point;         // 当前是否处于安全点
} share_processor_t;         // 通用共享协程处理器

// 独享 processor 比较简单, 因为只需要管理一个 coroutine
// 另外当 coroutine.status 状态为 syscall 时可以直接进入到安全点
typedef struct {
    pthread_t* thread; // 绑定的 pthread 线程
    n_errort* error; // 当前处理器中的全局错误
    coroutine_t* coroutine; // 当前正在处理的 coroutine
    bool safe_point; // 当前是否处于安全点
} solo_processor_t; // 独享协程处理器

extern int cpu_count;
extern linked_t* share_processor_list; // 贡献协程列表，数量一般就等于线程数量
extern linked_t* solo_processor_list;

bool need_stw();

#endif //NATURE_BASIC_H
