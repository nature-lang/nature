#include "rt_chan.h"

#include <stdatomic.h>

typedef struct {
    n_chan_t *chan;
    void *msg_ptr;
} scase;

static void selunlock(scase *cases, int16_t *lockorder, int16_t norder);

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

    if (msg_ptr) {
        rti_vec_access(ch->buf, ch->buf_front, msg_ptr);
    }

    ch->buf_front = (ch->buf_front + 1) % ch->buf->capacity;
}

/**
 * 获取 可以 push 的 element ptr
 */
static void *buf_next_ref(n_chan_t *ch) {
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

    assert(linkco->next == NULL);
    assert(linkco->prev == NULL);

    if (waitq->head == NULL) {
        assert(waitq->rear == NULL);

        rti_write_barrier_ptr(&waitq->head, linkco, false);
        rti_write_barrier_ptr(&waitq->rear, linkco, false);
    } else {
        assert(waitq->rear);

        rti_write_barrier_ptr(&waitq->rear->next, linkco, false);
        rti_write_barrier_ptr(&linkco->prev, waitq->rear, false);
        rti_write_barrier_ptr(&waitq->rear, linkco, false);
    }
}

static void waitq_remove(waitq_t *waitq, linkco_t *lc) {
    linkco_t *x = lc->prev;
    linkco_t *y = lc->next;
    if (x != NULL) {
        if (y != NULL) {
            x->next = y;
            y->prev = x;
            lc->next = NULL;
            lc->prev = NULL;
            return;
        }
        // end of queue
        x->next = NULL;
        waitq->rear = x;
        lc->prev = NULL;
        return;
    }

    if (y != NULL) {
        // head of queue
        y->prev = NULL;
        waitq->head = y;
        lc->next = NULL;
        return;
    }

    // x==y==nil. Either sgp is the only element in the queue,
    // or it has already been removed. Use q.first to disambiguate.
    if (waitq->head == lc) {
        waitq->head = NULL;
        waitq->rear = NULL;
    }
}

static linkco_t *waitq_pop(waitq_t *waitq) {
    assert(waitq);

    while (true) {
        if (waitq->head == NULL) {
            assert(waitq->rear == NULL);
            return NULL;
        }

        assertf(waitq->head->co, "waitq head %p value empty", waitq->head);

        linkco_t *pop_linkco = waitq->head;
        rti_write_barrier_ptr(&waitq->head, waitq->head->next, false);

        if (waitq->head == NULL) {
            rti_write_barrier_ptr(&waitq->rear, NULL, false);
        } else {
            rti_write_barrier_ptr(&waitq->head->prev, NULL, false);
        }

        pop_linkco->prev = NULL;
        pop_linkco->next = NULL;

        // register by select, need to check select_done
        // 该操作确保多个 linkco 对应的同一个 co 只能被唤醒一次
        if (pop_linkco->is_select) {
            int32_t expected = 0;
            if (!atomic_compare_exchange_strong(&pop_linkco->co->select_done, &expected, 1)) {
                continue;
            }
        }

        return pop_linkco;
    }
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

#ifdef __LINUX
    pthread_spin_lock(&share_stack->owner_lock);
#else
    pthread_mutex_lock(&share_stack->owner_lock);
#endif

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
#ifdef __LINUX
    pthread_spin_unlock(&share_stack->owner_lock);
#else
    pthread_mutex_unlock(&share_stack->owner_lock);
#endif
}

