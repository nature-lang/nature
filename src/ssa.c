#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ssa.h"

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
    // 定义的每个变量
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

        // 如果变量不在当前 n ∈ df 入口活跃,则不需要定义
        if (!ssa_var_belong(var, df_block->live_in)) {
          continue;
        }

        // add phi
        lir_op *phi_op = lir_new_op(LIR_OP_TYPE_PHI);
        phi_op->result.type = LIR_OPERAND_TYPE_VAR;
        phi_op->result.value = lir_clone_operand_var(var);

        // param to first
        // 根据支配边界的前驱来决定生成的 phi body 的数量
        phi_op->first.type = LIR_OPERAND_TYPE_PHI_BODY;
        phi_op->first.value = lir_new_phi_body(var, df_block->preds.count);

        // insert to list
        lir_op *label_op = df_block->first_op;
        label_op->succ->pred = phi_op;
        phi_op->succ = label_op->succ;

        label_op->succ = phi_op;
        phi_op->pred = label_op;
      }
    }
  }
}

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
        block->live_out = new_live_out;
      }
    }
  }
}

// live out 为 n 的所有后继的 live_in 的并集
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

  table_free(exist_var);
  return live_out;
}

// 在当前块使用的变量 + 离开当前块依旧活跃的变量（这些变量未在当前块重新定义）
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

  table_free(exist_var);
  return live_in;
}

bool ssa_live_changed(lir_vars *old, lir_vars *new) {
  if (old->count != new->count) {
    return true;
  }
  table *var_count = table_new();
  for (int i = 0; i < old->count; ++i) {
    string ident = old->list[i]->ident;
    table_set(var_count, ident, old->list[i]);
  }

  // double count
  uint8_t double_count = 0;
  for (int i = 0; i < new->count; ++i) {
    string ident = new->list[i]->ident;
    void *has = table_get(var_count, ident);
    if (has == NULL) {
      table_free(var_count);
      return true;
    }

    double_count += 1;
  }

  if (double_count != new->count) {
    table_free(var_count);
    return true;
  }

  table_free(var_count);
  return false;
}

/**
 * UEVar(m) 在 m 中重新定义之前就开始使用的变量
 * VarKill(m) 在 m 中定义的所有变量的集合
 */
void ssa_use_def(closure *c) {
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_vars use = {.count=0};
    lir_vars def = {.count=0};

    table *exist_use = table_new();
    table *exist_def = table_new();

    lir_basic_block *block = c->blocks.list[label];

    lir_op *op = block->first_op;
    while (op != NULL) {
      // first param
      if (op->first.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->first.value;
        bool is_def = ssa_var_belong(var, def);
        if (!is_def && !table_exist(exist_use, var->ident)) {
          use.list[use.count++] = var;
          table_set(exist_use, var->ident, var);
        }
      }

      // second param
      if (op->second.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->second.value;
        bool is_def = ssa_var_belong(var, def);
        if (!is_def && !table_exist(exist_use, var->ident)) {
          use.list[use.count++] = var;
          table_set(exist_use, var->ident, var);
        }
      }

      if (op->result.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->result.value;
        if (!table_exist(exist_def, var->ident)) {
          def.list[def.count++] = var;
          table_set(exist_use, var->ident, var);
        }
      }

      op = op->succ;
    }

    block->use = use;
    block->def = def;

    table_free(exist_use);
    table_free(exist_def);
  }
}

// 计算支配边界
// 只有汇聚点(假设为 n)才能是支配边界，反证：如果一个点 n 只有一个前驱 j, 那么支配 j 则必定支配 n
// 对于 汇聚点 n 的每个前驱 pred_j, 只要其不是 n 的 idom, n 就是 pred_j' 的支配边界
// 同理对于 pred_j 的支配者 pred_j', 只要 pred_j' 不是 n的 idom, n 同样也是是 pred_j' 的支配边界
void ssa_df(closure *c) {
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_blocks df = {.count = 0};
    c->blocks.list[label]->df = df;
  }

  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_block *current_block = c->blocks.list[label];
    if (current_block->preds.count < 2) {
      continue;
    }

    for (int i = 0; i < current_block->preds.count; ++i) {
      lir_basic_block *pred = current_block->preds.list[i];
      // 只要 pred 不是 当前块的最近支配者, pred 的支配边界就一定包含着 current_block
      // 是否存在 idom[current_block] != pred, 但是 dom[current_block] = pred?
      // 不可能， 因为是从 current_block->pred->idom(pred)
      // pred 和 idom(pred) 之间如果存在节点支配 current,那么其一定会支配 current->pred，则其就是 idom(pred)
      while (pred->label != current_block->idom->label) {
        pred->df.list[pred->df.count++] = current_block;
        // 深度优先，进一步查找
        pred = pred->idom;
      }
    }
  }
}

