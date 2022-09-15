#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ssa.h"

/**
 * @param c
 */
void ssa(closure *c) {
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
void ssa_dom(closure *c) {
    // 初始化, dom[n0] = {l0}
    lir_basic_blocks dom = {.count = 1};
    c->blocks.list[0]->dom.count = 1;
    c->blocks.list[0]->dom.list[0] = c->blocks.list[0];

    // 初始化其他 dom 为所有节点的集合 {B0,B1,B2,B3..}
    for (int i = 1; i < c->blocks.count; ++i) {
        lir_basic_blocks other = {.count = 0};

        // Dom[i] = N
        for (int k = 0; k < c->blocks.count; ++k) {
            other.list[other.count++] = c->blocks.list[k];
        }

        c->blocks.list[i]->dom = other;
    }

    // 求不动点
    bool changed = true;
    while (changed) {
        changed = false;

        // dom[0] 自己支配自己，没必要进一步深挖了,所以从 1 开始遍历
        for (int label = 1; label < c->blocks.count; ++label) {
            lir_basic_blocks new_dom = ssa_calc_dom_blocks(c, c->blocks.list[label]);
            // 判断 dom 是否不同
            if (ssa_dom_changed(&c->blocks.list[label]->dom, &new_dom)) {
                changed = true;
                c->blocks.list[label]->dom = new_dom;
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
void ssa_idom(closure *c) {
    // 初始化 be_idom(支配者树)
    for (int label = 0; label < c->blocks.count; ++label) {
        lir_basic_blocks be_idom = {.count = 0};
        c->blocks.list[label]->be_idom = be_idom; // 一个基本块可以是节点的 idom, be_idom 用来构造支配者树
    }

    // 计算最近支配者 B0 没有支配者
    for (int label = 1; label < c->blocks.count; ++label) {
        lir_basic_block *block = c->blocks.list[label];
        lir_basic_blocks dom = block->dom;
        // 最近支配者不能是自身
        for (int i = dom.count - 1; i >= 0; --i) {
            if (dom.list[i]->label == block->label) {
                continue;
            }

            if (ssa_is_idom(dom, dom.list[i])) {
                block->idom = dom.list[i];

                // 添加反向关联关系
                block->idom->be_idom.list[block->idom->be_idom.count++] = block;
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
void ssa_df(closure *c) {
    // 初始化空集为默认行为，不需要特别声明
//  for (int label = 0; label < c->blocks.count; ++label) {
//    lir_basic_blocks df = {.count = 0};
//    c->blocks.list[label]->df = df;
//  }

    for (int label = 0; label < c->blocks.count; ++label) {
        lir_basic_block *current_block = c->blocks.list[label];
        // 非汇聚点不能是支配边界
        if (current_block->preds.count < 2) {
            continue;
        }

        for (int i = 0; i < current_block->preds.count; ++i) {
            lir_basic_block *runner = current_block->preds.list[i];

            // 只要 pred 不是 当前块的最近支配者, pred 的支配边界就一定包含着 current_block
            // 是否存在 idom[current_block] != pred, 但是 dom[current_block] = pred?
            // 不可能， 因为是从 current_block->pred->idom(pred)
            // pred 和 idom(pred) 之间如果存在节点支配 current,那么其一定会支配 current->pred，则其就是 idom(pred)
            while (runner->label != current_block->idom->label) {
                runner->df.list[runner->df.count++] = current_block;

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
void ssa_live(closure *c) {
    // 初始化 live out 每个基本块为 ∅
    for (int label = 0; label < c->blocks.count; ++label) {
        lir_vars out = {.count=0};
        lir_vars in = {.count=0};
        c->blocks.list[label]->live_out = out;
        c->blocks.list[label]->live_in = in;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int label = c->blocks.count - 1; label >= 0; --label) {
            lir_basic_block *block = c->blocks.list[label];
            lir_vars new_live_out = ssa_calc_live_out(c, c->blocks.list[label]);
            if (ssa_live_changed(&block->live_out, &new_live_out)) {
                changed = true;
                block->live_out = new_live_out;
            }

            lir_vars new_live_in = ssa_calc_live_in(c, c->blocks.list[label]);
            if (ssa_live_changed(&block->live_in, &new_live_in)) {
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
void ssa_add_phi(closure *c) {
    for (int label = 0; label < c->blocks.count; ++label) {
        // 定义的每个变量都需要添加到支配边界中
        lir_vars def = c->blocks.list[label]->def;
        lir_basic_blocks df = c->blocks.list[label]->df;

        for (int i = 0; i < def.count; ++i) {
            lir_operand_var *var = def.list[i];

            for (int k = 0; k < df.count; ++k) {
                lir_basic_block *df_block = df.list[k];
                // 判断该变量是否已经添加过 phi(另一个分支可能会先创建), 创建则跳过
                if (ssa_phi_defined(var, df_block)) {
                    continue;
                }

                // 如果 block 中的 def 变量不在当前 B in df 入口活跃,则不需要定义
                if (!ssa_var_belong(var, df_block->live_in)) {
                    continue;
                }

                // add phi (x1, x2, x3) => x
                lir_operand *result_param = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, LIR_NEW_VAR_OPERAND(var->ident));
                lir_operand *first_param = lir_new_phi_body(var, df_block->preds.count);
                lir_op *phi_op = lir_op_new(LIR_OP_TYPE_PHI, first_param, NULL, result_param);


                // insert to list(可能只有一个 label )
                list_node *label_node = df_block->operates->front;
                list_splice(df_block->operates, label_node, phi_op);
            }
        }
    }
}

/**
 * live out 为 n 的所有后继的 live_in 的并集
 */
lir_vars ssa_calc_live_out(closure *c, lir_basic_block *block) {
    lir_vars live_out = {.count = 0};
    table *exist_var = table_new(); // basic var ident

    for (int i = 0; i < block->succs.count; ++i) {
        lir_basic_block *succ = block->succs.list[i];

        // 未在 succ 中被重新定义(def)，且离开 succ 后继续活跃的变量
        for (int k = 0; k < succ->live_in.count; ++k) {
            lir_operand_var *var = succ->live_in.list[k];
            if (table_exist(exist_var, var->ident)) {
                continue;
            }
            live_out.list[live_out.count++] = var;
            table_set(exist_var, var->ident, var);
        }
    }

//    table_free(exist_var);
    return live_out;
}

/**
 * 在当前块使用的变量 + 离开当前块依旧活跃的变量（这些变量未在当前块重新定义）
 */
lir_vars ssa_calc_live_in(closure *c, lir_basic_block *block) {
    lir_vars live_in = {.count = 0};
    table *exist_var = table_new(); // basic var ident

    for (int i = 0; i < block->use.count; ++i) {
        lir_operand_var *var = block->use.list[i];
        if (table_exist(exist_var, var->ident)) {
            continue;
        }

        live_in.list[live_in.count++] = var;
        table_set(exist_var, var->ident, var);
    }

    for (int i = 0; i < block->live_out.count; ++i) {
        lir_operand_var *var = block->live_out.list[i];
        if (table_exist(exist_var, var->ident)) {
            continue;
        }

        // 是否是当前块中定义的变量。
        if (ssa_var_belong(var, block->def)) {
            continue;
        }

        live_in.list[live_in.count++] = var;
        table_set(exist_var, var->ident, var);
    }

//    table_free(exist_var);
    return live_in;
}

/**
 * 无顺序比较，所以需要用到 hash 表
 * @param old
 * @param new
 * @return
 */
bool ssa_live_changed(lir_vars *old, lir_vars *new) {
    if (old->count != new->count) {
        return true;
    }
    table *var_count = table_new();
    for (int i = 0; i < old->count; ++i) {
        string ident = old->list[i]->ident;
        table_set(var_count, ident, old->list[i]);
    }

    // double count 判断同一个变量出现的次数,因为可能出现 new.count > old.count 的情况
    uint8_t double_count = 0;
    for (int i = 0; i < new->count; ++i) {
        string ident = new->list[i]->ident;
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
 * use(m) 在 m 中重新定义之前就开始使用的变量
 * def(m) 在 m 中定义的所有变量的集合
 */
void ssa_use_def(closure *c) {
    // 可能出现 B0 处未定义，但是直接使用,也需要计入到符号表中
    table *exist_var = table_new();

    for (int label = 0; label < c->blocks.count; ++label) {
        lir_vars use = {.count=0};
        lir_vars def = {.count=0};

        table *exist_use = table_new();
        table *exist_def = table_new();

        lir_basic_block *block = c->blocks.list[label];

        list_node *current = block->operates->front;
        while (current->value != NULL) {
            lir_op *op = current->value;
            // first param (use)
            if (op->first != NULL && op->first->type == LIR_OPERAND_TYPE_VAR) {
                lir_operand_var *var = (lir_operand_var *) op->first->value;
                bool is_def = ssa_var_belong(var, def);
                if (!is_def && !table_exist(exist_use, var->ident)) {
                    use.list[use.count++] = var;
                    table_set(exist_use, var->ident, var);
                }

                if (!table_exist(exist_var, var->ident)) {
                    c->globals.list[c->globals.count++] = var;
                    table_set(exist_var, var->ident, var);
                }
            }

            // second param (use)
            if (op->second != NULL && op->second->type == LIR_OPERAND_TYPE_VAR) {
                lir_operand_var *var = (lir_operand_var *) op->second->value;
                bool is_def = ssa_var_belong(var, def);
                if (!is_def && !table_exist(exist_use, var->ident)) {
                    use.list[use.count++] = var;
                    table_set(exist_use, var->ident, var);
                }

                if (!table_exist(exist_var, var->ident)) {
                    c->globals.list[c->globals.count++] = var;
                    table_set(exist_var, var->ident, var);
                }
            }

            // def
            if (op->result != NULL && op->result->type == LIR_OPERAND_TYPE_VAR) {
                lir_operand_var *var = (lir_operand_var *) op->result->value;
                if (!table_exist(exist_def, var->ident)) {
                    def.list[def.count++] = var;
                    table_set(exist_use, var->ident, var);
                }

                if (!table_exist(exist_var, var->ident)) {
                    c->globals.list[c->globals.count++] = var;
                    table_set(exist_var, var->ident, var);
                }
            }

            current = current->next;
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
bool ssa_dom_changed(lir_basic_blocks *old_dom, lir_basic_blocks *new_dom) {
    if (old_dom->count != new_dom->count) {
        return true;
    }

    for (int i = 0; i < old_dom->count; ++i) {
        if (old_dom->list[i]->label != new_dom->list[i]->label) {
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
lir_basic_blocks ssa_calc_dom_blocks(closure *c, lir_basic_block *block) {
    lir_basic_blocks dom = {.count = 0};

    // 遍历当前 block 的 preds 的 dom_list, 然后求交集
    // 如果一个基本块支配者每一个前驱，那么其数量等于前驱的数量
    uint8_t block_label_count[UINT8_MAX];
    for (int label = 0; label < c->blocks.count; ++label) {
        block_label_count[label] = 0;
    }

    for (int i = 0; i < block->preds.count; ++i) {
        // 找到 pred
        lir_basic_blocks pred_dom = block->preds.list[i]->dom;

        // 遍历 pred_dom 为 label 计数
        for (int k = 0; k < pred_dom.count; ++k) {
            block_label_count[pred_dom.list[k]->label]++;
        }
    }

    // 如果 block 的count 和 preds_count 的数量一致则表示该基本块支配了所有的前驱
    // dom 严格按照 label 从小到大排序, 且 block 自身一定是支配自身的
    for (int label = 0; label < c->blocks.count; ++label) {
        if (block_label_count[label] == block->preds.count || label == block->label) {
            dom.list[dom.count++] = c->blocks.list[label];
        }
    }

    return dom;
}

// 前序遍历各个基本块
void ssa_rename(closure *c) {
    table *var_number_table = table_new(); // def 使用，用于记录当前应该命名为多少
    table *stack_table = table_new(); // use 使用，判断使用的变量的名称

    // 遍历所有名字变量,进行初始化
    for (int i = 0; i < c->globals.count; ++i) {
        lir_operand_var *var = c->globals.list[i];
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
    for (int i = 0; i < c->globals.count; ++i) {
        lir_operand_var *var = c->globals.list[i];
        uint8_t *number = table_get(var_number_table, var->old);
        var_number_stack *stack = table_get(stack_table, var->old);
        free(number);
        free(stack);
    }
//    table_free(var_number_table);
//    table_free(stack_table);
}

void ssa_rename_basic(lir_basic_block *block, table *var_number_table, table *stack_table) {
    // skip label type
//    lir_op *current_op = block->operates->front->succ;
    list_node *current = block->operates->front->next;

    // 当前块内的先命名
    while (current->value != NULL) {
        lir_op *op = current->value;
        // phi body 由当前块的前驱进行编号
        if (op->type == LIR_OP_TYPE_PHI) {
            uint8_t number = ssa_new_var_number((lir_operand_var *) op->result->value, var_number_table,
                                                stack_table);
            ssa_rename_var((lir_operand_var *) op->result->value, number);

            current = current->next;
            continue;
        }

        if (op->first != NULL && op->first->type == LIR_OPERAND_TYPE_VAR) {
            lir_operand_var *var = (lir_operand_var *) op->first->value;
            var_number_stack *stack = table_get(stack_table, var->old);
            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);
        }

        if (op->second != NULL && op->second->type == LIR_OPERAND_TYPE_VAR) {
            lir_operand_var *var = (lir_operand_var *) op->second->value;
            var_number_stack *stack = table_get(stack_table, var->old);
            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);
        }

        if (op->result != NULL && op->result->type == LIR_OPERAND_TYPE_VAR) {
            lir_operand_var *var = (lir_operand_var *) op->result->value;
            uint8_t number = ssa_new_var_number(var, var_number_table, stack_table);
            ssa_rename_var(var, number);
        }

        current = current->next;
    }

    // 遍历当前块的 cfg 后继为 phi body 编号, 前序遍历，默认也会从左往右遍历的，应该会满足的吧！
    // 最后是否所有的 phi_body 中的每一个值都会被命名引用，是否有遗漏？
    // 不会，如果 A->B->D / A->C->D / A -> F -> E -> D
    // 假设在 D 是 A 和 E 的支配边界，
    // 当且仅当 x = live_in(D) 时
    // D 中变量 x = phi(x of pred-B, x of pred-C，x of pred-E)
    // 当计算到 B 时，即使变量，没有在 b 中定义，只要函数的作用域还在，在 stack 中也一定能找到的变量重命名，无非是同名而已！！！
    for (int i = 0; i < block->succs.count; ++i) {
        lir_basic_block *succ_block = block->succs.list[i];
        // 为 每个 phi 函数的 phi param 命名
//        lir_op *succ_op = succ_block->operates->front->succ;
        list_node *succ_node = succ_block->operates->front->next;
        while (succ_node->value != NULL && ((lir_op *) succ_node->value)->type == LIR_OP_TYPE_PHI) {
            lir_op *succ_op = succ_node->value;
            lir_operand_phi_body *phi_body = succ_op->first->value;
            lir_operand_var *var = phi_body->list[phi_body->count++];
            var_number_stack *stack = table_get(stack_table, var->ident);
            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);

            succ_node = succ_node->next;
        }
    }

    // 深度遍历-前序遍历,支配树可达所有节点
    for (int i = 0; i < block->be_idom.count; ++i) {
        ssa_rename_basic(block->be_idom.list[i], var_number_table, stack_table);
    }

    // 子节点递归完毕需要回到父节点，然后去下一个兄弟节点
    // 此时如果父节点定义了 x (1), 在左子节点重新定义 了 x (2), 如果在右子节点有 b = x + 1, 然后又有 x = c + 2
    // 此时 stack[x].top = 2;  但实际上右子节点使用的是 x1, 所以此时需要探出在左子节点定义的所有变量的 stack 空间。
    // 右子节点则由 b_1 = x_1 + 1, 而对于 x = c + 2, 则应该是 x_3 = c_1 + 2, 所以 counter 计数不能减少
    list_node *current_node = block->operates->front->next;
    while (current_node->value != NULL) {
        lir_op *current_op = current_node->value;
        if (current_op->result != NULL && current_op->result->type == LIR_OPERAND_TYPE_VAR) {
            lir_operand_var *var = (lir_operand_var *) current_op->result->value;

            // pop stack
            var_number_stack *stack = table_get(stack_table, var->old);
            stack->count--;
        }
        current_node = current_node->next;
    }
}

/**
 * number 增加
 * @param var
 * @param var_number_table
 * @param stack_table
 * @return
 */
uint8_t ssa_new_var_number(lir_operand_var *var, table *var_number_table, table *stack_table) {
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

bool ssa_is_idom(lir_basic_blocks dom, lir_basic_block *await) {
    for (int i = 0; i < dom.count; ++i) {
        lir_basic_block *item = dom.list[i];
        if (item->label == await->label) {
            continue;
        }

        // 判断是否包含
        if (!lir_blocks_contains(item->dom, await->label)) {
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
bool ssa_phi_defined(lir_operand_var *var, lir_basic_block *block) {
    list_node *current = block->operates->front->next;
    while (current->value != NULL && ((lir_op *) current->value)->type == LIR_OP_TYPE_PHI) {
        lir_op *op = current->value;
        lir_operand_var *phi_var = op->result->value;
        if (strcmp(phi_var->ident, var->ident) == 0) {
            return true;
        }

        current = current->next;
    }

    return false;
}

bool ssa_var_belong(lir_operand_var *var, lir_vars vars) {
    for (int i = 0; i < vars.count; ++i) {
        if (strcmp(vars.list[i]->ident, var->ident) == 0) {
            return true;
        }
    }

    return false;
}



