#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ssa.h"

/**
 * @param c
 */
void ssa(closure_t *c) {
    // 计算每个 basic_block 支配者，一个基本块可以被多个父级基本块支配
    ssa_dom(c);
    // 计算最近支配者
    ssa_idom(c);
    // 计算支配边界
    ssa_df(c);
    // use def
    ssa_use_def(c);
    // 活跃分析, 计算基本块入口活跃 live_in 和 出口活跃 live_out
    ssa_live(c);
    // 放置 phi 函数
    ssa_add_phi(c);
    // rename
    ssa_rename(c);
}

/**
 * 计算每个块的支配者块
 * 对于 Bi 和 Bj, 如果从入口节点 B0 到 Bj 的每条路径都包含 Bi, 那么Bi支配Bj
 *
 * A -> B -> C
 * A -> E -> F -> C
 * 到达 C 必须要经过其前驱 B,F 中的一个，如果一个节点即支配 B 也支配着 F，（此处是 A）
 * 则 A 一定支配着 C, 即如果 A 支配着 C 的所有前驱，则 A 一定支配 C
 * @param c
 */
void ssa_dom(closure_t *c) {
    // 初始化, dom[n0] = {l0}
    basic_block_t *basic_block;
    basic_block = c->blocks->take[0];
    slice_push(basic_block->dom, basic_block);

    // 初始化其他 dom 为所有节点的集合 {B0,B1,B2,B3..}
    for (int i = 1; i < c->blocks->count; ++i) {
        slice_t *other = slice_new(); // lir_basic_block

        // Dom[i] = N
        for (int k = 0; k < c->blocks->count; ++k) {
            slice_push(other, c->blocks->take[k]);
        }
        ((basic_block_t *) c->blocks->take[i])->dom = other;
    }

    // 求不动点
    bool changed = true;
    while (changed) {
        changed = false;

        // dom[0] 自己支配自己，没必要进一步深挖了,所以从 1 开始遍历
        for (int label = 1; label < c->blocks->count; ++label) {
            basic_block_t *block = c->blocks->take[label];
            slice_t *new_dom = ssa_calc_dom_blocks(c, block);
            // 判断 dom 是否不同
            if (ssa_dom_changed(block->dom, new_dom)) {
                changed = true;
                block->dom = new_dom;
            }
        }
    }
}

/**
 * 计算最近支配点
 * 在支配 p 的点中，若一个支配点 i≠p，满足 i 被 p 剩下的所有的支配点支配，则称 i 为 p 的最近支配点
 * B0 没有 idom
 * idom 一定是父节点中的某一个
 * 由于采用中序遍历编号，所以父节点的 label 一定小于当前 label
 * 当前 label 的多个支配者中 label 最小的一个就是 idom
 * @param c
 */
void ssa_idom(closure_t *c) {
    // 初始化 be_idom(支配者树)
    for (int label = 0; label < c->blocks->count; ++label) {
        slice_t *be_idom = slice_new();
        ((basic_block_t *) c->blocks->take[label])->be_idom = be_idom; // 一个基本块可以是节点的 idom, be_idom 用来构造支配者树
    }

    // 计算最近支配者 B0 没有支配者
    for (int label = 1; label < c->blocks->count; ++label) {
        basic_block_t *block = c->blocks->take[label];
        slice_t *dom = block->dom;
        // 最近支配者不能是自身
        for (int i = dom->count - 1; i >= 0; --i) {
            if (((basic_block_t *) dom->take[i])->label_index == block->label_index) {
                continue;
            }

            if (ssa_is_idom(dom, dom->take[i])) {
                block->idom = dom->take[i];

                // 添加反向关联关系
                slice_push(block->idom->be_idom, block);
                break;
            }
        }
    }
}

