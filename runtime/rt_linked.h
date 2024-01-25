#ifndef NATURE_RT_LINKED_H
#define NATURE_RT_LINKED_H

#include "fixalloc.h"

// TODO 全局 alloc node 结构, 后续可以考虑更换成 processor_linked_nodealloc 来减少锁的粒度
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
} rt_linked_t;

static inline void rt_linked_init(rt_linked_t *l) {
    l->count = 0;

    pthread_mutex_lock(&global_nodealloc_locker);
    rt_linked_node_t *empty = fixalloc_alloc(&global_nodealloc);
    pthread_mutex_unlock(&global_nodealloc_locker);

    l->front = empty;
    l->rear = empty;
}

// 尾部 push
static inline void rt_linked_push(rt_linked_t *l, void *value) {
    pthread_mutex_lock(&global_nodealloc_locker);
    rt_linked_node_t *empty = fixalloc_alloc(&global_nodealloc);
    pthread_mutex_unlock(&global_nodealloc_locker);

    empty->prev = l->rear;

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

    rt_linked_node_t *node = l->front; // 推出头部节点
    void *value = node->value;

    l->front = node->succ;
    l->front->prev = NULL;
    l->count--;

    pthread_mutex_lock(&global_nodealloc_locker);
    fixalloc_free(&global_nodealloc, node);
    pthread_mutex_unlock(&global_nodealloc_locker);

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
}

#endif // NATURE_RT_LINKED_H
