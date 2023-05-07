#include "utils/stack.h"

ct_stack_t *stack_new() {
    ct_stack_t *s = malloc(sizeof(ct_stack_t));
    s->count = 0;
    // 栈顶始终使用空占位符
    stack_node *empty = stack_new_node(NULL);
    s->top = empty;
    return s;
}

void stack_push(ct_stack_t *s, void *value) {
    stack_node *empty = stack_new_node(NULL);
    s->top->value = value;
    empty->next = s->top;
    s->top = empty;
    s->count++;
}

void *stack_pop(ct_stack_t *s) {
    if (stack_empty(s)) {
        return NULL;
    }
    void *value = s->top->next->value;

    s->top = s->top->next;
    s->top->value = NULL;
    s->count--;

    return value;
}

bool stack_empty(ct_stack_t *s) {
    return s->count == 0;
}

stack_node *stack_new_node(void *value) {
    stack_node *n = malloc(sizeof(stack_node));
    n->next = NULL;
    n->value = value;
    return n;
}