/**
 * 计算支配边界
 * 定义：n 支配 m 的前驱 p，且n 不严格支配 m (即允许 n = m), 则 m 是 n 的支配边界
 * (极端情况是会出现 n 的支配者自身的前驱)  http://asset.eienao.com/image-20210802183805691.png
 * 对定义进行反向推理可以得到
 *
 * 如果 m 为汇聚点，对于 m 的任意前驱 p, p 一定会支配自身，且不支配 m. 所以一定有 DF(p) = m
 * n in Dom(p) (支配着 p 的节点 n), 则有 n 支配 p, 如果 n 不支配 m, 则一定有 DF(p) = m
 */
void ssa_df(closure_t *c) {
    // 初始化空集为默认行为，不需要特别声明
//  for (int label = 0; label < c->blocks.count; ++label) {
//    lir_basic_blocks df = {.count = 0};
//    c->blocks.list[label]->df = df;
//  }

    for (int label = 0; label < c->blocks->count; ++label) {
        basic_block_t *current_block = c->blocks->take[label];
        // 非汇聚点不能是支配边界
        if (current_block->preds->count < 2) {
            continue;
        }

        for (int i = 0; i < current_block->preds->count; ++i) {
            basic_block_t *runner = current_block->preds->take[i];

            // 只要 pred 不是 当前块的最近支配者, pred 的支配边界就一定包含着 current_block
            // 是否存在 idom[current_block] != pred, 但是 dom[current_block] = pred?
            // 不可能， 因为是从 current_block->pred->idom(pred)
            // pred 和 idom(pred) 之间如果存在节点支配 current,那么其一定会支配 current->pred，则其就是 idom(pred)
            while (runner->label_index != current_block->idom->label_index) {
                slice_push(runner->df, current_block);

                // 向上查找
                runner = runner->idom;
            }
        }
    }
}

/**
 * 活动分析
 * live_in 在当前块 use，且未 def,或者存在与当前块 live out 中的的变量
 * live_out 离开当前块后依旧活跃的变量
 * @param c
 */
void ssa_live(closure_t *c) {
    // 初始化 live out 每个基本块为 ∅
    for (int label = 0; label < c->blocks->count; ++label) {
        slice_t *out = slice_new();
        slice_t *in = slice_new();
        ((basic_block_t *) c->blocks->take[label])->live_out = out;
        ((basic_block_t *) c->blocks->take[label])->live_in = in;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int label = c->blocks->count - 1; label >= 0; --label) {
            basic_block_t *block = c->blocks->take[label];
            slice_t *new_live_out = ssa_calc_live_out(c, c->blocks->take[label]);
            if (ssa_live_changed(block->live_out, new_live_out)) {
                changed = true;
                block->live_out = new_live_out;
            }

            slice_t *new_live_in = ssa_calc_live_in(c, c->blocks->take[label]);
            if (ssa_live_changed(block->live_in, new_live_in)) {
                changed = true;
                block->live_in = new_live_in;
            }
        }
    }
}

/**
 * 剪枝静态单赋值
 *
 * 添加了 phi 函数，还未进行重新编号
 * 支配边界其实就是两条线路的汇聚点,如果其中一条线路 n 中定义的每个变量 x 都需要在，
 * df(n) 支配边界中的块声明对应的 x = phi(x_pred1,x_pred2), 即使在 df(n) 中没有使用该变量
 * 毕竟谁能保证后续用不到呢(live_out 可以保证哈哈）
 * @param c
 */
