#include "cfg.h"
#include <assert.h>

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
 *  bal end_for
 *
 * end_for:
 *  test
 *  sub
 *  shift
 *  beq l3
 *  bal l3
 *
 * l3:
 *  test
 *  sub
 *  shift
 * 当遇到 label_a 时会开启一个新的 basic block, 如果再次遇到一个 label_b 也需要开启一个 branch 指令，但如果 label_a 最后一条指令不是 branch 指令，
 * 则需要添加 branch 指令到 label_a 中链接 label_a 和 label_b, 同理，如果遇到了 branch 指令(需要结束 basic block)到下一条指令不是 label，
 * 则需要添加 label 到 branch 到下一条指令中。 从而能够正确开启新的 basic block
 *
 * 为了 basic block 之间能够任意排序，即使是顺序 block，之间也需要添加 BAL 指令进行链接
 * 类似:
 * BEA B1
 * BAL B3
 *
 * 如果 from -> edge -> to, to 不是 from 的唯一 succ, from 也不是 to 的唯一 pred,这种边称为 critical edges，会影响 RESOLVE ssa 和 data_flow
 * 所以需要添加一个新的 basic block, 作为 edge 的中间节点,从而打破 critical edges
 * @param c
 * @return
 */
void cfg(closure_t *c) {
    // 用于快速定位 block succ/pred
    table_t *basic_block_table = table_new();

    // 1.根据 label(if/else/while 等都会产生 label) 分块,仅考虑顺序块关联关系
    basic_block_t *current_block = NULL; // 第一次 traverse 时还没有任何 block
    lir_op_t *label_op = list_first(c->operations)->value;
    assert(label_op->code == LIR_OPCODE_LABEL && "first op must be label");

    LIST_FOR(c->operations) {
        lir_op_t *op = LIST_VALUE();
        if (op->code == LIR_OPCODE_LABEL) {
            // 遇到 label， 开启一个新的 basic block
            lir_operand_symbol_label *operand_label = op->output->value;

            // 2. new block 添加 first_op, new block 添加到 table 中,和 c->blocks 中
            basic_block_t *new_block = lir_new_basic_block(operand_label->ident, c->blocks->count);
            table_set(basic_block_table, new_block->name, new_block);
            slice_push(c->blocks, new_block);

            // 3. 建立顺序关联关系 (由于顺序遍历 code, 所以只能建立顺序关系)
            if (current_block != NULL) {
                // 存在 new_block, current_block 必须已 BAL 指令结尾跳转到 new_block
                lir_op_t *last_op = list_last(current_block->operations)->value;
                if (last_op->code != LIR_OPCODE_BAL) {
                    lir_op_t *temp_op = lir_op_bal(lir_new_label_operand(new_block->name, true));
                    list_push(current_block->operations, temp_op);
                }
            }

            // 4. current = new
            current_block = new_block;
        }

        if (lir_op_is_branch(op) && LIST_NODE()->succ != NULL) {
            // 如果下一条指令不是 LABEL，则使用主动添加 temp label
            lir_op_t *next_op = LIST_NODE()->succ->value;
            if (next_op->code != LIR_OPCODE_LABEL) {
                lir_op_t *temp_label = lir_op_unique_label(TEMP_LABEL);
                list_insert_after(c->operations, LIST_NODE(), temp_label);
            }
        }

        // 值 copy
        list_push(current_block->operations, op);
    }

    // 2. 根据 last_op is goto,cmp_goto 构造跳跃关联关系(所以一个 basic block 通常只有两个 succ)
    // call 调到别的 closure_t 去了，不在当前 closure_t cfg 构造的考虑范围

    SLICE_FOR(c->blocks) {
        current_block = SLICE_VALUE(c->blocks);

        // 添加 first_op(label 之后的第一个 op) 和 last_op
        current_block->first_op = list_first(current_block->operations)->succ;
        current_block->last_op = list_last(current_block->operations);

        // 最后一个指令块的结尾指令不是 branch 分支
        lir_op_t *last_op = current_block->last_op->value;
        if (last_op->code != LIR_OPCODE_BAL) {
            continue;
        }

        char *name = ((lir_operand_symbol_label *) last_op->output->value)->ident;
        basic_block_t *target_block = (basic_block_t *) table_get(basic_block_table, name);
        assert(target_block != NULL && "target block must exist");
        slice_push(current_block->succs, target_block);
        slice_push(target_block->preds, current_block);

        lir_op_t *second_last_op = list_last(current_block->operations)->prev->value;
        if (!lir_op_is_branch(second_last_op)) {
            continue;
        }
        name = ((lir_operand_symbol_label *) second_last_op->output->value)->ident;
        target_block = (basic_block_t *) table_get(basic_block_table, name);
        assert(target_block != NULL && "target block must exist");
        slice_push(current_block->succs, target_block);
        slice_push(target_block->preds, current_block);
    }

    broken_critical_edges(c);

    // 添加入口块
    c->entry = c->blocks->take[0];
}

void broken_critical_edges(closure_t *c) {
    SLICE_FOR(c->blocks) {
        basic_block_t *b = SLICE_VALUE(c->blocks);
        for (int i = 0; i < b->preds->count; ++i) {
            basic_block_t *p = b->preds->take[i];
            if (b->preds->count > 1 && p->succs->count > 1) {
                // p -> b 为 critical edge， 需要再其中间插入一个 empty block(only contain label + bal operations)
                lir_op_t *label_op = lir_op_unique_label(TEMP_LABEL);
                lir_operand *label_operand = label_op->output;
                lir_op_t *bal_op = lir_op_bal(lir_new_label_operand(b->name, true));

                lir_operand_symbol_label *symbol_label = label_operand->value;
                basic_block_t *new_block = lir_new_basic_block(symbol_label->ident, c->blocks->count);
//                slice_insert(c->blocks, b->id, new_block);
                slice_push(c->blocks, new_block);
                // 添加指令
                list_push(new_block->operations, label_op);
                list_push(new_block->operations, bal_op);

                // cfg 关系调整
                slice_push(new_block->succs, b);
                slice_push(b->preds, new_block);

                slice_push(new_block->preds, p);
                slice_push(p->succs, new_block);
                // 从 p->succs 中删除 b, 从 b 的 preds 中删除 p
                for (int j = 0; j < p->succs->count; ++j) {
                    if (p->succs->take[j] == b) {
                        slice_remove(p->succs, j);
                        break;
                    }
                }
                for (int j = 0; j < b->preds->count; ++j) {
                    if (b->preds->take[j] == p) {
                        slice_remove(b->preds, j);
                        break;
                    }
                }

                // 跳转指令调整  p -> b 改成 p -> new_block -> b
                assert(OP(p->last_op->value)->code == LIR_OPCODE_BAL);
                symbol_label = OP(p->last_op->value)->output->value;
                if (symbol_label->ident == b->name) {
                    // change to new_block
                    symbol_label->ident = new_block->name;
                }

                if (lir_op_is_branch(p->last_op->prev->value)) {
                    symbol_label = OP(p->last_op->prev->value)->output->value;
                    if (symbol_label->ident == b->name) {
                        symbol_label->ident = new_block->name;
                    }
                }

            }
        }
    }

}
