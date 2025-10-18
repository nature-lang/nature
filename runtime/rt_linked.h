#ifndef NATURE_RT_LINKED_H
#define NATURE_RT_LINKED_H

#include "fixalloc.h"
#include "utils/mutex.h"
#include <uv.h>

#define RT_LINKED_FOR(_list) for (rt_linked_node_t *_node = _list.front; _node != _list.rear; _node = _node->succ)
#define RT_LINKED_VALUE() (_node->value)
#define RT_LINKED_NODE() (_node)

// 所有的 node 都是通过 fixalloc 分配
typedef struct rt_linked_node_t {
    void *value;
    struct rt_linked_node_t *succ;
    struct rt_linked_node_t *prev;
} rt_linked_node_t;

typedef struct {
    rt_linked_node_t *head;
    rt_linked_node_t *rear;

    fixalloc_t nodealloc;

    uint64_t count;
    pthread_mutex_t locker;
} rt_linked_fixalloc_t;

static inline void rt_linked_fixalloc_init(rt_linked_fixalloc_t *l) {
    l->count = 0;

    fixalloc_init(&l->nodealloc, sizeof(rt_linked_node_t));

    pthread_mutex_init(&l->locker, false);

    rt_linked_node_t *empty = fixalloc_alloc(&l->nodealloc);
    l->head = empty;
    l->rear = empty;
}

static inline bool rt_linked_fixalloc_empty(rt_linked_fixalloc_t *l) {
    pthread_mutex_lock(&l->locker);
    bool result = l->count == 0;
    pthread_mutex_unlock(&l->locker);
    return result;
}

static inline void rt_linked_fixalloc_push(rt_linked_fixalloc_t *l, void *value) {
    assert(l);

    pthread_mutex_lock(&l->locker);

    rt_linked_node_t *empty = fixalloc_alloc(&l->nodealloc);

    empty->prev = l->rear;

    l->rear->succ = empty;
    l->rear->value = value;

    l->rear = empty;
    l->count++;

    pthread_mutex_unlock(&l->locker);
}

// 尾部永远指向一个空白节点
static inline void rt_linked_fixalloc_push_heap(rt_linked_fixalloc_t *l, void *value) {
    assert(l);

    pthread_mutex_lock(&l->locker);

    rt_linked_node_t *new_node = fixalloc_alloc(&l->nodealloc);

    new_node->value = value;

    // 头部插入
    new_node->succ = l->head;
    l->head->prev = new_node;
    l->head = new_node;
    l->count++;

    pthread_mutex_unlock(&l->locker);
}

static inline void *rt_linked_fixalloc_pop_no_lock(rt_linked_fixalloc_t *l) {
    if (l->count == 0) {
        return NULL;
    }

    assertf(l->head, "l=%p front is null", l);
    rt_linked_node_t *node = l->head; // 推出头部节点
    void *value = node->value;

    if (!node->succ) {
        assertf(node->succ, "l=%p node=%p succ cannot null", l, node);
    }

    l->head = node->succ;
    l->head->prev = NULL;
    l->count--;

    fixalloc_free(&l->nodealloc, node);

    return value;
}

// pop and free node from front
static inline void *rt_linked_fixalloc_pop(rt_linked_fixalloc_t *l) {
    pthread_mutex_lock(&l->locker);
    void *value = rt_linked_fixalloc_pop_no_lock(l);
    pthread_mutex_unlock(&l->locker);
    return value;
}

static inline rt_linked_node_t *rt_linked_fixalloc_first(rt_linked_fixalloc_t *l) {
    pthread_mutex_lock(&l->locker);
    if (l->count == 0) {
        pthread_mutex_unlock(&l->locker);
        return NULL;
    }
    rt_linked_node_t *result = l->head;
    pthread_mutex_unlock(&l->locker);
    return result;
}

static inline void rt_linked_fixalloc_remove(rt_linked_fixalloc_t *l, rt_linked_node_t *node) {
    if (node == NULL) {
        return;
    }

    pthread_mutex_lock(&l->locker);
    if (node == l->head) {
        rt_linked_fixalloc_pop_no_lock(l);
        pthread_mutex_unlock(&l->locker);
        return;
    }

    rt_linked_node_t *prev = node->prev;
    rt_linked_node_t *succ = node->succ;
    prev->succ = succ;
    succ->prev = prev;

    l->count--;
    fixalloc_free(&l->nodealloc, node);

    pthread_mutex_unlock(&l->locker);
}

static inline rt_linked_node_t *rt_linked_fixalloc_last(rt_linked_fixalloc_t *l) {
    pthread_mutex_lock(&l->locker);
    if (l->count == 0) {
        pthread_mutex_unlock(&l->locker);
        return NULL;
    }
    rt_linked_node_t *node = l->rear->prev;

    pthread_mutex_unlock(&l->locker);
    return node;
}

static inline void rt_linked_fixalloc_free(rt_linked_fixalloc_t *l) {
    while (l->count > 0) {
        rt_linked_fixalloc_pop(l);
    }

    fixalloc_free(&l->nodealloc, l->rear);
    fixalloc_destroy(&l->nodealloc);
}

#endif// NATURE_RT_LINKED_H
