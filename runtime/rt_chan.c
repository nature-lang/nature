#include "rt_chan.h"
#include "stdlib.h"
#include "stdint.h"
#include "rt_mutex.h"
#include "processor.h"

typedef struct {
    linkco_t *head;
    linkco_t *rear;
} waitq_t;

typedef struct {
    int64_t buf_cap;
    int64_t buf_len;
    void *buf;
    int64_t msg_size;
    waitq_t sendq;
    waitq_t recvq;

    pthread_mutex_t lock;
} chan_t;

static bool waitq_empty(waitq_t *waitq) {
    assert(waitq);
    return waitq->head == NULL;
}

static void waitq_push(waitq_t *waitq, linkco_t *linkco) {
    assert(waitq);

    assert(linkco->succ == NULL);
    assert(linkco->prev == NULL);

    if (waitq->head == NULL) {
        assert(waitq->rear == NULL);

        rt_write_barrier(&waitq->head, &linkco);
        rt_write_barrier(&waitq->rear, &linkco);
    } else {
        assert(waitq->rear);

        rt_write_barrier(&waitq->rear->succ, &linkco);
        rt_write_barrier(&linkco->prev, &waitq->rear);
        rt_write_barrier(&waitq->rear, &linkco);
    }
}

static linkco_t *waitq_pop(waitq_t *waitq) {
    assert(waitq);

    if (waitq->head == NULL) {
        assert(waitq->rear == NULL);
        return NULL;
    }

    assertf(waitq->head->co, "waitq head %p value empty", waitq->head);

    linkco_t *pop_linkco = waitq->head;
    linkco_t *null_co = NULL;

    rt_write_barrier(&waitq->head, &waitq->head->succ);

    if (waitq->head == NULL) {
        rt_write_barrier(&waitq->rear, &null_co);
    } else {
        rt_write_barrier(&waitq->head->prev, &null_co);
    }

    pop_linkco->prev = NULL;
    pop_linkco->succ = NULL;
    return pop_linkco;
}


/**
 * pull stack_ptr <- msg_ptr
 * push stack_ptr -> msg_ptr
 * share_stack_ptr + offset = stack_top(offset = 0, but it's max value)
 * @param co
 * @param stack_ptr
 * @return
 */
static void rt_msg_transmit(coroutine_t *co, void *stack_ptr, void *msg_ptr, bool pull, int64_t size) {
    // 计算基于 share_stack_ptr 得到的 offset(正数)
    aco_share_stack_t *share_stack = co->aco.share_stack;
    aco_save_stack_t *save_stack = &co->aco.save_stack;

    pthread_spin_lock(&share_stack->owner_lock);
    if (share_stack->owner != &co->aco) {
        // != 表示 share_stack 溢出到了 save_stack 中
        assert(save_stack->valid_sz > 0);
        assert(co->status == CO_STATUS_WAITING);
        addr_t offset = (addr_t) share_stack->align_retptr - (addr_t) stack_ptr;
        stack_ptr = (void *) (((addr_t) save_stack->ptr + save_stack->valid_sz) - offset);
    }

    if (pull) {
        memmove(stack_ptr, msg_ptr, size);
    } else {
        memmove(msg_ptr, stack_ptr, size);
    }

    pthread_spin_unlock(&share_stack->owner_lock);
}

/**
 * msg 中存储了 share_stack 的栈地址
 * @param chan
 * @param msg_ptr
 */
void rt_chan_send(chan_t *chan, void *msg_ptr) {
    pthread_mutex_lock(&chan->lock);

    if (!waitq_empty(&chan->recvq)) {
        DEBUGF("[rt_chan_send] recvq not empty,  will direct wakeup")

        linkco_t *linkco = waitq_pop(&chan->recvq);

        // linkco->data <- msg_ptr
        rt_msg_transmit(linkco->co, linkco->data, msg_ptr, true, chan->msg_size);

        // 一旦设置为 runnable 并 push 到 runnable_list 中，coroutine 将会立即运行。
        processor_t *p = linkco->co->p;
        co_set_status(p, linkco->co, CO_STATUS_RUNNABLE);
        rt_linked_fixalloc_push(&p->runnable_list, linkco->co);

        rti_release_linkco(linkco);

        pthread_mutex_unlock(&chan->lock);
        return;
    } else {
        DEBUGF("[rt_chan_send] recvq empty,  will yield to waiting")
        coroutine_t *co = coroutine_get();

        // recvq empty, 将自身 send 和 msg 地址存放在 sendq 中, 等待 recv 唤醒
        linkco_t *linkco = rti_acquire_linkco();

        linkco->co = co;
        linkco->data = msg_ptr;
        waitq_push(&chan->sendq, linkco);

        assert(co->yield_lock == NULL);
        co->yield_lock = &chan->lock;
        co_yield_waiting(co->p, co);

        // 数据已经取走，直接返回即可, coroutine_resume 后会将上面的 yield_lock 进行解锁
        DEBUGF("[rt_chan_send] co wakeup, will return, msg(int)=%ld",
               fetch_int_value((addr_t) msg_ptr, chan->msg_size))
        return;
    }
}

void rt_chan_recv(chan_t *chan, void *msg_ptr) {
    pthread_mutex_lock(&chan->lock);

    if (!waitq_empty(&chan->sendq)) {
        DEBUGF("[rt_chan_recv] sendq not empty,  will direct wakeup")
        linkco_t *linkco = waitq_pop(&chan->sendq);

        rt_msg_transmit(linkco->co, linkco->data, msg_ptr, false, chan->msg_size);


        // 将取出的 send co 放入到 runnable 进行准备激活
        processor_t *p = linkco->co->p;
        co_set_status(p, linkco->co, CO_STATUS_RUNNABLE);
        rt_linked_fixalloc_push(&p->runnable_list, linkco->co);

        rti_release_linkco(linkco);

        pthread_mutex_unlock(&chan->lock);
        return;
    } else {
        DEBUGF("[rt_chan_recv] sendq empty,  will yield to waiting")
        coroutine_t *co = coroutine_get();
//        void *save_stack_ptr = rt_share_to_save_stack_ptr(co, msg_ptr);

        // sendq empty, 将自身 recv 和 msg 地址存放在 recvq 中, 等待 send 唤醒
        linkco_t *linkco = rti_acquire_linkco();

        linkco->co = co;
        linkco->data = msg_ptr;
        waitq_push(&chan->recvq, linkco);

        assert(co->yield_lock == NULL);
        co->yield_lock = &chan->lock;
        co_yield_waiting(co->p, co);

        // 数据已经收到，直接返回即可, coroutine_resume 后会将上面的 yield_lock 进行解锁
        DEBUGF("[rt_chan_recv] co wakeup, will return, msg(int)=%ld",
               fetch_int_value((addr_t) msg_ptr, chan->msg_size))
        return;
    }
}
