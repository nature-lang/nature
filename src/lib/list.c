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
    l->rear->next = empty;

    l->rear = empty;
    l->count++;
}

// 在指定位置插入节点，尾部需要是 empty 节点, rear 指向 empty
void list_splice(list *l, list_node *node, void *value) {
    list_node *await = list_new_node();
    await->value = value;

    // 如果是要在最后一个节点 直接调用 push 就行了
    if (node == l->rear) {
        list_push(l, value);
        return;
    }

    // node <-> await <-> next
    list_node *next = node->next;

    node->next = await;
    await->prev = node;

    await->next = next;
    next->prev = await;

    l->count++;
}


// 头部 pop
void *list_pop(list *l) {
    if (l->count == 0) {
        return NULL; // null 表示队列为空
    }

    list_node *temp = l->front; // 推出头部节点
    void *value = temp->value;

    l->front = temp->next;
    l->front->prev = NULL;
    l->count--;
    free(temp);

    return value;
}

list_node *list_new_node() {
    list_node *node = malloc(sizeof(list_node));
    node->value = NULL;
    node->next = NULL;
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
        current = current->next;
    }
}

list_node *list_last(list *l) {
    if (list_empty(l)) {
        return NULL;
    }
    return l->rear->prev;
}
