#ifndef NATURE_SRC_CFG_H_
#define NATURE_SRC_CFG_H_

#include "lir.h"

/**
 * operates to basic block
 * 按顺序遍历指令集
 * new lir_basic_block
 */
void cfg(closure *c);

void *lir_basic_blocks_push(lir_basic_blocks *list, lir_basic_block *item);

#endif //NATURE_SRC_CFG_H_
