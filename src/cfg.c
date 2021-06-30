#include "cfg.h"

/**
 * l1:
 *  move
 *  move
 *  add
 *  goto l2 一定会有 label
 *
 * C1:
 *  add
 *  add
 *  goto l3
 *
 * C2:
 *  move
 *  move
 *
 * end_for:
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
  table *basic_block_table = table_new();

  // 1.根据 label 分块,仅考虑顺序块挂链关系
  lir_basic_block *curr_block = NULL;
  lir_op *curr_op = c->operates->front;
  while (curr_op != NULL) {
    if (curr_op->type == LIR_OP_TYPE_LABEL) {
      // 1. last block 添加 last_op,并截断  last_op->succ
      if (curr_block != NULL) {
        curr_block->last_op = curr_op->pred;
        curr_block->last_op->succ = NULL;
      }

      // 2. new block 添加 first_op, new block 添加到 table 中,和 c->blocks 中
      lir_basic_block *new_block = lir_new_basic_block();
      new_block->first_op = curr_op;
      new_block->name = *((lir_operand_label *) curr_op->result.value);
      table_set(basic_block_table, new_block->name, new_block);
      c->blocks.list[c->blocks.count++] = new_block;


      // 3. 建立顺序关联关系
      if (curr_block != NULL && curr_block->last_op->type != LIR_OP_TYPE_GOTO) {
        lir_basic_blocks_push(&curr_block->succs, new_block);
        lir_basic_blocks_push(&new_block->preds, curr_block);
      }

      // 4. curr = new
      curr_block = new_block;
    }

    curr_op = curr_op->succ;
  }

  // 2. 根据 last_op is goto,cmp_goto 进一步构造关联关系
  for (int i = 0; i < c->blocks.count; ++i) {
    curr_block = c->blocks.list[i];
    if (curr_block->last_op->type != LIR_OP_TYPE_GOTO
        && curr_block->last_op->type != LIR_OP_TYPE_CMP_GOTO) {
      continue;
    }

    // 处理 goto 模式的关联关系
    string name = *((lir_operand_label *) curr_block->last_op->result.value);
    lir_basic_block *target_block = (lir_basic_block *) table_get(basic_block_table, name);

    lir_basic_blocks_push(&curr_block->succs, target_block);
    lir_basic_blocks_push(&target_block->preds, curr_block);
  }
}