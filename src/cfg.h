#ifndef NATURE_SRC_CFG_H_
#define NATURE_SRC_CFG_H_

#include "lir.h"

/**
 * asm_operations to basic block
 * 按顺序遍历指令集
 * new basic_block_t
 */
void cfg(closure_t *c);

#endif //NATURE_SRC_CFG_H_
