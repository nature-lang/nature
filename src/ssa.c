#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ssa.h"
#include "src/debug/debug.h"

/**
 *  如果 self 被除了 await 和 self 外的其他所有 block 支配，那这个节点就是 await 的最近支配者
 * @param be_doms
 * @param self_id
 * @param await_id
 * @return
 */
static bool self_is_imm_dom(slice_t *be_doms, basic_block_t *self, uint64_t await_id) {
    for (int i = 0; i < be_doms->count; ++i) {
        basic_block_t *item = be_doms->take[i];
        if (item->id == self->id || item->id == await_id) {
            continue;
        }

        // 测试 self 是否被 item 所支配, 只要没有，那 self 就不会是 await 的最近支配者
        if (!lir_blocks_contains(self->domers, item->id)) {
            return false;
        }
    }

    return true;
}

/**
 * 计算 block 中的直接支配者
 * @param doms
 * @param self
 * @return
 */
static basic_block_t *calc_imm_domer(slice_t *doms, basic_block_t *self) {
    for (int i = 0; i < doms->count; ++i) {
        basic_block_t *dom = doms->take[i];

        // self 不能作为自己的的最近支配节点
        if (dom->id == self->id) {
            continue;
        }

        // 判断 item 是否被除了 [await->id 和 self->id 以外的所有 id 所支配]
        if (self_is_imm_dom(doms, dom, self->id)) {
            return dom;
        }
    }
    assertf(false, "block=%s must have one imm dom", self->name);
    exit(1);
}

/**
 * @param c
 */