void ssa_add_phi(closure_t *c) {
    for (int label = 0; label < c->blocks->count; ++label) {
        // 定义的每个变量都需要添加到支配边界中
        basic_block_t *basic_block = c->blocks->take[label];
        slice_t *def = basic_block->def;
        slice_t *df = basic_block->df;

        for (int i = 0; i < def->count; ++i) {
            lir_operand_var *var = def->take[i];

            for (int k = 0; k < df->count; ++k) {
                basic_block_t *df_block = df->take[k];
                // 判断该变量是否已经添加过 phi(另一个分支可能会先创建), 创建则跳过
                if (ssa_phi_defined(var, df_block)) {
                    continue;
                }

                // 如果 block 中的 def 变量不在当前 B in df 入口活跃,则不需要定义
                if (!ssa_var_belong(var, df_block->live_in)) {
                    continue;
                }

                // add phi (x1, x2, x3) => x
                lir_operand *result_param = LIR_NEW_OPERAND(LIR_OPERAND_VAR, LIR_NEW_VAR_OPERAND(var->ident));
                lir_operand *first_param = lir_new_phi_body(var, df_block->preds->count);
                lir_op_t *phi_op = lir_op_new(LIR_OPCODE_PHI, first_param, NULL, result_param);


                // insert to list(可能只有一个 label )
                list_node *label_node = df_block->operations->front;
                list_insert_after(df_block->operations, label_node, phi_op);
            }
        }
    }
}

/**
 * live out 为 n 的所有后继的 live_in 的并集
 */
slice_t *ssa_calc_live_out(closure_t *c, basic_block_t *block) {
    slice_t *live_out = slice_new();
    table_t *exist_var = table_new(); // basic var ident

    for (int i = 0; i < block->succs->count; ++i) {
        basic_block_t *succ = block->succs->take[i];

        // 未在 succ 中被重新定义(def)，且离开 succ 后继续活跃的变量
        for (int k = 0; k < succ->live_in->count; ++k) {
            lir_operand_var *var = succ->live_in->take[k];
            if (table_exist(exist_var, var->ident)) {
                continue;
            }
            slice_push(live_out, var);
            table_set(exist_var, var->ident, var);
        }
    }

    return live_out;
}

/**
 * 在当前块使用的变量 + 离开当前块依旧活跃的变量（这些变量未在当前块重新定义）
 */
slice_t *ssa_calc_live_in(closure_t *c, basic_block_t *block) {
    slice_t *live_in = slice_new();
    table_t *exist_var = table_new(); // basic var ident

    SLICE_FOR(block->use, lir_operand_var) {
        lir_operand_var *var = SLICE_VALUE();
        if (table_exist(exist_var, var->ident)) {
            continue;
        }

        slice_push(live_in, var);
        table_set(exist_var, var->ident, var);
    }

    SLICE_FOR(block->live_out, lir_operand_var) {
        lir_operand_var *var = SLICE_VALUE();
        if (table_exist(exist_var, var->ident)) {
            continue;
        }

        // 是否是当前块中定义的变量。
        if (ssa_var_belong(var, block->def)) {
            continue;
        }

        slice_push(live_in, var);
        table_set(exist_var, var->ident, var);
    }

    return live_in;
}

/**
 * 无顺序比较，所以需要用到 hash 表
 * @param old
 * @param new
 * @return
 */
bool ssa_live_changed(slice_t *old, slice_t *new) {
    if (old->count != new->count) {
        return true;
    }
    table_t *var_count = table_new();
    for (int i = 0; i < old->count; ++i) {
        string ident = ((lir_operand_var *) old->take[i])->ident;
        table_set(var_count, ident, old->take[i]);
    }

    // double count 判断同一个变量出现的次数,因为可能出现 new.count > old.count 的情况
    uint8_t double_count = 0;
    for (int i = 0; i < new->count; ++i) {
        string ident = ((lir_operand_var *) new->take[i])->ident;
        void *has = table_get(var_count, ident);
        if (has == NULL) {
//            table_free(var_count);
            return true;
        }

        double_count += 1;
    }

    if (double_count != new->count) {
//        table_free(var_count);
        return true;
    }

//    table_free(var_count);
    return false;
}

/**
 * 所有使用或者定义的变量，都加入到 global 中
 * use(m) 在 m 中重新定义之前就开始使用的变量
 * def(m) 在 m 中定义的所有变量的集合
 */
