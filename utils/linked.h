#ifndef NATURE_UTILS_LINKED_H_
#define NATURE_UTILS_LINKED_H_

#include <stdint.h>
#include <stdlib.h>

#include "helper.h"

// rear 为 empty node, 不会进入到循环中(一旦不满足条件，就会立即退出)
#define LINKED_FOR(_list) for (linked_node *node = _list->front; node != _list->rear; node = node->succ)

#define LINKED_VALUE() (node->value)
#define LINKED_NODE() (node)

typedef struct linked_node {
    void *value;
    struct linked_node *succ;
    struct linked_node *prev;
} linked_node;

/**
 * 队列或者说链表结构
 */
typedef struct {
    linked_node *front; // 头部
    linked_node *rear;  // 尾部
    uint16_t count;     // 队列中的元素个数
} linked_t;

linked_t *linked_new(); // 空队列， 初始化时，head,tail = NULL

void *linked_pop(linked_t *l); // 头出队列

void *linked_pop_free(linked_t *l); // 头出队列

void linked_push(linked_t *l, void *value); // 尾入队列

linked_node *linked_first(linked_t *l); // 最后一个可用元素，绝非空元素
linked_node *linked_last(linked_t *l);  // 最后一个可用元素，绝非空元素
void linked_insert_after(linked_t *l, linked_node *prev, void *value);

void linked_insert_before(linked_t *l, linked_node *succ, void *value);

linked_t *linked_split(linked_t *l, linked_node *node);

void linked_concat(linked_t *dst, linked_t *src); // src 追加到 dst 中

void linked_remove(linked_t *l, linked_node *node);

void linked_remove_free(linked_t *l, linked_node *node);

void linked_cleanup(linked_t *l);

void linked_free(linked_t *l);

linked_node *linked_new_node();

bool linked_empty(linked_t *l);

#endif // NATURE_SRC_LIB_LIST_H_
