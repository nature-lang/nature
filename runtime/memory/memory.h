#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include <stdlib.h>
#include <stdint.h>

#include "stack.h"
#include "allocator.h"
#include "collector.h"
#include "utils/links/symdef.h"
#include "utils/links/fndef.h"

// gc 基于此进行全部符号的遍历
extern int symdef_count;
extern symdef_t *symdef_list;

extern int fndef_count;
extern fndef_t *fndef;

#define addr_t uint64_t
//typedef uint64_t addr_t

typedef struct {
    stack_t *user_stack;
    stack_t *system_stack;
    collector_t *collector;
} memory_t;

memory_t *memory;

void system_stack();

void user_stack();

#endif //NATURE_MEMORY_H
