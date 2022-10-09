#ifndef NATURE_SRC_CFG_H_
#define NATURE_SRC_CFG_H_

#include "src/lir/lir.h"

#define LIR_BLOCKS_PUSH(_list, item) (_list)->list[(_list)->count++] = (item)

/**
 * operations to basic block
 * 按顺序遍历指令集
 * new lir_basic_block
 */
void cfg(closure *c);

#endif //NATURE_SRC_CFG_H_
