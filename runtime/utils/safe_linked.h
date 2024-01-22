#ifndef NATURE_UTILS_SAFE_LINKED_H_
#define NATURE_UTILS_SAFE_LINKED_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "utils/linked.h"

static inline linked_node *safe_linked_new_node() {
    linked_node *node = SAFE_NEW(linked_node);
    node->value = NULL;
    node->succ = NULL;
    node->prev = NULL;
    return node;
}

static inline linked_t *safe_linked_new() {
    linked_t *l = SAFE_NEW(linked_t);
    l->count = 0;

    linked_node *empty = safe_linked_new_node();
    l->front = empty;
    l->rear = empty;

    return l;
}

static inline void *safe_linked_pop_free(linked_t *l) {
    PREEMPT_LOCK();
    void *value = linked_pop_free(l);
    PREEMPT_UNLOCK();
    return value;
}

static inline void *safe_linked_pop(linked_t *l) {
    PREEMPT_LOCK();
    void *value = linked_pop(l);
    PREEMPT_UNLOCK();
    return value;
}

static inline void safe_linked_push(linked_t *l, void *value) {
    linked_node *empty = safe_linked_new_node();
    empty->prev = l->rear;

    l->rear->value = value;
    l->rear->succ = empty;

    l->rear = empty;
    l->count++;
}

static inline void safe_linked_insert_after(linked_t *l, linked_node *prev, void *value) {
    linked_node *await = safe_linked_new_node();
    await->value = value;

    // 如果是要在最后一个节点 直接调用 push 就行了
    if (prev == l->rear) {
        linked_push(l, value);
        return;
    }

    if (prev == NULL) {
        // 直接插入到头部之前
        await->succ = l->front;
        l->front->prev = await;

        l->front = await;
    } else {
        // prev <-> await <-> next
        linked_node *next = prev->succ;

        prev->succ = await;
        await->prev = prev;

        await->succ = next;
        next->prev = await;
    }

    l->count++;
}

static inline void safe_linked_insert_before(linked_t *l, linked_node *succ, void *value) {
    PREEMPT_LOCK();
    linked_insert_before(l, succ, value);
    PREEMPT_UNLOCK();
}

static inline linked_t *safe_linked_split(linked_t *l, linked_node *node) {
    PREEMPT_LOCK();
    linked_t *new_list = linked_split(l, node);
    PREEMPT_UNLOCK();

    return new_list;
}

static inline void safe_linked_concat(linked_t *dst, linked_t *src) {
    PREEMPT_LOCK();
    linked_concat(dst, src);
    PREEMPT_UNLOCK();
}

static inline void safe_linked_remove(linked_t *l, linked_node *node) {
    PREEMPT_LOCK();
    linked_remove(l, node);
    PREEMPT_UNLOCK();
}

static inline void safe_linked_remove_free(linked_t *l, linked_node *node) {
    PREEMPT_LOCK();
    linked_remove_free(l, node);
    PREEMPT_UNLOCK();
}

static inline void safe_linked_cleanup(linked_t *l) {
    PREEMPT_LOCK();
    linked_cleanup(l);
    PREEMPT_UNLOCK();
}

static inline void safe_linked_free(linked_t *l) {
    PREEMPT_LOCK();
    linked_free(l);
    PREEMPT_UNLOCK();
}

#endif
