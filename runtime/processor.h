#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include <stdint.h>
#include <uv.h>

#include "nutils/errort.h"
#include "nutils/vec.h"
#include "runtime.h"

extern int cpu_count;
extern slice_t *share_processor_list; // 共享协程列表的数量一般就等于线程数量
extern slice_t *solo_processor_list;  // 独享协程列表其实就是多线程
extern uv_key_t tls_processor_key;
extern uv_key_t tls_coroutine_key;

extern bool processor_need_stw;  // 全局 STW 标识
extern bool processor_need_exit; // 全局 STW 标识

void thread_handle_sigurg(int sig);

// 调度器每一次处理都会先调用 need_stw 判断是否存在 STW 需求
bool processor_get_stw();

void processor_stop_the_world();

void processor_start_the_world();

bool processor_all_safe();

void processor_wait_all_safe();

void processor_wait_all_gc_work_finish();

/**
 *  processor 停止调度
 */
bool processor_get_exit();

void processor_set_exit();

/**
 * 判断 p->thread_id 是否和当前线程的 id 相同
 * @return
 */
bool processor_own(processor_t *p);

/**
 * 阻塞特定时间的网络 io 时间, 如果有 io 事件就绪则立即返回
 * uv_run 有三种模式
 * UV_RUN_DEFAULT: 持续阻塞不返回, 可以使用 uv_stop 安全退出
 * UV_RUN_ONCE: 处理至少活跃的 fd 后返回, 如果没有活跃的 fd 则一直阻塞
 * UV_RUN_NOWAIT: 不阻塞, 如果没有事件就绪则立即返回
 * @param timeout_ms
 * @return
 */
int io_run(processor_t *p, uint64_t timeout_ms);

/**
 * 一个阻塞的循环调度器，不停的从当前 share_processor 中读取 runnable_list 进行处理，处理完成后通过 io_run 阻塞一段时间
 * 每一次循环结束会调用 need_stw 判断是否需要 STW
 *
 * 处理 coroutine 可能会存在阻塞的情况, 1. 阻塞系统调用 2. tempcall 3. for 循环处理
 */
void processor_run(void *p);

/**
 * runtime_main 会负责调用该方法，该方法读取 cpu 的核心数，然后初始化对应数量的 share_processor
 * 每个 share_processor 可以通过 void* arg 直接找到自己的 share_processor_t 对象
 */
void processor_init();

processor_t *processor_new();

void processor_free(processor_t *);

/**
 * @param fn
 * @param args 元素的类型是 n_union_t 联合类型
 * @param solo
 * @return
 */
coroutine_t *coroutine_new(void *fn, n_vec_t *args, bool solo, bool main);

/**
 * 为 coroutine 选择合适的 processor 绑定，如果是独享 coroutine 则创建一个 solo processor
 */
void coroutine_dispatch(coroutine_t *co);

/**
 * 有 processor_run 调用
 * coroutine 的本质是一个指针，指向了需要执行的代码的 IP 地址。 (aco_create 会绑定对应的 fn)
 */
void coroutine_resume(processor_t *p, coroutine_t *co);

void coroutine_yield_with_status(co_status_t status);

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

coroutine_t *coroutine_get();

void pre_block_syscall();

void post_block_syscall();

#endif // NATURE_PROCESSOR_H
