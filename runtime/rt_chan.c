#include "rt_chan.h"

static bool buf_empty(n_chan_t *ch) {
    return ch->buf_front == ch->buf_rear;
}


static bool buf_full(n_chan_t *ch) {
    if (ch->buf->length == 0) {
        return true;
    }
    return ((ch->buf_rear + 1) % ch->buf->length) == ch->buf_front;
}

/**
 * 从队列头部取出元素
 */
static void buf_pop(n_chan_t *ch, void *msg_ptr) {
    assert(!buf_empty(ch));

    rt_vec_access(ch->buf, ch->buf_front, msg_ptr);

    ch->buf_front = (ch->buf_front + 1) % ch->buf->capacity;
}


/**
 * 获取 可以 push 的 element ptr
 */
static void *buf_push_element_addr(n_chan_t *ch) {
    assert(!buf_full(ch));

    void *ele_addr = (void *) rt_vec_element_addr(ch->buf, ch->buf_rear);
    ch->buf_rear = (ch->buf_rear + 1) % ch->buf->length;

    return ele_addr;
}


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
 * pull(true) stack_ptr <- msg_ptr
 * push(false) stack_ptr -> msg_ptr
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

n_chan_t *rt_chan_new(int64_t rhash, int64_t ele_rhash, int64_t buf_len) {
    assertf(rhash > 0, "rhash must be a valid hash");
    assertf(ele_rhash > 0, "ele_rhash must be a valid hash");

    rtype_t *rtype = rt_find_rtype(rhash);
    assert(rtype && "cannot find rtype with hash");
    rtype_t *element_rtype = rt_find_rtype(ele_rhash);
    assert(element_rtype && "cannot find element_rtype with hash");

    n_chan_t *chan = rti_gc_malloc(rtype->size, rtype);
    chan->msg_size = rtype_stack_size(element_rtype, POINTER_SIZE);
    pthread_mutex_init(&chan->lock, NULL);
    // ele_rhash
    chan->buf = rti_vec_new(element_rtype, buf_len, buf_len);
    return chan;
}

/**
 * msg 中存储了 share_stack 的栈地址
 * @param chan
 * @param msg_ptr
 */
void rt_chan_send(n_chan_t *chan, void *msg_ptr) {
    pthread_mutex_lock(&chan->lock);

    if (!waitq_empty(&chan->recvq)) {
        assert(buf_empty(chan)); // 存在多余的 recv queue 时, buf 则必须是空的

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
    } else if (!buf_full(chan)) {
        // 直接将 msg push 到 chan 中, 不阻塞当前 buf
        void *dst_ptr = buf_push_element_addr(chan);
        memmove(dst_ptr, msg_ptr, chan->msg_size);

        pthread_mutex_unlock(&chan->lock);
        return;
    } else {
        assert(waitq_empty(&chan->recvq));

        // 直接将自身 yield 等待唤醒
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

void rt_chan_recv(n_chan_t *chan, void *msg_ptr) {
    pthread_mutex_lock(&chan->lock);

    if (!waitq_empty(&chan->sendq)) {
        linkco_t *linkco = waitq_pop(&chan->sendq);
        if (buf_empty(chan)) {
            // 没有缓冲区或者缓冲区中不存在数据
            DEBUGF("[rt_chan_recv] sendq have, buf empty,  will direct wakeup")
            rt_msg_transmit(linkco->co, linkco->data, msg_ptr, false, chan->msg_size);
        } else {
            assert(buf_full(chan));

            // 直接将缓冲区中的数据 pop 到 msg 中，然后将 sendq 中的数据 push 到尾部。
            buf_pop(chan, msg_ptr);

            void *dst_ptr = buf_push_element_addr(chan);
            rt_msg_transmit(linkco->co, linkco->data, dst_ptr, false, chan->msg_size);
        }

        // 将取出的 send co 放入到 runnable 进行激活
        processor_t *p = linkco->co->p;
        co_set_status(p, linkco->co, CO_STATUS_RUNNABLE);
        rt_linked_fixalloc_push(&p->runnable_list, linkco->co);

        rti_release_linkco(linkco);
        pthread_mutex_unlock(&chan->lock);
        return;
    } else if (!buf_empty(chan)) {
        assert(waitq_empty(&chan->sendq));
        buf_pop(chan, msg_ptr);

        pthread_mutex_unlock(&chan->lock);
        return;
    } else {
        DEBUGF("[rt_chan_recv] sendq empty,  will yield to waiting")
        coroutine_t *co = coroutine_get();

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
