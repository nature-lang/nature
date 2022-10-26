#include "list.h"
#include <assert.h>

static uint16_t list_count(list *l) {
    uint16_t count = 0;
    LIST_FOR(l) {
        count++;
    }
    return count;
}

list *list_new() {
    list *l = malloc(sizeof(list));
    l->count = 0;

    list_node *empty = list_new_node();
    l->front = empty;
    l->rear = empty;

    return l;
}

// 尾部 push
void list_push(list *l, void *value) {
    list_node *empty = list_new_node();
    empty->prev = l->rear;

    l->rear->value = value;
    l->rear->succ = empty;

    l->rear = empty;
    l->count++;
}

// 头部 pop
void *list_pop(list *l) {
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    list_node *temp = l->front; // 推出头部节点
    void *value = temp->value;

    l->front = temp->succ;
    l->front->prev = NULL;
    l->count--;
//    free(temp);

    return value;
}

list_node *list_new_node() {
    list_node *node = malloc(sizeof(list_node));
    node->value = NULL;
    node->succ = NULL;
    node->prev = NULL;
    return node;
}

bool list_empty(list *l) {
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
list *list_split(list *l, list_node *node) {
    assert(l->count > 1);
    assert(node);

    list *new_list = list_new();

    new_list->front = node;
    new_list->rear = l->rear;
    new_list->count = list_count(new_list);

    if (node == l->front) {
        return new_list;
    }
    // 原 rear 截断(list 的结尾是 empty)
    list_node *rear_empty = list_new_node();
    list_node *last = node->prev;
    rear_empty->prev = last;
    last->succ = rear_empty;
    l->rear = rear_empty;
    l->count = list_count(l);

    return new_list;
}

void list_append(list *dst, list *src) {
    list_node *current = src->front;
    while (current->value != NULL) {
        void *v = current->value;
        list_push(dst, v);
        current = current->succ;
    }
}

list_node *list_last(list *l) {
    if (list_empty(l)) {
        return NULL;
    }
    return l->rear->prev;
}

list_node *list_first(list *l) {
    return l->front;
}

// 在指定位置的后方插入节点，尾部需要是 empty 节点, rear 指向 empty
void list_insert_after(list *l, list_node *prev, void *value) {
    list_node *await = list_new_node();
    await->value = value;

    // 如果是要在最后一个节点 直接调用 push 就行了
    if (prev == l->rear) {
        list_push(l, value);
        return;
    }

    if (prev == NULL) {
        // 直接插入到头部之前
        await->succ = l->front;
        l->front->prev = await;

        l->front = await;
    } else {
        // prev <-> await <-> next
        list_node *next = prev->succ;

        prev->succ = await;
        await->prev = prev;

        await->succ = next;
        next->prev = await;
    }

    l->count++;
}

// 在指定位置的前方插入节点，尾部需要是 empty 节点, rear 指向 empty
void list_insert_before(list *l, list_node *succ, void *value) {
    list_node *await = list_new_node();
    await->value = value;

    // 如果是要在最后一个节点 直接调用 push 就行了
    if (succ == l->rear) {
        list_push(l, value);
        return;
    }

    if (succ == NULL || succ->prev == NULL) {
        // 直接插入到头部之前
        await->succ = l->front;
        l->front->prev = await;

        l->front = await;
    } else {
        // prev <-> await <-> succ
        list_node *prev = succ->prev;

        prev->succ = await;
        await->prev = prev;

        await->succ = succ;
        succ->prev = await;
    }

    l->count++;
}

void list_remove(list *l, list_node *node) {
    if (node == NULL) {
        return;
    }

    if (node == l->front) {
        list_pop(l);
        return;
    }

    list_node *prev = node->prev;
    list_node *succ = node->succ;

    prev->succ = succ;
    succ->prev = prev;

    l->count--;
//    free(node); // 不能释放，会导致引用关系丢失
}
