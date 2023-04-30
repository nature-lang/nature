#include "cfg.h"
#include "src/debug/debug.h"
#include <assert.h>

/**
 * 删除不可达代码块
 */
static void cfg_pruning(closure_t *c) {
    slice_t *blocks = slice_new();
    slice_push(blocks, c->blocks->take[0]);
    for (int i = 1; i < c->blocks->count; ++i) {
        basic_block_t *b = c->blocks->take[i];
        if (b->preds->count > 0) {
            slice_push(blocks, b);
            continue;
        }
        // 需要删减该块, 则该块的后记也需要清除对该块对引用
        for (int j = 0; j < b->succs->count; ++j) {
            basic_block_t *succ = b->succs->take[j];
            // 重新构建 succ 的 preds
            for (int k = 0; k < succ->preds->count; ++k) {
                basic_block_t *succ_pred = succ->preds->take[k];
                if (succ_pred->id == b->id) {
                    slice_remove(succ->preds, k);
                    break;
                }
            }
        }
    }
    c->blocks = blocks;
}

static void broken_critical_edges(closure_t *c) {
    SLICE_FOR(c->blocks) {
        basic_block_t *b = SLICE_VALUE(c->blocks);
        for (int i = 0; i < b->preds->count; ++i) {
            basic_block_t *p = b->preds->take[i]; // 从 p->b 这条边
            if (b->preds->count > 1 && p->succs->count > 1) {
                // p -> b 为 critical edge， 需要再其中间插入一个 empty block(only contain label + bal asm_operations)
                lir_op_t *label_op = lir_op_unique_label(c->module, TEMP_LABEL);
                lir_operand_t *label = label_op->output;
                lir_op_t *bal_op = lir_op_bal(label_operand(b->name, true));

                lir_symbol_label_t *symbol_label = label->value;
                basic_block_t *new_block = lir_new_basic_block(symbol_label->ident, c->blocks->count);
//                slice_insert(c->blocks, b->id, new_block);
                slice_push(c->blocks, new_block);
                // 添加指令
                linked_push(new_block->operations, label_op);
                linked_push(new_block->operations, bal_op);

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
                linked_node *last = linked_last(p->operations);
                assert(OP(last)->code == LIR_OPCODE_BAL);
                symbol_label = OP(last)->output->value;
                if (symbol_label->ident == b->name) {
                    // change to new_block
                    symbol_label->ident = new_block->name;
                }

                if (lir_op_branch(last->prev->value)) {
                    symbol_label = OP(last->prev)->output->value;
                    if (symbol_label->ident == b->name) {
                        symbol_label->ident = new_block->name;
                    }
                }

            }
        }
    }
}

/**
 * 从 entry 到 end block 到所有线路上都需要包含 return 语句
 * 如果到达了 label_end block 则说明这一条线路上没有 return, 直接 assert 即可
 * @param c
 * @param entry
 * @return
 */
static void return_check(closure_t *c, table_t *handled, basic_block_t *b) {
    if (c->return_operand == NULL) {
        return;
    }

    if (handled == NULL) {
        handled = table_new();
    }
    if (table_exist(handled, b->name)) {
        // 循环节点
        return;
    }

    // 含多个 return 指令，都清理掉
    LINKED_FOR(b->operations) {
        lir_op_t *op = LINKED_VALUE();
        // 如果当前分支包含 return, 那么当前分支到后续所有子分支都会包含
        if (op->code == LIR_OPCODE_RETURN) {
            return; // 递归返回
        }
    }

    // end_label 是最后到 label 如果都不包含 opcode return, 则存在一条不包含 return 的线路
    if (str_equal(b->name, c->end_label)) {
        assertf(false, "fn %s missing return", c->symbol_name);
    }

    // 当前 block 没有找到 return, 递归寻找 succ
    for (int i = 0; i < b->succs->count; ++i) {
        return_check(c, handled, b->succs->take[i]);
    }
}