void ssa_use_def(closure_t *c) {
    // 可能出现 B0 处未定义，但是直接使用,也需要计入到符号表中
    table_t *exist_var = table_new();

    for (int label = 0; label < c->blocks->count; ++label) {
        slice_t *use = slice_new();
        slice_t *def = slice_new();

        table_t *exist_use = table_new();
        table_t *exist_def = table_new();

        basic_block_t *block = c->blocks->take[label];

        LIST_FOR(block->operations) {
            lir_op_t *op = LIST_VALUE();

            // first param (use)
            slice_t *vars = lir_operand_vars(op->first);
            OPERAND_VAR_USE(vars)

            // second param 可能包含 actual param
            vars = lir_operand_vars(op->second);
            OPERAND_VAR_USE(vars)

            // def
            if (op->output != NULL && op->output->type == LIR_OPERAND_VAR) {
                lir_operand_var *var = (lir_operand_var *) op->output->value;
                if (!table_exist(exist_def, var->ident)) {
                    slice_push(def, var);
                    table_set(exist_use, var->ident, var);
                }

                // 变量定义,例如 mov a->b, or add a,b -> c
                if (!table_exist(exist_var, var->ident)) {
                    slice_push(c->globals, var);
                    table_set(exist_var, var->ident, var);
                }
            }
        }

        block->use = use;
        block->def = def;
    }
}


/**
 * old_dom 和 new_dom list 严格按照 label 编号，从小到大排序,所以可以进行顺序比较
 * @param old_dom
 * @param new_dom
 * @return
 */
bool ssa_dom_changed(slice_t *old_dom, slice_t *new_dom) {
    if (old_dom->count != new_dom->count) {
        return true;
    }

    for (int i = 0; i < old_dom->count; ++i) {
        if (((basic_block_t *) old_dom->take[i])->label_index != ((basic_block_t *) new_dom->take[i])->label_index) {
            return true;
        }
    }

    return false;
}

/**
 * 计算基本块 i 的支配者，如果一个基本块支配 i 的所有前驱，则该基本块一定支配 i
 * @param c
 * @param block
 * @return
 */
slice_t *ssa_calc_dom_blocks(closure_t *c, basic_block_t *block) {
    slice_t *dom = slice_new();

    // 遍历当前 block 的 preds 的 dom_list, 然后求交集
    // 如果一个基本块支配者每一个前驱，那么其数量等于前驱的数量
    uint8_t block_label_count[UINT8_MAX];
    for (int label = 0; label < c->blocks->count; ++label) {
        block_label_count[label] = 0;
    }

    for (int i = 0; i < block->preds->count; ++i) {
        // 找到 pred
        slice_t *pred_dom = ((basic_block_t *) block->preds->take[i])->dom;

        // 遍历 pred_dom 为 label 计数
        for (int k = 0; k < pred_dom->count; ++k) {
            block_label_count[((basic_block_t *) pred_dom->take[k])->label_index]++;
        }
    }

    // 如果 block 的count 和 preds_count 的数量一致则表示该基本块支配了所有的前驱
    // dom 严格按照 label 从小到大排序, 且 block 自身一定是支配自身的
    for (int label = 0; label < c->blocks->count; ++label) {
        if (block_label_count[label] == block->preds->count || label == block->label_index) {
            slice_push(dom, c->blocks->take[label]);
        }
    }

    return dom;
}

// 前序遍历各个基本块
void ssa_rename(closure_t *c) {
    table_t *var_number_table = table_new(); // def 使用，用于记录当前应该命名为多少
    table_t *stack_table = table_new(); // use 使用，判断使用的变量的名称

    // 遍历所有名字变量,进行初始化
    SLICE_FOR(c->globals, lir_operand_var) {
        lir_operand_var *var = SLICE_VALUE();
        uint8_t *number = NEW(uint8_t);
        *number = 0;

        var_number_stack *stack = NEW(var_number_stack);
        stack->count = 0;

        table_set(var_number_table, var->old, number);
        table_set(stack_table, var->old, stack);
    }

    // 从根开始更名
    ssa_rename_basic(c->entry, var_number_table, stack_table);

    // 释放 NEW 的变量
    SLICE_FOR(c->globals, lir_operand_var) {
        lir_operand_var *var = SLICE_VALUE();
        uint8_t *number = table_get(var_number_table, var->old);
        var_number_stack *stack = table_get(stack_table, var->old);
        free(number);
        free(stack);
    }
}