void ssa(closure_t *c) {
    // 计算每个 basic_block 支配者，一个基本块可以被多个父级基本块支配
    ssa_domers(c);

    // 计算最近支配者
    ssa_imm_domer(c);

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
void ssa_domers(closure_t *c) {
    // 初始化, domers[n0] = {l0}
    basic_block_t *basic_block;
    basic_block = c->blocks->take[0];
    slice_push(basic_block->domers, basic_block);

    // 初始化其他 domers 为所有节点的集合 {B0,B1,B2,B3..}
    for (int i = 1; i < c->blocks->count; ++i) {
        slice_t *other = slice_new(); // basic_block_t

        // Dom[i] = N
        for (int k = 0; k < c->blocks->count; ++k) {
            slice_push(other, c->blocks->take[k]);
        }
        ((basic_block_t *) c->blocks->take[i])->domers = other;
    }

    // 求不动点
    bool changed = true;
    while (changed) {
        changed = false;

        // domers[0] 自己支配自己，没必要进一步深挖了,所以从 1 开始遍历
        for (int id = 1; id < c->blocks->count; ++id) {
            basic_block_t *block = c->blocks->take[id];
            slice_t *new_dom = ssa_calc_dom_blocks(c, block);
            // 判断 domers 是否不同
            if (ssa_dom_changed(block->domers, new_dom)) {
                changed = true;
                block->domers = new_dom;
            }
        }
    }
}

/**
 * 计算最近支配点
 * 在支配 p 的点中，若一个支配点 i (i != p)， i 被 p 剩下其他的所有的支配点支配，则称 i 为 p 的最近支配点 imm_domer
 * B0 没有 imm_domer, 其他所有节点至少一个一个除了自身意外的最小支配者！
 * imm_domer 一定是父节点中的某一个
 * @param c
 */
void ssa_imm_domer(closure_t *c) {
    // 计算最近支配者 B0 没有支配者,所以直接跳过 0， 从 1 开始算
    for (int index = 1; index < c->blocks->count; ++index) {
        basic_block_t *block = c->blocks->take[index];

        // 当前块有多个支配者，但是只能有一个直接支配者
        basic_block_t *imm_domer = calc_imm_domer(block->domers, block);
        block->imm_domer = imm_domer;
        // 反向关联关系，用来构建支配者树使用
        slice_push(imm_domer->imm_domees, block);
    }
}

/**
 * 计算支配边界
 * 定义：n 支配 m 的前驱 p，且 n 不严格支配 m (即允许 n = m), 则 m 是 n 的支配边界
 * (极端情况是会出现 n 的支配者自身的前驱)  http://asset.eienao.com/image-20210802183805691.png
 * 对定义进行反向推理可以得到
 *
 * 如果 m 为汇聚点，对于 m 的任意前驱 p, p 一定会支配自身，且不支配 m. 所以一定有 DF(p) = m
 * n in Dom(p) (支配着 p 的节点 n), 则有 n 支配 p, 如果 n 不支配 m, 则一定有 DF(p) = m
 */
void ssa_df(closure_t *c) {
    for (int index = 0; index < c->blocks->count; ++index) {
        basic_block_t *current = c->blocks->take[index];
        // 只有多条 edge 的汇聚点才能是支配边界
        if (current->preds->count < 2) {
            continue;
        }

        for (int i = 0; i < current->preds->count; ++i) {
            basic_block_t *runner = current->preds->take[i];

            // 在上面限制的汇聚限制情况下,只要 pred 不是 当前块的直接支配者, pred 的支配边界就一定包含着 current
            // 是否存在 current.imm_domer != pred, 但是 current.domer 包含 pred?
            // 不可能， 因为从 current <-> pred <-> pred.imm_domer
            // pred 和 pred.imm_domer 之间如果存在节点直接支配 current,那么其一定会支配 pred，则其就是 pred.imm_domer
            // 只要 prev_runner 没有支配 current， 则 prev_runner 的支配边界就是 current
            while (runner->id != current->imm_domer->id) {
                slice_push(runner->df, current);

                if (runner->imm_domer->id == runner->id) {
                    assertf(false, "block=%s imm_domer is current", runner->name);
                }

                // 向上查找
                runner = runner->imm_domer;
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
    for (int id = 0; id < c->blocks->count; ++id) {
        slice_t *out = slice_new();
        slice_t *in = slice_new();
        ((basic_block_t *) c->blocks->take[id])->live_out = out;
        ((basic_block_t *) c->blocks->take[id])->live_in = in;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int id = c->blocks->count - 1; id >= 0; --id) {
            basic_block_t *block = c->blocks->take[id];
            slice_t *new_live_out = ssa_calc_live_out(c, c->blocks->take[id]);
            if (ssa_live_changed(block->live_out, new_live_out)) {
                changed = true;
                block->live_out = new_live_out;
            }

            slice_t *new_live_in = ssa_calc_live_in(c, c->blocks->take[id]);
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
    // 跳过没有插入过 phi 的节点的 var，这些节点不需要重新 name
    slice_t *added_phi_globals = slice_new();
    table_t *added_phi_global_table = table_new();

    for (int i = 0; i < c->ssa_globals->count; ++i) {
        lir_var_t *var = c->ssa_globals->take[i];
        table_t *inserted = table_new(); // key is block name

        linked_t *work_list = table_get(c->ssa_var_blocks, var->ident);
        assertf(work_list, "var '%s' has use, but lack def");
        while (!linked_empty(work_list)) {
            basic_block_t *var_def_block = linked_pop(work_list);
            for (int j = 0; j < var_def_block->df->count; ++j) {
                basic_block_t *df_block = var_def_block->df->take[j];
                if (table_exist(inserted, df_block->name)) {
                    continue;
                }

                // 变量 a 在虽然在 var_def_block 中进行了定义，但是可能在 df_block 中已经不在活跃了(live_in)
                // 此时不需要在 df 中插入 phi
                if (!ssa_var_belong(var, df_block->live_in)) {
                    continue;
                }

                lir_operand_t *output_param = operand_new(LIR_OPERAND_VAR, lir_var_new(c->module, var->ident));
                lir_operand_t *body_param = lir_new_phi_body(c->module, var, df_block->preds->count);
                lir_op_t *phi_op = lir_op_new(LIR_OPCODE_PHI, body_param, NULL, output_param);
                // insert to list(可能只有一个 label )
                linked_node *label_node = df_block->operations->front;
                linked_insert_after(df_block->operations, label_node, phi_op);
                table_set(inserted, df_block->name, df_block);

                if (!table_exist(added_phi_global_table, var->ident)) {
                    slice_push(added_phi_globals, var);
                    table_set(added_phi_global_table, var->ident, var);
                }

                // df_block to work list, 起到一个向后传播 phi def 到作用
                linked_push(work_list, df_block);
            }
        }
    }

    c->ssa_globals = added_phi_globals;
    c->ssa_globals_table = added_phi_global_table;
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
            lir_var_t *var = succ->live_in->take[k];
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

    SLICE_FOR(block->use) {
        lir_var_t *var = SLICE_VALUE(block->use);
        if (table_exist(exist_var, var->ident)) {
            continue;
        }

        slice_push(live_in, var);
        table_set(exist_var, var->ident, var);
    }

    SLICE_FOR(block->live_out) {
        lir_var_t *var = SLICE_VALUE(block->live_out);
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
        string ident = ((lir_var_t *) old->take[i])->ident;
        table_set(var_count, ident, old->take[i]);
    }

    // double count 判断同一个变量出现的次数,因为可能出现 new.count > old.count 的情况
    uint8_t double_count = 0;
    for (int i = 0; i < new->count; ++i) {
        string ident = ((lir_var_t *) new->take[i])->ident;
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
 * block.use 在 block 中重新定值之前就开始使用的变量
 * block.def 在 block 中定义的所有变量的集合
 */
void ssa_use_def(closure_t *c) {
    // 可能出现 B0 处未定义，但是直接使用,也需要计入到符号表中
    table_t *exist_global_vars = table_new();

    for (int id = 0; id < c->blocks->count; ++id) {
        slice_t *use = slice_new();
        slice_t *def = slice_new();

        table_t *exist_use = table_new();
        table_t *exist_def = table_new();

        basic_block_t *block = c->blocks->take[id];

        LINKED_FOR(block->operations) {
            lir_op_t *op = LINKED_VALUE();

            // first param (use)
            slice_t *vars = extract_var_operands(op, FLAG(LIR_FLAG_USE));
            for (int i = 0; i < vars->count; ++i) {
                lir_var_t *var = vars->take[i];
                // 不在当前块中定义，但是在当前块中使用的变量
                bool is_def = ssa_var_belong(var, def);
                if (!is_def && !table_exist(exist_use, var->ident)) {
                    slice_push(use, var);
                    table_set(exist_use, var->ident, var);

                    // 当前 var 一定已经在 prev 中定义过了，所以其肯定跨越了多个块
                    if (!table_exist(c->ssa_globals_table, var->ident)) {
                        slice_push(c->ssa_globals, var);
                        table_set(c->ssa_globals_table, var->ident, var);
                    }
                }
            }

            // def
            vars = extract_var_operands(op, FLAG(LIR_FLAG_DEF));
            for (int i = 0; i < vars->count; ++i) {
                lir_var_t *var = vars->take[i];
                // 记录所有的定值。
                if (!table_exist(exist_global_vars, var->ident)) {
                    slice_push(c->var_defs, var);
                    table_set(exist_global_vars, var->ident, var);
                }

                if (!table_exist(exist_def, var->ident)) {
                    slice_push(def, var);
                    table_set(exist_use, var->ident, var);
                }

                char *exists_key = str_connect(var->ident, block->name);
                if (!table_exist(c->ssa_var_blocks, exists_key)) {
                    linked_t *var_blocks = table_get(c->ssa_var_blocks, var->ident);
                    if (!var_blocks) {
                        var_blocks = linked_new();
                        table_set(c->ssa_var_blocks, var->ident, var_blocks);
                    }

                    linked_push(var_blocks, block);
                    table_set(c->ssa_var_block_exists, exists_key, var);
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
        if (((basic_block_t *) old_dom->take[i])->id != ((basic_block_t *) new_dom->take[i])->id) {
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
    uint8_t block_label_count[UINT16_MAX] = {0};
    for (int id = 0; id < c->blocks->count; ++id) {
        block_label_count[id] = 0;
    }

    for (int i = 0; i < block->preds->count; ++i) {
        // 找到 pred
        slice_t *pred_dom = ((basic_block_t *) block->preds->take[i])->domers;

        // 遍历 pred_dom 为 label 计数
        for (int k = 0; k < pred_dom->count; ++k) {
            block_label_count[((basic_block_t *) pred_dom->take[k])->id]++;
        }
    }

    // 如果 block 的count 和 preds_count 的数量一致则表示该基本块支配了所有的前驱
    // domers 严格按照 label 从小到大排序, 且 block 自身一定是支配自身的
    for (int id = 0; id < c->blocks->count; ++id) {
        if (block_label_count[id] == block->preds->count || id == block->id) {
            slice_push(dom, c->blocks->take[id]);
        }
    }

    return dom;
}

// 前序遍历各个基本块
void ssa_rename(closure_t *c) {
    table_t *var_number_table = table_new(); // def 使用，用于记录当前应该命名为多少
    table_t *stack_table = table_new(); // use 使用，判断使用的变量的名称

    // 遍历所有变量,进行初始化
    SLICE_FOR(c->var_defs) {
        lir_var_t *var = SLICE_VALUE(c->var_defs);
        uint8_t *number = NEW(uint8_t);
        *number = 0;

        var_number_stack *stack = NEW(var_number_stack);
        stack->count = 0;

        table_set(var_number_table, var->old, number);
        table_set(stack_table, var->old, stack);
    }

    // 从根开始更名(rename 就相当于创建了一个新的变量)
    ssa_rename_block(c, c->entry, var_number_table, stack_table);
}

void ssa_rename_block(closure_t *c, basic_block_t *block, table_t *var_number_table, table_t *stack_table) {
    // skip label code
    linked_node *current = block->operations->front->succ;

    // 遍历块中的所有 var 进行重命名，如果符号不是 ssa_global, 则保持原封不动
    while (current->value != NULL) {
        lir_op_t *op = current->value;
        // phi body 由当前块的前驱进行编号
        if (op->code == LIR_OPCODE_PHI) {
            uint8_t number = ssa_new_var_number((lir_var_t *) op->output->value, var_number_table,
                                                stack_table);
            ssa_rename_var((lir_var_t *) op->output->value, number);

            current = current->succ;
            continue;
        }

        // use
        slice_t *vars = extract_var_operands(op, FLAG(LIR_FLAG_USE));
        for (int i = 0; i < vars->count; ++i) {
            lir_var_t *var = vars->take[i];

            var_number_stack *stack = table_get(stack_table, var->old);
            assert(stack);
            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);
        }

        vars = extract_var_operands(op, FLAG(LIR_FLAG_DEF));
        for (int i = 0; i < vars->count; ++i) {
            lir_var_t *var = vars->take[i];

            uint8_t number = ssa_new_var_number(var, var_number_table, stack_table); // 新增定义
            ssa_rename_var(var, number);
        }

        current = current->succ;
    }

    // phi body 编号(之前已经放置好了 phi body，只是还没有编号)
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
//        lir_op_t *succ_op = succ_block->asm_operations->front->succ;
        linked_node *op_node = linked_first(succ_block->operations)->succ; // front is label
        while (op_node->value != NULL && OP(op_node)->code == LIR_OPCODE_PHI) {
            lir_op_t *op = OP(op_node);
            slice_t *phi_body = op->first->value;
            // block 位于 succ 的 phi_body 的具体位置
            lir_var_t *var = ssa_phi_body_of(phi_body, succ_block->preds, block);
            var_number_stack *stack = table_get(stack_table, var->ident);
            assert(stack);
            assert(stack->count > 0);

            uint8_t number = stack->numbers[stack->count - 1];
            ssa_rename_var(var, number);

            op_node = op_node->succ;
        }
    }

    // 基于支配树进行 深度遍历-前序遍历
    for (int i = 0; i < block->imm_domees->count; ++i) {
        ssa_rename_block(c, block->imm_domees->take[i], var_number_table, stack_table);
    }

    // 子节点递归完毕需要回到父节点，然后去下一个兄弟节点
    // 此时如果父节点定义了 x (1), 在左子节点重新定义 了 x (2), 如果在右子节点有 b = x + 1, 然后又有 x = c + 2
    // 此时 stack[x].top = 2;  但实际上右子节点使用的是 x1, 所以此时需要探出在左子节点定义的所有变量的 stack 空间。
    // 右子节点则由 b_1 = x_1 + 1, 而对于 x = c + 2, 则应该是 x_3 = c_1 + 2, 所以 counter 计数不能减少
    linked_node *current_node = block->operations->front->succ;
    while (current_node->value != NULL) {
        lir_op_t *op = current_node->value;
        // output var
        slice_t *vars = extract_var_operands(op, FLAG(LIR_FLAG_DEF));
        for (int i = 0; i < vars->count; ++i) {
            lir_var_t *var = vars->take[i];
            if (!table_exist(c->ssa_globals_table, var->old)) {
                continue;
            }

            // pop stack
            var_number_stack *stack = table_get(stack_table, var->old);
            assertf(stack, "var %s not found in stack table", var->old);
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
uint8_t ssa_new_var_number(lir_var_t *var, table_t *var_number_table, table_t *stack_table) {
    uint8_t *value = table_get(var_number_table, var->old);
    var_number_stack *stack = table_get(stack_table, var->old);
    assert(stack);

    uint8_t result = *value;
    *value += 1;

    table_set(var_number_table, var->old, value);
    stack->numbers[stack->count++] = result;

    return result;
}

void ssa_rename_var(lir_var_t *var, uint8_t number) {
    // 1: '\0'
    // 2: '_12'
    char *buf = (char *) malloc(strlen(var->ident) + sizeof(uint8_t) + 3);
    sprintf(buf, "%s.s%d", var->ident, number);
    var->ident = buf; // 已经分配在了堆中，需要手动释放了
}


/**
 * @param var
 * @param block
 * @return
 */
bool ssa_phi_defined(lir_var_t *var, basic_block_t *block) {
    linked_node *current = block->operations->front->succ;
    while (current->value != NULL && ((lir_op_t *) current->value)->code == LIR_OPCODE_PHI) {
        lir_op_t *op = current->value;
        lir_var_t *phi_var = op->output->value;
        if (strcmp(phi_var->ident, var->ident) == 0) {
            return true;
        }

        current = current->succ;
    }

    return false;
}

bool ssa_var_belong(lir_var_t *var, slice_t *vars) {
    SLICE_FOR(vars) {
        lir_var_t *item = SLICE_VALUE(vars);
        if (strcmp(item->ident, var->ident) == 0) {
            return true;
        }
    }

    return false;
}

lir_var_t *ssa_phi_body_of(slice_t *phi_body, slice_t *preds, basic_block_t *guide) {
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

void live_remove(table_t *t, slice_t *lives, lir_var_t *var) {
    if (!table_exist(t, var->ident)) {
        return;
    }

    for (int i = 0; i < lives->count; ++i) {
        lir_var_t *item = lives->take[i];
        if (str_equal(item->ident, var->ident)) {
            slice_remove(lives, i);
            break;
        }
    }
    table_delete(t, var->ident);
}

void live_add(table_t *t, slice_t *lives, lir_var_t *var) {
    if (table_exist(t, var->ident)) {
        return;
    }

    slice_push(lives, var);
    table_set(t, var->ident, var);
}




