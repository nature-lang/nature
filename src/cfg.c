#include "cfg.h"


/**
 * l1:
 *  move
 *  move
 *  add
 *  goto l2 一定会有 label
 *  add
 *  add
 *  goto l3
 *  move
 *  move
 * l2:
 *  test
 *  sub
 *  shift
 *
 * l3:
 *  test
 *  sub
 *  shift
 *
 * @param c
 * @return
 */
lir_basic_blocks cfg(closure *c) {
//  lir_basic_blocks result;
//  return result;
  lir_basic_block *block = lir_new_basic_block();
  lir_op *current_op = c->operates->front;
  block->first_op = current_op; // 第一个指令入 block
  while (current_op != NULL) {
    if (current_op->type == LIR_OP_TYPE_CMP_GOTO) {
      // 二分点 => goto 目标块，和继续执行的 块
      // goto 的 label 块,是不是遇到 label 就要停
    }

    current_op = current_op->succ;
  }
}