void ssa_rename_basic(basic_block_t *block, table_t *var_number_table, table_t *stack_table) {
    // skip label code
//    lir_op *current_op = block->operations->front->succ;
    list_node *current = block->operations->front->succ;

    // 当前块内的先命名
    while (current->value != NULL) {
        lir_op_t *op = current->value;
        // phi body 由当前块的前驱进行编号
        if (op->code == LIR_OPCODE_PHI) {
            uint8_t number = ssa_new_var_number((lir_operand_var *) op->output->value, var_number_table,
                                                stack_table);
            ssa_rename_var((lir_operand_var *) op->output->value, number);

            current = current->succ;
            continue;
        }

        slice_t *vars = lir_operand_vars(op->first);
        if (vars->count > 0) {
            SLICE_FOR(vars, lir_operand_var) {
                lir_operand_var *var = SLICE_VALUE();
                var_number_stack *stack = table_get(stack_table, var->old);
                uint8_t number = stack->numbers[stack->count - 1];
                ssa_rename_var(var, number);
            }
        }

        vars = lir_operand_vars(op->second);
        if (vars->count > 0) {
            SLICE_FOR(vars, lir_operand_var) {
                lir_operand_var *var = SLICE_VALUE();
                var_number_stack *stack = table_get(stack_table, var->old);
                uint8_t number = stack->numbers[stack->count - 1];
                ssa_rename_var(var, number);
            }
        }

        if (op->output != NULL && op->output->type == LIR_OPERAND_VAR) {
            lir_operand_var *var = (lir_operand_var *) op->output->value;
            uint8_t number = ssa_new_var_number(var, var_number_table, stack_table); // 新增定义
            ssa_rename_var(var, number);
        }

        current = current->succ;
    }

    // phi body 编号
    // 遍历当前块的 cfg 后继为 phi body 编号, 前序遍历，默认也会从左往右遍历的，应该会满足的吧！
    // 最后是否所有的 phi_body 中的每一个值都会被命名引用，是否有遗漏？
    // 不会，如果 A->B->D / A->C->D / A -> F -> E -> D
    // 假设在 D 是 A 和 E 的支配边界，
    // 当且仅当 x = live_in(D) 时
    // D 中变量 x = phi(x of pred-B, x of pred-C，x of pred-E)
    // 当计算到 B 时，即使变量，没有在 B 中定义，只要函数的作用域还在，在 stack 中也一定能找到的变量重命名，无非是同名而已！！！
    // phi body 生成时是根据 block->pred count 生成的，block->pred-N 的索引 等于对应的 phi_body 的索引
    for (int i = 0; i < block->succs->count; ++i) {
        basic_block_t *succ_block = block->succs->take[i];
        // 为 每个 phi 函数的 phi param 命名
//        lir_op *succ_op = succ_block->operations->front->succ;
        list_node *succ_node = succ_block->operations->front->succ;
        while (succ_node->value != NULL && OP(succ_node)->code == LIR_OPCODE_PHI) {
            lir_op_t *succ_op = OP(succ_node);
            slice_t *phi_body = succ_op->first->value;
            // block 位于 succ 的 phi_body 的具体位置
            lir_operand_var *var = ssa_phi_body_of(phi_body, succ_block->preds, block);
            var_number_stack *stack = table_get(stack_table, var->ident);
            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);

            succ_node = succ_node->succ;
        }
    }

    // 深度遍历-前序遍历,支配树可达所有节点
    for (int i = 0; i < block->be_idom->count; ++i) {
        ssa_rename_basic(block->be_idom->take[i], var_number_table, stack_table);
    }

    // 子节点递归完毕需要回到父节点，然后去下一个兄弟节点
    // 此时如果父节点定义了 x (1), 在左子节点重新定义 了 x (2), 如果在右子节点有 b = x + 1, 然后又有 x = c + 2
    // 此时 stack[x].top = 2;  但实际上右子节点使用的是 x1, 所以此时需要探出在左子节点定义的所有变量的 stack 空间。
    // 右子节点则由 b_1 = x_1 + 1, 而对于 x = c + 2, 则应该是 x_3 = c_1 + 2, 所以 counter 计数不能减少
    list_node *current_node = block->operations->front->succ;
    while (current_node->value != NULL) {
        lir_op_t *current_op = current_node->value;
        if (current_op->output != NULL && current_op->output->type == LIR_OPERAND_VAR) {
            lir_operand_var *var = (lir_operand_var *) current_op->output->value;

            // pop stack
            var_number_stack *stack = table_get(stack_table, var->old);
            stack->count--;
        }
        current_node = current_node->succ;
    }
}

