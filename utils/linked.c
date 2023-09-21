#include "linked.h"
#include <assert.h>

static uint16_t linked_count(linked_t *l) {
    uint16_t count = 0;
    LINKED_FOR(l) {
        count++;
    }
    return count;
}

linked_t *linked_new() {
    linked_t *l = mallocz(sizeof(linked_t));
    l->count = 0;

    linked_node *empty = linked_new_node();
    l->front = empty;
    l->rear = empty;

    return l;
}

// 尾部 push
void linked_push(linked_t *l, void *value) {
    linked_node *empty = linked_new_node();
    empty->prev = l->rear;

    l->rear->value = value;
    l->rear->succ = empty;

    l->rear = empty;
    l->count++;
}

// 头部 pop
void *linked_pop(linked_t *l) {
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    linked_node *temp = l->front; // 推出头部节点
    void *value = temp->value;

    l->front = temp->succ;
    l->front->prev = NULL;
    l->count--;

    return value;
}

// 头部 pop value, 并清理 node
void *linked_pop_free(linked_t *l) {
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    linked_node *node = l->front; // 推出头部节点
    void *value = node->value;

    l->front = node->succ;
    l->front->prev = NULL;
    l->count--;

    free(node);

    return value;
}

linked_node *linked_new_node() {
    linked_node *node = mallocz(sizeof(linked_node));
    node->value = NULL;
    node->succ = NULL;
    node->prev = NULL;
    return node;
}

bool linked_empty(linked_t *l) {
    if (l->count == 0) {
        return true;
    }
    return false;
}

/**
 * 在 node 位置将 list 分成两部分，其中 node 属于第二部分
 * 截断第一部分,rear 为 empty 节点
 * @param l
 * @param node
 * @return
 */
linked_t *linked_split(linked_t *l, linked_node *node) {
    assert(l->count > 0);
    assert(node);

    linked_t *new_list = linked_new();

    new_list->front = node;
    new_list->rear = l->rear;
    new_list->count = linked_count(new_list);

    if (node == l->front) {
        linked_cleanup(l);
        return new_list;
    }
    // 原 rear 截断(list 的结尾是 empty)
    linked_node *rear_empty = linked_new_node();
    linked_node *last = node->prev;
    rear_empty->prev = last;
    last->succ = rear_empty;
    l->rear = rear_empty;
    l->count = linked_count(l);

    return new_list;
}

void linked_concat(linked_t *dst, linked_t *src) {
    linked_node *current = src->front;
    while (current->value != NULL) {
        void *v = current->value;
        linked_push(dst, v);
        current = current->succ;
    }
}

linked_node *linked_last(linked_t *l) {
    if (linked_empty(l)) {
        return NULL;
    }
    return l->rear->prev;
}

linked_node *linked_first(linked_t *l) {
    return l->front;
}

// 在指定位置的后方插入节点，尾部需要是 empty 节点, rear 指向 empty
void linked_insert_after(linked_t *l, linked_node *prev, void *value) {
    linked_node *await = linked_new_node();
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

// 在指定位置的前方插入节点，尾部需要是 empty 节点, rear 指向 empty
void linked_insert_before(linked_t *l, linked_node *succ, void *value) {
    linked_node *await = linked_new_node();
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

void linked_cleanup(linked_t *l) {
    l->count = 0;

    linked_node *empty = linked_new_node();
    l->front = empty;
    l->rear = empty;
}

void linked_remove(linked_t *l, linked_node *node) {
    if (node == NULL) {
        return;
    }

    if (node == l->front) {
        linked_pop(l);
        return;
    }

    linked_node *prev = node->prev;
    linked_node *succ = node->succ;

    prev->succ = succ;
    succ->prev = prev;

    l->count--;
}

void linked_remove_free(linked_t *l, linked_node *node) {
    linked_remove(l, node);
    free(node);
}


void linked_free(linked_t *l) {
    while (l->count > 0) {
        linked_pop_free(l);
    }

    // 清理 empty_node
    free(l->front);

    // free my
    free(l);
}
