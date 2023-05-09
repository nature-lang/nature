#ifndef NATURE_SRC_LIB_STACK_H_
#define NATURE_SRC_LIB_STACK_H_

#include <stdlib.h>
#include "helper.h"

typedef struct stack_node {
    void *value;
    struct stack_node *next; // 栈的下一个元素
} stack_node;

typedef struct stack {
    stack_node *top; // 始终指向栈顶的下一个元素
    uint16_t count;
} ct_stack_t;

ct_stack_t *stack_new();

void *stack_pop(ct_stack_t *s); // 出栈 value
void stack_push(ct_stack_t *s, void *value); // 入栈
bool stack_empty(ct_stack_t *s);

stack_node *stack_new_node(void *value);

#endif //NATURE_SRC_LIB_STACK_H_
