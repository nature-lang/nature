#ifndef NATURE_MEMORY_H
#define NATURE_MEMORY_H

#include <stdlib.h>
#include <stdint.h>

#include "stack.h"
#include "allocator.h"
#include "collector.h"
#include "utils/links/symdef.h"
#include "utils/links/fndef.h"
#include "utils/value.h"

#define PTR_SIZE 8  // 单位 byte

// gc 基于此进行全部符号的遍历
extern int symdef_count;
extern symdef_t *symdef_list;

extern int fndef_count;
extern fndef_t *fndef;

typedef struct {
    stack_t *user_stack;
    stack_t *system_stack;
    collector_t *collector;
} memory_t;

memory_t *memory;

void system_stack();

void user_stack();

void *fetch_addr_value(addr_t addr);

#endif //NATURE_MEMORY_H
