#ifndef NATURE_RT_LINKED_H
#define NATURE_RT_LINKED_H

#include "fixalloc.h"

#define RT_LINKED_FOR(_list) for (rt_linked_node_t *_node = _list.front; _node != _list.rear; _node = _node->succ)
#define RT_LINKED_VALUE() (_node->value)
#define RT_LINKED_NODE() (_node)

extern fixalloc_t global_nodealloc;
extern pthread_mutex_t global_nodealloc_locker;

// 所有的 node 都是通过 fixalloc 分配
typedef struct rt_linked_node_t {
    void *value;
    struct rt_linked_node_t *succ;
    struct rt_linked_node_t *prev;
} rt_linked_node_t;

typedef struct {
    rt_linked_node_t *front;
    rt_linked_node_t *rear;

    uint16_t count;

    fixalloc_t *nodealloc;
    pthread_mutex_t *nodealloc_locker;
} rt_linked_t;

static inline void rt_linked_init(rt_linked_t *l, fixalloc_t *nodealloc, pthread_mutex_t *nodealloc_locker) {
    l->count = 0;
    if (nodealloc == NULL) {
        l->nodealloc = &global_nodealloc;
    } else {
        l->nodealloc = nodealloc;
    }

    if (nodealloc_locker == NULL) {
        l->nodealloc_locker = &global_nodealloc_locker;
    } else {
        l->nodealloc_locker = nodealloc_locker;
    }

    pthread_mutex_lock(l->nodealloc_locker);
    rt_linked_node_t *empty = fixalloc_alloc(l->nodealloc);
    pthread_mutex_unlock(l->nodealloc_locker);

    l->front = empty;
    l->rear = empty;
}

static inline void rt_linked_push(rt_linked_t *l, void *value) {
    assert(l);
    assert(l->nodealloc_locker);

    // 创建一个新的 empty 节点
    pthread_mutex_lock(l->nodealloc_locker);
    rt_linked_node_t *empty = fixalloc_alloc(l->nodealloc);
    pthread_mutex_unlock(l->nodealloc_locker);

    empty->prev = l->rear;

    // 尾部插入，然后选择一个新的空白节点
    l->rear->value = value;
    l->rear->succ = empty;

    l->rear = empty;
    l->count++;
}

// pop and free node
static inline void *rt_linked_pop(rt_linked_t *l) {
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    assertf(l->front, "l=%p front is null", l);
    rt_linked_node_t *node = l->front; // 推出头部节点
    void *value = node->value;

    // 至少是一个 empty 节点而不是  null
    assertf(node->succ, "l=%p node=%p succ is null", l, node);

    l->front = node->succ;
    l->front->prev = NULL;
    l->count--;

    pthread_mutex_lock(l->nodealloc_locker);
    fixalloc_free(l->nodealloc, node);
    pthread_mutex_unlock(l->nodealloc_locker);

    return value;
}

static inline rt_linked_node_t *rt_linked_first(rt_linked_t *l) {
    if (l->count == 0) {
        return NULL;
    }

    return l->front;
}

static inline rt_linked_node_t *rt_linked_last(rt_linked_t *l) {
    if (l->count == 0) {
        return NULL;
    }

    return l->rear->prev;
}

static inline void rt_linked_destroy(rt_linked_t *l) {
    while (l->count > 0) {
        rt_linked_pop(l);
    }

    fixalloc_free(l->nodealloc, l->rear);
}

#endif // NATURE_RT_LINKED_H
