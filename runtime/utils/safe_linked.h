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
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    linked_node *node = l->front; // 推出头部节点
    void *value = node->value;

    l->front = node->succ;
    l->front->prev = NULL;
    l->count--;

    safe_free(node);

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
    linked_node *await = safe_linked_new_node();
    await->value = value;

    // 如果是要在最后一个节点 直接调用 push 就行了
    if (succ == l->rear) {
        linked_push(l, value);
        return;
    }

    if (succ == NULL || succ->prev == NULL) {
        // 直接插入到头部之前
        await->succ = l->front;
        l->front->prev = await;

        l->front = await;
    } else {
        // prev <-> await <-> succ
        linked_node *prev = succ->prev;

        prev->succ = await;
        await->prev = prev;

        await->succ = succ;
        succ->prev = await;
    }

    l->count++;
}

static inline linked_t *safe_linked_split(linked_t *l, linked_node *node) {
    assert(l->count > 0);
    assert(node);

    linked_t *new_list = safe_linked_new();

    new_list->front = node;
    new_list->rear = l->rear;
    new_list->count = linked_count(new_list);

    if (node == l->front) {
        linked_cleanup(l);
        return new_list;
    }
    // 原 rear 截断(list 的结尾是 empty)
    linked_node *rear_empty = safe_linked_new_node();
    linked_node *last = node->prev;
    rear_empty->prev = last;
    last->succ = rear_empty;
    l->rear = rear_empty;
    l->count = linked_count(l);

    return new_list;
}

static inline void safe_linked_concat(linked_t *dst, linked_t *src) {
    linked_node *current = src->front;
    while (current->value != NULL) {
        void *v = current->value;
        safe_linked_push(dst, v);
        current = current->succ;
    }
}

static inline void safe_linked_remove(linked_t *l, linked_node *node) {
    if (node == NULL) {
        return;
    }

    if (node == l->front) {
        safe_linked_pop_free(l);
        return;
    }

    linked_node *prev = node->prev;
    linked_node *succ = node->succ;

    prev->succ = succ;
    succ->prev = prev;

    l->count--;
}

static inline void safe_linked_cleanup(linked_t *l) {
    l->count = 0;

    linked_node *empty = safe_linked_new_node();
    l->front = empty;
    l->rear = empty;
}

static inline void safe_linked_free(linked_t *l) {
    while (l->count > 0) {
        safe_linked_pop_free(l);
    }

    // 清理 empty_node
    safe_free(l->front);

    // free my
    safe_free(l);
}

#endif
