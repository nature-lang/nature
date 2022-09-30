#ifndef NATURE_SRC_LIB_LIST_H_
#define NATURE_SRC_LIB_LIST_H_

#include <stdlib.h>
#include <stdint.h>
#include "src/value.h"

// rear 为 empty node, 不会进入到循环中(一旦不满足条件，就会立即退出)
#define LIST_FOR(_list) for (list_node *node = _list->front; node != _list->rear; node = node->succ)

#define LIST_VALUE() (node->value)
#define LIST_NODE() (node)

typedef struct list_node {
    void *value;
    struct list_node *succ;
    struct list_node *prev;
} list_node;

/**
 * 队列或者说链表结构
 */
typedef struct {
    list_node *front; // 头部
    list_node *rear; // 尾部
    uint16_t count; // 队列中的元素个数
} list;

list *list_new(); // 空队列， 初始化时，head,tail = NULL
void *list_pop(list *l); // 头出队列
list_node *list_first(list *l); // 最后一个可用元素，绝非空元素
list_node *list_last(list *l); // 最后一个可用元素，绝非空元素
void list_push(list *l, void *value); // 尾入队列
void list_insert_after(list *l, list_node *prev, void *value);

void list_insert_before(list *l, list_node *succ, void *value);

list *list_split(list *l, list_node *node);

void list_append(list *dst, list *src); // src 追加到 dst 中

list_node *list_new_node();

bool list_empty(list *l);

#endif //NATURE_SRC_LIB_LIST_H_
