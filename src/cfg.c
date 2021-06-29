#include "cfg.h"
#include "lib/helper.h"

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
  // 1.创建 table, 记录每一个 label 的 succs/preds
  table *label_succs_table = table_new();
  table *label_preds_table = table_new();

  // 2.根据 label 分块
  lir_basic_block *current_block = NULL;
  lir_op *current_op = c->operates->front;

  while (current_op != NULL) {
//    if (current_op->type == LIR_OP_TYPE_LABEL) {
//      // 新的块的开始
//      lir_basic_block *new_block = lir_new_basic_block();
//      new_block->first_op = current_op;
//      lir_operand_label *new_block_label = (lir_operand_label *) new_block->first_op->result.value;
//
//      if (!table_exist(label_succs_table, *new_block_label)) {
//        string*succs = malloc(UINT8_MAX * sizeof(string));
//        table_set(label_succs_table, *new_block_label, succs);
//      }
//      if (!table_exist(label_preds_table, *new_block_label)) {
//        string*preds = malloc(UINT8_MAX * sizeof(string));
//        table_set(label_preds_table, *new_block_label, preds);
//      }
//
//
//
//      // 添加后继
//      if (current_block != NULL) {
//        lir_operand_label *current_block_label = (lir_operand_label *) current_block->first_op->result.value;
//        string*succs = (string*) table_get(label_succs_table, *current_block_label);
//        string_to_unique_list(succs, *new_block_label);
//        table_set(label_succs_table, *current_block_label, succs);
//      }
//
//      // 添加前驱
//
//      current_block = new_block;
    }

    // goto 的 label 块,是不是遇到 label 就要停
    if (current_op->type == LIR_OP_TYPE_CMP_GOTO) {
    }

    current_op = current_op->succ;
  }

  // 3.写入 succs/preds

  // 4.遍历合并单 succs/preds
}