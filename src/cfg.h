#ifndef NATURE_SRC_CFG_H_
#define NATURE_SRC_CFG_H_

#include "lir.h"

#define LIR_BLOCKS_PUSH(_list, item) (_list)->list[(_list)->count++] = (item)

/**
 * asm_operations to basic block
 * 按顺序遍历指令集
 * new basic_block_t
 */
void cfg(closure_t *c);

void broken_critical_edges(closure_t *c);

#endif //NATURE_SRC_CFG_H_