/**
 * number 增加
 * @param var
 * @param var_number_table
 * @param stack_table
 * @return
 */
uint8_t ssa_new_var_number(lir_operand_var *var, table_t *var_number_table, table_t *stack_table) {
    uint8_t *value = table_get(var_number_table, var->old);
    var_number_stack *stack = table_get(stack_table, var->old);

    uint8_t result = *value;
    *value += 1;

    table_set(var_number_table, var->old, value);
    stack->numbers[stack->count++] = result;

    return result;
}

void ssa_rename_var(lir_operand_var *var, uint8_t number) {
    // 1: '\0'
    // 2: '_12'
    char *buf = (char *) malloc(strlen(var->ident) + sizeof(uint8_t) + 3);
    sprintf(buf, "%s.s%d", var->ident, number);
    var->ident = buf; // 已经分配在了堆中，需要手动释放了
}

bool ssa_is_idom(slice_t *dom, basic_block_t *await) {
    for (int i = 0; i < dom->count; ++i) {
        basic_block_t *item = dom->take[i];
        if (item->label_index == await->label_index) {
            continue;
        }

        // 判断是否包含
        if (!lir_blocks_contains(item->dom, await->label_index)) {
            return false;
        }
    }

    return true;
}

/**
 * @param var
 * @param block
 * @return
 */
bool ssa_phi_defined(lir_operand_var *var, basic_block_t *block) {
    list_node *current = block->operations->front->succ;
    while (current->value != NULL && ((lir_op_t *) current->value)->code == LIR_OPCODE_PHI) {
        lir_op_t *op = current->value;
        lir_operand_var *phi_var = op->output->value;
        if (strcmp(phi_var->ident, var->ident) == 0) {
            return true;
        }

        current = current->succ;
    }

    return false;
}

bool ssa_var_belong(lir_operand_var *var, slice_t *vars) {
    SLICE_FOR(vars, lir_operand_var) {
        if (strcmp((SLICE_VALUE())->ident, var->ident) == 0) {
            return true;
        }
    }

    return false;
}

lir_operand_var *ssa_phi_body_of(slice_t *phi_body, slice_t *preds, basic_block_t *guide) {
    int index = -1;
    for (int i = 0; i < preds->count; ++i) {
        basic_block_t *p = preds->take[i];
        if (p == guide) {
            index = i;
        }
    }
    assert(index >= 0 && "preds not contain guide");
    assert(phi_body->count > index);

    return phi_body->take[index];
}

void live_remove(table_t *t, slice_t *lives, lir_operand_var *var) {
    if (!table_exist(t, var->ident)) {
        return;
    }

    for (int i = 0; i < lives->count; ++i) {
        lir_operand_var *item = lives->take[i];
        if (str_equal(item->ident, var->ident)) {
            slice_remove(lives, i);
            table_delete(t, var->ident);
            break;
        }
    }
}

void live_add(table_t *t, slice_t *lives, lir_operand_var *var) {
    if (table_exist(t, var->ident)) {
        return;
    }

    slice_push(lives, var);
    table_set(t, var->ident, var);
}