// n0 没有 idom
// idom 一定是父节点中的某一个
// 由于采用中序遍历编号，所以父节点的 label 一定小于当前 label
// 当前 label 的多个支配者中 label 最小的一个就是 idom
void ssa_idom(closure *c) {
  // 初始化 be_idom
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_blocks be_idom = {.count = 0};
    c->blocks.list[label]->be_idom = be_idom;
  }
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_block *block = c->blocks.list[label];
    block->idom = block->dom.list[block->dom.count - -2];
    // 添加反向关系
    block->idom->be_idom.list[block->idom->be_idom.count] = block;
  }
}

// A -> B -> C
// A -> E -> F -> C
// 到达 C 必须要经过前驱 B,F 中的一个，如果一个节点即支配 B 也支配着 F，（此处是 A）
// 则 A 一定支配着 C, 即如果 A 支配着 C 的所有前驱，则 A 一定支配 C
void ssa_dom(closure *c) {
  // 初始化, dom[n0] = {l0}
  lir_basic_blocks dom = {.count = 0};
  dom.list[dom.count++] = c->blocks.list[0];
  c->blocks.list[0]->dom = dom;

  // 初始化其他 dom
  for (int i = 1; i < c->blocks.count; ++i) {
    lir_basic_blocks other = {.count = 0};

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

bool ssa_dom_changed(lir_basic_blocks *old, lir_basic_blocks *new) {
  if (old->count != new->count) {
    true;
  }

  // 因此是根据 block label 从小到大排序的，所以可以这么遍历
  for (int i = 0; i < old->count; ++i) {
    if (old->list[i]->label != new->list[i]->label) {
      return true;
    }
  }

  return false;
}

lir_basic_blocks ssa_calc_dom_blocks(closure *c, lir_basic_block *block) {
  lir_basic_blocks dom = {.count = 0};

  // 遍历当前 block 的 preds 的 dom_list, 然后求交集
  // key => label
  // value => a number of
  uint8_t block_label_count[UINT8_MAX];
  for (int i = 0; i < block->preds.count; ++i) {
    // 找到 pred
    lir_basic_block *pred = block->preds.list[i];
    // 通过 pred->label，从 dom_list 中找到对应的 dom
    lir_basic_blocks pred_dom = pred->dom;
    // 遍历 pred_dom 为 label 计数
    for (int k = 0; k < pred_dom.count; ++k) {
      block_label_count[pred_dom.list[k]->label]++;
    }
  }

  // 如果 block 的count 和 preds_count 的数量一致则表示全部相交，即
  for (int label = 0; label < c->blocks.count; ++label) {
    if (block_label_count[label] == block->preds.count) {
      dom.list[dom.count++] = c->blocks.list[label];
    }
  }

  // 由于 n 的支配者的 label 肯定小于 n, 所以最后添加 n,这样支配者就是按从小往大编号了
  // 从而便于比较，和找出 idom
  dom.list[dom.count++] = block;

  return dom;
}

// 前序遍历各个基本块
void ssa_rename(closure *c) {
  table *var_number_table = table_new();
  table *stack_table = table_new();
  // 遍历所有名字
  for (int i = 0; i < c->globals.count; ++i) {
    lir_operand_var *var = c->globals.list[i];
    uint8_t *number = malloc(sizeof(uint8_t));
    *number = 0;

    var_number_stack *stack = malloc(sizeof(var_number_stack));
    stack->count = 0;

    table_set(var_number_table, var->ident, number);
    table_set(stack_table, var->ident, stack);
  }

  // 从根开始更名
  ssa_rename_basic(c->entry, var_number_table, stack_table);

  // TODO 遍历释放其中的每个值,否则就是空悬指针啦
  table_free(var_number_table);
  table_free(stack_table);
}

void ssa_rename_basic(lir_basic_block *block, table *var_number_table, table *stack_table) {
  // skip label op
  lir_op *op = block->first_op->succ;

  // 当前块内的先命名
  while (op != NULL) {
    // phi body 由当前块的前驱进行编号
    if (op->type == LIR_OP_TYPE_PHI) {
      uint8_t number = ssa_new_var_number((lir_operand_var *) op->result.value, var_number_table, stack_table);
      ssa_rename_var((lir_operand_var *) op->result.value, number);

      op = op->succ;
      continue;
    }

    if (op->first.type == LIR_OPERAND_TYPE_VAR) {
      lir_operand_var *var = (lir_operand_var *) op->first.value;
      var_number_stack *stack = table_get(stack_table, var->ident);
      uint8_t number = stack->numbers[stack->count - 1];
      ssa_rename_var(var, number);
    }

    if (op->second.type == LIR_OPERAND_TYPE_VAR) {
      lir_operand_var *var = (lir_operand_var *) op->second.value;
      var_number_stack *stack = table_get(stack_table, var->ident);
      uint8_t number = stack->numbers[stack->count - 1];
      ssa_rename_var(var, number);
    }

    if (op->result.type == LIR_OPERAND_TYPE_VAR) {
      lir_operand_var *var = (lir_operand_var *) op->result.value;
      uint8_t number = ssa_new_var_number(var, var_number_table, stack_table);
      ssa_rename_var(var, number);
    }

    op = op->succ;
  }

  // 遍历当前块的 cfg 后继为 phi body 编号, 前序遍历，默认也会从左往右遍历的，应该会满足的吧！
  // 最后是否所有的 phi_body 中的每一个值都会被命名引用，是否有遗漏？
  // 不会，如果 A->B->D / A->C->D / A -> F -> E -> D
  // 假设在 D 是 A 和 E 的支配边界，
  // 当且仅当 x = live_in(D) 时
  // D 中变量 x = phi(x of pred-B, x of pred-C，x of pred-E)
  // 当计算到 B 时，即使变量，没有在 b 中定义，只要函数的作用域还在，在 stack 中也一定能找到的变量重命名，无非是同名而已！！！
  for (int i = 0; i < block->succs.count; ++i) {
    struct lir_basic_block *succ = block->succs.list[i];
    // 为 每个 phi 函数的 phi param 命名
    lir_op *succ_op = succ->first_op->succ;
    while (succ_op->type == LIR_OP_TYPE_PHI) {
      lir_operand_phi_body *phi_body = succ_op->first.value;
      lir_operand_var *var = phi_body->vars.list[phi_body->rename_count++];
      var_number_stack *stack = table_get(stack_table, var->ident);
      uint8_t number = stack->numbers[stack->count - 1];
      ssa_rename_var(var, number);

      succ_op = succ_op->succ;
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
  op = block->first_op->succ;
  while (op != NULL) {
    if (op->result.type == LIR_OPERAND_TYPE_VAR) {
      lir_operand_var *var = (lir_operand_var *) op->result.value;

      // pop stack
      var_number_stack *stack = table_get(stack_table, var->ident);
      stack->count--;
    }
    op = op->succ;
  }
}

uint8_t ssa_new_var_number(lir_operand_var *var, table *var_number_table, table *stack_table) {
  uint8_t *value = table_get(var_number_table, var->ident);
  var_number_stack *stack = table_get(stack_table, var->ident);

  uint8_t result = *value;
  *value += 1;

  table_set(var_number_table, var->ident, value);
  stack->numbers[stack->count++] = result;

  return result;
}
void ssa_rename_var(lir_operand_var *var, uint8_t number) {
  var->old = var->ident;
  // 1 '\0'
  // 2 '_12'
  char *buf = (char *) malloc(strlen(var->ident) + 1 + 2);
  sprintf(buf, "%s_%d", var->ident, number);
  var->ident = buf; // 已经分配在了堆中，需要手动释放了
}