static void cfg_build(closure_t *c) {
    // 用于快速定位 block succ/pred
    table_t *basic_block_table = table_new();

    // 1.根据 label(if/else/while 等都会产生 label) 分块,仅考虑顺序块关联关系
    basic_block_t *current_block = NULL; // 第一次 traverse 时还没有任何 block
    lir_op_t *label_op = linked_first(c->operations)->value;
    assert(label_op->code == LIR_OPCODE_LABEL && "first op must be label");

    for (linked_node *node = c->operations->front; node != c->operations->rear; node = node->succ) {
        lir_op_t *op = node->value;
        if (op->code == LIR_OPCODE_LABEL) {
            // 遇到 label， 开启一个新的 basic block
            lir_symbol_label_t *operand_label = op->output->value;

            // 2. new block 添加 first_op, new block 添加到 table 中,和 c->blocks 中
            basic_block_t *new_block = lir_new_basic_block(operand_label->ident, c->blocks->count);
            table_set(basic_block_table, new_block->name, new_block);
            slice_push(c->blocks, new_block);

            // 3. 建立顺序关联关系 (由于顺序遍历 code, 所以只能建立顺序关系)
            if (current_block != NULL) {
                // 存在 new_block, current_block 必须已 BAL 指令结尾跳转到 new_block
                linked_node *last_node = linked_last(current_block->operations);
                lir_op_t *last_op = last_node->value;

                // 重复指令处理
                // if last_op branch and label == bal target, the change this op code is bal
                if (lir_op_branch_cmp(last_op)) {
                    lir_symbol_label_t *label = last_op->output->value;
                    if (str_equal(label->ident, new_block->name)) {
                        // 删除最后一个指令,只保留下面需要接入到 bal
                        linked_remove(current_block->operations, last_node);
                    }
                }

                // 重启读取 last op value
                last_op = linked_last(current_block->operations)->value;

                // 所有指令块必须以 bal 结尾
                if (last_op->code != LIR_OPCODE_BAL) {
                    lir_op_t *temp_op = lir_op_bal(label_operand(new_block->name, true));
                    linked_push(current_block->operations, temp_op);
                }
            }

            // 4. current = new
            current_block = new_block;

            goto BLOCK_OP_PUSH;
        }


        if (lir_op_branch(op) && node->succ) {
            linked_node *succ = node->succ;

            // 连续 branch 优化，抛弃 bal 之后的 branch
            if (op->code == LIR_OPCODE_BAL && lir_op_branch(succ->value)) {
                /**
                 * bal 会导致强制跳转，如果有这种情况则到 xxx 之间的指令没有 label 引导，是不可达的
                 * 所以需要清理 bal 后续仅跟着的指令
                 * bal foo
                 * bal bar
                 * beq car
                 * bal dog
                 * xxx
                 */
                do {
                    linked_remove(c->operations, succ);
                    // succ 已经从 linked 中删除，所以 node->succ 将会是一个全新当值
                    succ = node->succ;
                } while (succ && lir_op_branch(succ->value));
            }

            // 如果下一条指令不是 LABEL，则使用主动添加  label 指令
            lir_op_t *next_op = succ->value;
            if (next_op->code != LIR_OPCODE_LABEL) {
                lir_op_t *temp_label = lir_op_unique_label(c->module, TEMP_LABEL);
                linked_insert_after(c->operations, LINKED_NODE(), temp_label);
            }
        }

        BLOCK_OP_PUSH:
        // 值 copy
        linked_push(current_block->operations, op);
    }

    // 2. 根据 last_op is goto,cmp_goto 构造跳跃关联关系(所以一个 basic block 通常只有两个 succ)
    // call 调到别的 closure_t 去了，不在当前 closure_t cfg 构造的考虑范围
    SLICE_FOR(c->blocks) {
        current_block = SLICE_VALUE(c->blocks);

        // 最后一个指令块的结尾指令不是 branch 分支
        lir_op_t *last_op = linked_last(current_block->operations)->value;
        if (last_op->code != LIR_OPCODE_BAL) {
            continue;
        }

        char *name = ((lir_symbol_label_t *) last_op->output->value)->ident;
        basic_block_t *target_block = (basic_block_t *) table_get(basic_block_table, name);
        assert(target_block != NULL && "target block must exist");
        slice_push(current_block->succs, target_block);
        slice_push(target_block->preds, current_block);

        lir_op_t *second_last_op = linked_last(current_block->operations)->prev->value;
        if (!lir_op_branch(second_last_op)) {
            continue;
        }
        name = ((lir_symbol_label_t *) second_last_op->output->value)->ident;
        target_block = (basic_block_t *) table_get(basic_block_table, name);
        assert(target_block != NULL && "target block must exist");
        slice_push(current_block->succs, target_block);
        slice_push(target_block->preds, current_block);
    }
}

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
    cfg_build(c);

    // 不可达代码块消除
    cfg_pruning(c);

    broken_critical_edges(c);

    // 添加入口块
    c->entry = c->blocks->take[0];

    // return 分析
    return_check(c, NULL, c->entry);
}
