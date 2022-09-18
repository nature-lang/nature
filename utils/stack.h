#ifndef NATURE_SRC_LIB_STACK_H_
#define NATURE_SRC_LIB_STACK_H_

#include <stdlib.h>
#include "src/value.h"

typedef struct stack_node {
    void *value;
    struct stack_node *next; // 栈的下一个元素
} stack_node;

typedef struct stack {
    stack_node *top; // 始终指向栈顶的下一个元素
    uint16_t count;
} stack_t;

stack_t *stack_new();

void *stack_pop(stack_t *s); // 出栈 value
void stack_push(stack_t *s, void *value); // 入栈
bool stack_empty(stack_t *s);
stack_node *stack_new_node(void *value);

#endif //NATURE_SRC_LIB_STACK_H_
