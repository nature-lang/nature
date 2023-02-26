#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include "stack.h"
#include "allocator.h"
#include "collector.h"

typedef struct {
    stack_t *user_stack;
    stack_t *system_stack;
    collector_t *collector;
} memory_t;

#endif //NATURE_MEMORY_H
