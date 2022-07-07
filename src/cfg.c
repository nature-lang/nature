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
 *  TODO 添加统一结尾块，并将所有的 ret 指令添加跳转到统一结尾块
 *
 * @param c
 * @return
 */
void cfg(closure *c) {
    // 用于快速定位 block succ/pred
    table *basic_block_table = table_new();

    // 1.根据 label 分块,仅考虑顺序块挂链关系
    lir_basic_block *current_block = NULL;
    lir_op *current_op = c->operates->front;
    while (current_op != NULL) {
        if (current_op->op == LIR_OP_TYPE_LABEL) {
            lir_operand_label *operand_label = current_op->result->value;

            // 2. new block 添加 first_op, new block 添加到 table 中,和 c->blocks 中
            lir_basic_block *new_block = lir_new_basic_block();
            new_block->label = c->blocks.count;
            new_block->name = operand_label->ident;
            table_set(basic_block_table, new_block->name, new_block);
            c->blocks.list[c->blocks.count++] = new_block;


            // 3. 建立顺序关联关系 (由于顺序遍历 op, 所以只能建立顺序关系)
            if (current_block != NULL && current_block->operates->rear->op != LIR_OP_TYPE_GOTO) {
                LIR_BLOCKS_PUSH(&current_block->succs, new_block);
                LIR_BLOCKS_PUSH(&new_block->preds, current_block);
            }

            // 4. 截断 current block
            if (current_block != NULL) {
                current_block->operates->rear->succ = NULL;
            }


            // 5. current = new
            current_block = new_block;
        }

        list_op_push(current_block->operates, current_op);
        current_op = current_op->succ;
    }

    // 2. 根据 last_op is goto,cmp_goto 构造跳跃关联关系
    // call 调到别的 closure 去了，不在当前 closure cfg 构造的考虑范围
    for (int i = 0; i < c->blocks.count; ++i) {
        current_block = c->blocks.list[i];
        lir_op *last_op = current_block->operates->rear;
        if (last_op->op != LIR_OP_TYPE_GOTO && last_op->op != LIR_OP_TYPE_CMP_GOTO) {
            continue;
        }
        lir_operand_label *operand_label = last_op->result->value;

        // 处理 goto 模式的关联关系
        string name = operand_label->ident;
        lir_basic_block *target_block = (lir_basic_block *) table_get(basic_block_table, name);

        LIR_BLOCKS_PUSH(&current_block->succs, target_block);
        LIR_BLOCKS_PUSH(&target_block->preds, current_block);
    }

    // 添加入口块
    c->entry = c->blocks.list[0];
}