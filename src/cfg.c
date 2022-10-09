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
 * TODO 可能有基本块不是以 branch 结尾或者 label 开头
 * 当遇到 label_a 时会开启一个新的 basic block, 如果再次遇到一个 label_b 也需要开启一个 branch 指令，但如果 label_a 最后一条指令不是 branch 指令，
 * 则需要添加 branch 指令到 label_a 中链接 label_a 和 label_b, 同理，如果遇到了 branch 指令(需要结束 basic block)到下一条指令不是 label，
 * 则需要添加 label 到 branch 到下一条指令中。 从而能够正确开启新的 basic block
 *
 * 如果 from -> edge -> to, to 不是 from 的唯一 succ, from 也不是 to 的唯一 pred,这种边称为 critical edges，会影响 RESOLVE ssa 和 data_flow
 * 所以需要添加一个新的 basic block, 作为 edge 的中间节点,从而打破 critical edges
 * @param c
 * @return
 */
void cfg(closure *c) {
    // 用于快速定位 block succ/pred
    table *basic_block_table = table_new();

    // 1.根据 label(if/else/while 等都会产生 label) 分块,仅考虑顺序块关联关系
    lir_basic_block *current_block = NULL;
    list_node *current = c->operations->front;
    while (current->value != NULL) {
        lir_op *op = current->value;
        if (op->code == LIR_OPCODE_LABEL) {
            lir_operand_symbol_label *operand_label = op->result->value;

            // 2. new block 添加 first_op, new block 添加到 table 中,和 c->blocks 中
            lir_basic_block *new_block = lir_new_basic_block();
            new_block->label = c->blocks->count;
            new_block->name = operand_label->ident;
            new_block->operations = list_new();
            table_set(basic_block_table, new_block->name, new_block);
            slice_push(c->blocks, new_block);


            // 3. 建立顺序关联关系 (由于顺序遍历 code, 所以只能建立顺序关系)
            if (current_block != NULL) {
                lir_op *rear_op = list_last(current_block->operations)->value;
                if (rear_op->code != LIR_OPCODE_BAL) {
                    slice_push(current_block->succs, new_block);
                    slice_push(new_block->preds, current_block);
                }
            }

            // 4. 截断 current block
//            if (current_block != NULL) {
//                current_block->operations->rear->succ = NULL;
//            }


            // 5. current = new
            current_block = new_block;
        }

        list_push(current_block->operations, op);
        current = current->succ;
    }

    // 2. 根据 last_op is goto,cmp_goto 构造跳跃关联关系(所以一个 basic block 通常只有两个 succ)
    // call 调到别的 closure 去了，不在当前 closure cfg 构造的考虑范围
    for (int i = 0; i < c->blocks->count; ++i) {
        current_block = c->blocks->take[i];
        lir_op *last_op = list_last(current_block->operations)->value;
        if (last_op->code != LIR_OPCODE_BAL && last_op->code != LIR_OPCODE_BEQ) {
            continue;
        }
        lir_operand_symbol_label *operand_label = last_op->result->value;

        // 处理 goto 模式的关联关系
        string name = operand_label->ident;
        lir_basic_block *target_block = (lir_basic_block *) table_get(basic_block_table, name);

        slice_push(current_block->succs, target_block);
        slice_push(target_block->preds, current_block);
    }

    // 添加入口块
    c->entry = c->blocks->take[0];
}