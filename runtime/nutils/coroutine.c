#include "coroutine.h"

#include "runtime/processor.h"

coroutine_t *coroutine_create(void *fn, n_vec_t *args, uint64_t flag) {
    bool solo = FLAG(CO_FLAG_SOLO) & flag;
    return coroutine_new(fn, args, solo, false);
}

coroutine_t *coroutine_run(void *fn, n_vec_t *args, uint64_t flag) {
    coroutine_t *co = coroutine_create(fn, args, flag);

    coroutine_dispatch(co);

    return co;
}

void coroutine_yield() {
    processor_t *p = processor_get();
    co_yield_runnable(p, p->coroutine);
}

static void uv_timer_close_cb(uv_handle_t *handle) {
    free(handle);
}

/**
 * repeat 为 0， 所以不会重复执行，所以不需要手动钓孙 uv_stop_timer 停止计时器
 * @param timer
 */
static void uv_on_timer(uv_timer_t *timer) {
    RDEBUGF("[uv_on_timer] start, timer=%p, timer->data=%p", timer, timer->data);
    coroutine_t *co = timer->data;

    // - 标记 coroutine 并推送到可调度队列中等待 processor handle
    processor_t *p = co->p;
    assert(p);

    RDEBUGF("[uv_on_timer] will push to runnable_list, p_index_%d=%d, c=%d", p->share, p->index, co->status);

    // timer 到时间了, push 到尾部等待调度
    co->status = CO_STATUS_RUNNABLE;
    mutex_lock(&p->co_locker);
    rt_linked_push(&p->runnable_list, co);
    mutex_unlock(&p->co_locker);

    RDEBUGF("[uv_on_timer] will stop and clear timer=%p, p_index_%d=%d", timer, p->share, p->index);

    uv_timer_stop(timer);

    // 注册 close 事件而不是瞬时 close!
    uv_close((uv_handle_t *)timer, uv_timer_close_cb);

    RDEBUGF("[uv_on_timer] success stop and clear timer=%p, p_index_%d=%d", timer, p->share, p->index);
}

void coroutine_sleep(int64_t ms) {
    processor_t *p = processor_get();
    assert(p);
    coroutine_t *co = coroutine_get();
    assert(co);

    // - 初始化 libuv 定时器(io_run 回调会读取 timer 的地址，所以需要在堆中分配)
    uv_timer_t *timer = NEW(uv_timer_t);
    uv_timer_init(&p->uv_loop, timer);
    timer->data = co;

    // 设定定时器超时时间与回调
    uv_timer_start(timer, uv_on_timer, ms, 0);

    TDEBUGF("[runtime.coroutine_sleep] start, co=%p uv_loop=%p, p_index_%d=%d, timer=%p, timer_value=%lu", co, &p->uv_loop, p->share,
            p->index, &timer, fetch_addr_value((addr_t)&timer));

    // 退出等待 io 事件就绪
    co_yield_waiting(p, co);

    TDEBUGF("[runtime.coroutine_sleep] coroutine sleep completed resume, co=%p uv_loop=%p, p_index_%d=%d, timer=%p, timer_value=%lu", co,
            &p->uv_loop, p->share, p->index, &timer, fetch_addr_value((addr_t)&timer));
}