static bool chan_yield_commit(coroutine_t *co, void *chan_lock) {
    pthread_mutex_unlock(chan_lock);
    return true;
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

static void rt_send(n_chan_t *chan, linkco_t *linkco, void *msg_ptr, scase *cases, int16_t *lockorder, int16_t norder) {
    coroutine_t *co = linkco->co;
    assert(co);
    co->data = linkco;
    linkco->success = true;

    if (linkco->data) {
        rt_msg_transmit(linkco->co, linkco->data, msg_ptr, true, chan->msg_size);
        linkco->data = NULL;
    }

    if (cases) {
        selunlock(cases, lockorder, norder);
    } else {
        pthread_mutex_unlock(&chan->lock);
    }
    co_ready(co);
}

/**
 * msg 中存储了 share_stack 的栈地址
 * @param chan
 * @param msg_ptr
 */
bool rt_chan_send(n_chan_t *chan, void *msg_ptr, bool try) {
    pthread_mutex_lock(&chan->lock);

    if (chan->closed) {
        rti_throw("send on closed channel", false);
        pthread_mutex_unlock(&chan->lock);
        return false;
    }

    linkco_t *linkco = waitq_pop(&chan->recvq);
    if (linkco) {
        rt_send(chan, linkco, msg_ptr, NULL, NULL, 0);
        return true;
    }

    if (!buf_full(chan)) {
        // 直接将 msg push 到 chan 中, 不阻塞当前 buf
        void *dst_ptr = buf_next_ref(chan);

        memmove(dst_ptr, msg_ptr, chan->msg_size);

        pthread_mutex_unlock(&chan->lock);
        return true;
    }
    if (try) {
        pthread_mutex_unlock(&chan->lock);
        return false;
    }

    if (chan->closed) {
        rti_throw("send on closed channel", false);
        pthread_mutex_unlock(&chan->lock);
        return false;
    }

    // 直接将自身 yield 并等待 recv 唤醒
    DEBUGF("[rt_chan_send] recvq empty,  will yield to waiting")
    coroutine_t *co = coroutine_get();

    // recvq empty, 将自身 send 和 msg 地址存放在 sendq 中, 等待 recv 唤醒
    linkco = rti_acquire_linkco();

    linkco->co = co;
    linkco->data = msg_ptr;
    linkco->is_select = false;
    linkco->chan = chan;

    co->waiting = linkco;
    waitq_push(&chan->sendq, linkco);

    assert(co->wait_unlock_fn == NULL);
    co_yield_waiting(co, chan_yield_commit, &chan->lock);
    assertf(linkco == co->waiting, "coroutine waiting list is corrupted");

    co->waiting = NULL;
    co->data = NULL;
    rti_release_linkco(linkco);

    // 已经 send 完成，也可能是 chan closed
    if (!linkco->success) {
        rti_throw("send on closed channel", false);
        return false;
    }

    // 数据已经取走，直接返回即可, coroutine_resume 后会将上面的 yield_lock 进行解锁
    DEBUGF("[rt_chan_send] co wakeup, will return, msg(int)=%ld",
           fetch_int_value((addr_t) msg_ptr, chan->msg_size))
    return true;
}

static void rt_recv(n_chan_t *chan, linkco_t *linkco, void *msg_ptr, scase *cases, int16_t *lockorder, int16_t norder) {
    assert(msg_ptr);
    if (buf_empty(chan)) {
        DEBUGF("[rt_chan_recv] sendq have, buf empty,  will direct wakeup")
        rt_msg_transmit(linkco->co, linkco->data, msg_ptr, false, chan->msg_size);
    } else {
        assert(buf_full(chan));

        // 直接将缓冲区中的数据 pop 到 msg 中，然后将 sendq 中的数据 push 到尾部。
        assert(msg_ptr);

        buf_pop(chan, msg_ptr);

        // copy linkco->data to buf tail
        void *dst_ptr = buf_next_ref(chan);
        rt_msg_transmit(linkco->co, linkco->data, dst_ptr, false, chan->msg_size);
    }

    linkco->data = NULL;
    coroutine_t *co = linkco->co; // ready co

    if (cases) {
        selunlock(cases, lockorder, norder);
    } else {
        pthread_mutex_unlock(&chan->lock);
    }

    co->data = linkco;
    linkco->success = true;

    co_ready(co);
}

/**
 * recv 即使是 closed 能继续读取数据, 直到读取完毕, 此时将会抛出一个 error
 * @param chan
 * @param msg_ptr
 * @param try
 */
bool rt_chan_recv(n_chan_t *chan, void *msg_ptr, bool try) {
    pthread_mutex_lock(&chan->lock);

    linkco_t *linkco = waitq_pop(&chan->sendq);
    if (linkco) {
        rt_recv(chan, linkco, msg_ptr, NULL, NULL, 0);
        return true;
    }

    if (!buf_empty(chan)) {
        assert(waitq_empty(&chan->sendq));
        buf_pop(chan, msg_ptr);

        pthread_mutex_unlock(&chan->lock);
        return true;
    }

    // no buf no sendq
    if (try) {
        pthread_mutex_unlock(&chan->lock);
        return false;
    }

    DEBUGF("[rt_chan_recv] sendq empty, will yield to waiting")
    if (chan->closed) {
        rti_throw("recv on closed channel", false);
        pthread_mutex_unlock(&chan->lock);
        return false;
    }
    coroutine_t *co = coroutine_get();

    // sendq empty, 将自身 recv 和 msg 地址存放在 recvq 中, 等待 send 唤醒
    linkco = rti_acquire_linkco();
    linkco->co = co;
    linkco->data = msg_ptr;
    linkco->waitlink = NULL;
    linkco->is_select = false;
    linkco->chan = chan;
    co->waiting = linkco;
    co->data = NULL;
    waitq_push(&chan->recvq, linkco);

    assert(co->wait_unlock_fn == NULL);
    co_yield_waiting(co, chan_yield_commit, &chan->lock);

    assert(linkco == co->waiting);
    co->waiting = NULL;
    co->data = NULL;
    linkco->chan = NULL;
    rti_release_linkco(linkco);

    // 数据已经收到，直接返回即可, coroutine_resume 后会将上面的 yield_lock 进行解锁
    DEBUGF("[rt_chan_recv] co wakeup, will return, msg(int)=%ld",
           fetch_int_value((addr_t) msg_ptr, chan->msg_size))
    return true;
}

void rt_chan_close(n_chan_t *chan) {
    if (chan->closed) {
        rti_throw("chan already closed", false);
        return;
    }

    pthread_mutex_lock(&chan->lock);
    chan->closed = true;
    pthread_mutex_unlock(&chan->lock);
}

bool rt_chan_is_closed(n_chan_t *chan) {
    return chan->closed;
}

bool rt_chan_is_successful(n_chan_t *chan) {
    return chan->successful;
}

// 用于锁定所有 channel
static void sellock(scase *cases, int16_t *lockorder, int16_t norder) {
    n_chan_t *c = NULL;
    for (int16_t i = 0; i < norder; i++) {
        int16_t o = lockorder[i];
        n_chan_t *c0 = cases[o].chan;
        if (c0 != c) {
            c = c0;
            pthread_mutex_lock(&c->lock);
        }
    }
}

// 用于解锁所有 channel
static void selunlock(scase *cases, int16_t *lockorder, int16_t norder) {
    for (int16_t i = norder - 1; i >= 0; i--) {
        n_chan_t *chan = cases[lockorder[i]].chan;
        if (i > 0 && chan == cases[lockorder[i - 1]].chan) {
            continue;
        }

        pthread_mutex_unlock(&chan->lock);
    }
}

// 用于 select 的 yield commit
static bool selpark_commit(coroutine_t *co, void *arg) {
    n_chan_t *lastc = NULL;
    for (linkco_t *lc = co->waiting; lc != NULL; lc = lc->waitlink) {
        if (lc->chan != lastc && lastc != NULL) {
            pthread_mutex_unlock(&lastc->lock);
        }

        lastc = lc->chan;
    }

    if (lastc != NULL) {
        pthread_mutex_unlock(&lastc->lock);
    }

    return true;
}

/**
 * 返回 cases index, 如果 default 分支被选中则返回 -1
 * @param cases
 * @param send_count
 * @param recvs_count
 * @param _try
 * @return
 */
int rt_chan_select(scase *cases, int16_t sends_count, int16_t recvs_count, bool _try) {

    DEBUGF("[rt_chan_select] cases = %p, sends_count = %d, recvs_count = %d", cases, sends_count, recvs_count);

    int16_t cases_count = sends_count + recvs_count;
    int16_t *order = mallocz(sizeof(int16_t) * cases_count * 2);
    int16_t *pollorder = order;
    int16_t *lockorder = order + cases_count;

    // 生成 pollorder by cheaprand
    int pollorder_count = 0;
    for (int16_t i = 0; i < cases_count; i++) {
        scase *cas = &cases[i];
        if (cas->chan == NULL) {
            cas->msg_ptr = NULL;
            continue;
        }

        int16_t j = rand() % (pollorder_count + 1);
        pollorder[pollorder_count] = pollorder[j];
        pollorder[j] = i;
        pollorder_count++;
    }

    // 生成 lockorder by chan address
    for (int16_t i = 0; i < cases_count; i++) {
        int16_t j = i;

        n_chan_t *chan = cases[pollorder[i]].chan;
        while (j > 0 && cases[lockorder[(j - 1) / 2]].chan < chan) {
            int16_t k = (j - 1) / 2;
            lockorder[j] = lockorder[k];
            j = k;
        }

        lockorder[j] = pollorder[i];
    }

    for (int16_t i = cases_count - 1; i >= 0; i--) {
        int16_t o = lockorder[i];
        n_chan_t *chan = cases[o].chan;
        lockorder[i] = lockorder[0];
        int16_t j = 0;
        while (true) {
            int16_t k = j * 2 + 1;
            if (k >= i) {
                break;
            }
            if (k + 1 < i && cases[lockorder[k]].chan < cases[lockorder[k + 1]].chan) {
                k++;
            }
            if (chan < cases[lockorder[k]].chan) {
                lockorder[j] = lockorder[k];
                j = k;
                continue;
            }
            break;
        }

        lockorder[j] = o;
    }

    // sellock scases by lockorder
    sellock(cases, lockorder, cases_count);

    linkco_t *lc = NULL;

    // pass 1 - look for something already waiting
    int16_t casi = 0;
    scase *cas = NULL;
    bool case_success = false;
    n_chan_t *c = NULL;
    for (int16_t i = 0; i < cases_count; i++) {
        casi = i;
        cas = &cases[casi];
        c = cas->chan;
        assert(c);

        if (casi >= sends_count) {
            // recv
            lc = waitq_pop(&c->sendq);
            if (lc != NULL) {
                goto RECV;
            }

            if (!buf_empty(c)) {
                goto BUFRECV;
            }

            if (c->closed != 0) {
                goto RCLOSE;
            }
        } else {
            // send
            if (c->closed != 0) {
                goto SCLOSE;
            }

            lc = waitq_pop(&c->recvq);
            if (lc != NULL) {
                goto SEND;
            }

            if (!buf_full(c)) {
                goto BUFSEND;
            }
        }
    }

    if (_try) {
        selunlock(cases, lockorder, cases_count);
        casi = -1;
        goto RETC;
    }

    // pass 2 - enqueue on all chans
    coroutine_t *co = coroutine_get();
    assert(co->waiting == NULL);

    linkco_t *lclist = NULL;
    linkco_t *lcnext = NULL;
    linkco_t **nextp = NULL;

    nextp = &co->waiting;
    for (int16_t i = 0; i < cases_count; i++) {
        casi = i;
        cas = &cases[casi];
        c = cas->chan;

        lc = rti_acquire_linkco();
        lc->co = co;
        lc->is_select = true;
        lc->data = cas->msg_ptr;
        lc->chan = c;

        *nextp = lc;
        nextp = &lc->waitlink;

        if (casi < sends_count) {
            waitq_push(&c->sendq, lc);
        } else {
            waitq_push(&c->recvq, lc);
        }
    }

    co->data = NULL;
    // yield
    co_yield_waiting(co, selpark_commit, NULL);
    DEBUGF("[rt_chan_select] co wakeup, will return, casi=%d", casi);

    // sellock
    sellock(cases, lockorder, cases_count);
    atomic_store(&co->select_done, 0);

    // get linkco by co->data
    lc = co->data;
    assert(lc);
    co->data = NULL;

    // pass3  dequeue from unsuccessful chans
    // otherwise they stack up on quiet channels
    // record the successful case, if any.
    // We singly-linked up the SudoGs in lock order.
    casi = -1;
    cas = NULL;
    case_success = false;
    lclist = co->waiting;
    // Clear all elem before unlinking from gp.waiting.
    for (linkco_t *lc1 = co->waiting; lc1 != NULL; lc1 = lc1->waitlink) {
        lc1->is_select = false;
        lc1->data = NULL;
        lc1->chan = NULL;
    }
    co->waiting = NULL;

    scase *k = NULL;
    for (int16_t i = 0; i < cases_count; i++) {
        k = &cases[i];
        if (lc == lclist) {
            casi = i;
            cas = k;
            case_success = lclist->success;
        } else {
            c = k->chan;
            if (i < sends_count) {
                waitq_remove(&c->sendq, lclist);
            } else {
                waitq_remove(&c->recvq, lclist);
            }
        }

        lcnext = lclist->waitlink;
        lclist->waitlink = NULL;

        rti_release_linkco(lclist);
        lclist = lcnext;
    }

    assert(cas != NULL);

    c = cas->chan;

    if (casi < sends_count) {
        if (!case_success) {
            goto SCLOSE;
        }
    }

    selunlock(cases, lockorder, cases_count);
    goto RETC;

    BUFRECV:
    case_success = true;
    buf_pop(c, cas->msg_ptr);
    selunlock(cases, lockorder, cases_count);
    goto RETC;

    BUFSEND:
    case_success = true;
    void *dst_ptr = buf_next_ref(c);
    memmove(dst_ptr, cas->msg_ptr, c->msg_size);
    selunlock(cases, lockorder, cases_count);
    goto RETC;

    RECV:
    rt_recv(c, lc, cas->msg_ptr, cases, lockorder, cases_count);
    case_success = true;
    goto RETC;

    RCLOSE:
    selunlock(cases, lockorder, cases_count);
    case_success = false;
    cases[casi].chan->successful = false;
    goto RETC;

    SEND:
    rt_send(c, lc, cas->msg_ptr, cases, lockorder, cases_count);

    goto RETC;

    SCLOSE:
    selunlock(cases, lockorder, cases_count);
    case_success = false;
    cases[casi].chan->successful = false;
    goto RETC;

    RETC:
    if (casi >= 0) {

    }

    free(order);
    DEBUGF("[rt_chan_select] return casi=%d", casi);
    return casi;
}
