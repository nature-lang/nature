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
    coroutine_yield_with_status(CO_STATUS_RUNNABLE);
}

/**
 * repeat 为 0， 所以不会重复执行，所以不需要手动钓孙 uv_stop_timer 停止计时器
 * @param timer
 */
static void uv_on_timer(uv_timer_t *timer) {
    DEBUGF("[runtime.coroutine_on_timer] start, timer=%p, timer->data=%p", timer, timer->data);
    coroutine_t *c = timer->data;

    // - 标记 coroutine 并推送到可调度队列中等待 processor handle
    c->status = CO_STATUS_RUNNABLE;
    processor_t *p = c->p;
    assert(p);

    DEBUGF("[runtime.coroutine_on_timer] will push to runnable_list, p=%p, c=%d", p, c->status)

    linked_push(p->runnable_list, c);
}

void coroutine_sleep(int64_t ms) {
    processor_t *p = processor_get();
    coroutine_t *c = coroutine_get();

    // - 初始化 libuv 定时器
    uv_timer_t timer;
    uv_timer_init(p->uv_loop, &timer);

    timer.data = c;

    // 设定定时器超时时间与回调
    uv_timer_start(&timer, uv_on_timer, ms, 0);

    // 退出等待 io 事件就绪
    coroutine_yield_with_status(CO_STATUS_WAITING);

    DEBUGF("coroutine sleep completed");
}