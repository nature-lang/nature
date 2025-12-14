#include "runtime/processor.h"

coroutine_t *rt_coroutine_async(void *fn, int64_t flag, n_future_t *fu) {
    coroutine_t *co = rt_coroutine_new(fn, flag, fu, NULL);
    rt_coroutine_dispatch(co);
    DEBUGF("[rt_coroutine_async] co=%p, fn=%p, flag=%ld, fu=%p, size=%ld", co, fn, flag, fu, fu->size);

    return co;
}

void rt_coroutine_async2(void *fn, int64_t flag, bool is_direct) {
    if (is_direct) {
        flag |= FLAG(CO_FLAG_DIRECT);
    }
    coroutine_t *co = rt_coroutine_new(fn, flag, NULL, NULL);
    rt_coroutine_dispatch(co);
}

void rt_coroutine_yield() {
    n_processor_t *p = processor_get();
    co_yield_runnable(p, p->coroutine);
}

void rt_select_block() {

    co_yield_waiting(coroutine_get(), NULL, NULL);
}


void *rt_coroutine_arg() {
    coroutine_t *co = coroutine_get();
    return co->arg;
}

static void uv_timer_close_cb(uv_handle_t *handle) {
    free(handle);
}

/**
 * repeat 为 0， 所以不会重复执行，所以不需要手动钓孙 uv_stop_timer 停止计时器
 * @param timer
 */
static void sleep_timer_cb(uv_timer_t *timer) {
    DEBUGF("[sleep_timer_cb] callback start, timer=%p, timer->data=%p", timer, timer->data);
    coroutine_t *co = timer->data;

    // - 标记 coroutine 并推送到可调度队列中等待 processor handle
    n_processor_t *p = co->p;
    assert(p);


    // timer 到时间了, push 到尾部等待调度
    co_ready(co);
    DEBUGF("[sleep_timer_cb] will stop and clear timer=%p, p_index=%d, co=%p, status=%d", timer,
           p->index, co, co->status);

    uv_timer_stop(timer);

    // 注册 close 事件而不是瞬时 close!
    uv_close((uv_handle_t *) timer, uv_timer_close_cb);

    DEBUGF("[sleep_timer_cb] success stop and clear timer=%p, p_index=%d, co=%p, status=%d", timer,
            p->index, co, co->status);
}

static inline void uv_async_sleep_register(coroutine_t *co, int64_t ms) {
    uv_timer_t *timer = NEW(uv_timer_t);
    uv_timer_init(&global_loop, timer);
    timer->data = co;

    uv_timer_start(timer, sleep_timer_cb, ms, 0);

    DEBUGF("[runtime.rt_coroutine_sleep] start, co=%p, p_index=%d, timer=%p, timer_value=%lu",
           co, p->index, &timer, fetch_addr_value((addr_t) &timer));
}

void rt_coroutine_sleep(int64_t ms) {
    coroutine_t *co = coroutine_get();

    DEBUGF("[runtime.rt_coroutine_sleep] start, co=%p p_index=%d, timer=%p, timer_value=%lu", co,
           p->index, &timer, fetch_addr_value((addr_t) &timer));

    global_waiting_send(uv_async_sleep_register, co, (void *) ms, 0);

    DEBUGF(
            "[runtime.rt_coroutine_sleep] coroutine sleep resume, co=%p, co_status=%d, p_index=%d, timer=%p",
            co, co->status, p->index, &timer);
}


int64_t rt_processor_index() {
    coroutine_t *co = coroutine_get();
    return co->p->index;
}
