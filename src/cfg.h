#ifndef NATURE_SRC_CFG_H_
#define NATURE_SRC_CFG_H_

#include "lir.h"

/**
 * operates to basic block
 * 按顺序遍历指令集
 * new lir_basic_block
 * TODO 如何确定 preds 和 succs?
 * TODO 是否所有的 basic_block 都已 label 开头？
 * if 语句， while 语句， for 语句 才需要分块
 */
lir_basic_blocks cfg(closure *c);


#endif //NATURE_SRC_CFG_H_
