#include "list.h"

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

// 在指定位置的后方插入节点，尾部需要是 empty 节点, rear 指向 empty
void list_insert(list *l, list_node *prev, void *value) {
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
    free(temp);

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
