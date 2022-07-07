#ifndef NATURE_SRC_LIB_STACK_H_
#define NATURE_SRC_LIB_STACK_H_

#include <stdlib.h>
#include "src/value.h"

typedef struct stack_node {
    void *value;
    struct stack_node *next;
} stack_node;

typedef struct stack {
    stack_node *top; // 始终指向栈顶的下一个元素
    uint16_t count;
} stack;

stack *stack_new();

void *stack_pop(stack *s); // 出栈
void stack_push(stack *s, void *value); // 入栈
stack_node *stack_new_node();

bool stack_empty(stack *s);

#endif //NATURE_SRC_LIB_STACK_H_
