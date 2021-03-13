#include <c++/v1/functional>
#include "ssa.h"

//  1. phi 如何编号？和 label 编号一致
// 2. 支配边界其实就是两条线路的汇聚点,如果其中一条线路
// n 中定义的每个变量 x 都需要在，df(n) 中的块声明对应的 x = phi(x_pred1,x_pred2), 即使在 df(n) 中没有使用该变量,
// 毕竟谁能保证后续用不到呢(live_out 可以保证哈哈）
void ssa_add_phi(closure *c) {
  for (int label = 0; label < c->block_labels; ++label) {
    // 定义的每个变量
    linear_vars def = c->blocks[label]->def;
    linear_df df = c->blocks[label]->df;

    for (int i = 0; i < def.count; ++i) {
      linear_operand_var *var = def.vars[i];

      for (int k = 0; k < df.count; ++k) {
        linear_basic_block *df_block = df.blocks[k];
        // 判断该变量是否已经添加过 phi(另一个分支可能会先创建), 创建则跳过
        if (ssa_phi_defined(var, df_block)) {
          continue;
        }

        // 如果变量不在当前 n ∈ df 入口活跃,则不需要定义
        if (!ssa_var_belong(var, df_block->live_in)) {
          continue;
        }

        // add phi
        linear_op *phi_op = linear_new_op(LINEAR_OP_TYPE_PHI);
        phi_op->result.type = LINEAR_OPERAND_TYPE_VAR;
        phi_op->result.value = linear_clone_operand_var(var);

        // param to first
        phi_op->first.type = LINEAR_OPERAND_TYPE_PHI_BODY;
        phi_op->first.value = linear_new_phi_body(var, df_block->preds_count);

        // insert to linked list
        linear_op *label_op = df_block->op;
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
  for (int label = 0; label < c->block_labels; ++label) {
    linear_vars out = {.count=0};
    linear_vars in = {.count=0};
    c->blocks[label]->live_out = out;
    c->blocks[label]->live_in = in;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (int label = c->block_labels; label >= 0; --label) {
      linear_basic_block *block = c->blocks[label];
      linear_vars new_live_out = ssa_calc_live_out(c, c->blocks[label]);
      if (ssa_live_changed(&block->live_out, &new_live_out)) {
        changed = true;
        block->live_out = new_live_out;
      }

      linear_vars new_live_in = ssa_calc_live_in(c, c->blocks[label]);
      if (ssa_live_changed(&block->live_in, &new_live_in)) {
        changed = true;
        block->live_out = new_live_out;
      }
    }
  }
}

// live out 为 n 的所有后继的 live_in 的并集
linear_vars ssa_calc_live_out(closure *c, linear_basic_block *block) {
  linear_vars live_out = {.count = 0};
  table *exist_var = table_new(); // basic var ident

  for (int i = 0; i < block->succs_count; ++i) {
    linear_basic_block *succ = block->succs[i];

    // 未在 succ 中被重新定义(def)，且离开 succ 后继续活跃的变量
    for (int k = 0; k < succ->live_in.count; ++k) {
      linear_operand_var *var = succ->live_in.vars[k];
      if (table_exist(exist_var, var->ident)) {
        continue;
      }
      live_out.vars[live_out.count++] = var;
      table_set(exist_var, var->ident, var);
    }
  }

  table_free(exist_var);
  return live_out;
}

// 在当前块使用的变量 + 离开当前块依旧活跃的变量（这些变量未在当前块重新定义）
linear_vars ssa_calc_live_in(closure *c, linear_basic_block *block) {
  linear_vars live_in = {.count = 0};
  table *exist_var = table_new(); // basic var ident

  for (int i = 0; i < block->use.count; ++i) {
    linear_operand_var *var = block->use.vars[i];
    if (table_exist(exist_var, var->ident)) {
      continue;
    }

    live_in.vars[live_in.count++] = var;
    table_set(exist_var, var->ident, var);
  }

  for (int i = 0; i < block->live_out.count; ++i) {
    linear_operand_var *var = block->live_out.vars[i];
    if (table_exist(exist_var, var->ident)) {
      continue;
    }

    // 是否是当前块中定义的变量。
    if (ssa_var_belong(var, block->def)) {
      continue;
    }

    live_in.vars[live_in.count++] = var;
    table_set(exist_var, var->ident, var);
  }

  table_free(exist_var);
  return live_in;
}

bool ssa_live_changed(linear_vars *old, linear_vars *new) {
  if (old->count != new->count) {
    return true;
  }
  table *var_count = table_new();
  for (int i = 0; i < old->count; ++i) {
    string ident = old->vars[i]->ident;
    table_set(var_count, ident, old->vars[i]);
  }

  // double count
  uint8_t double_count = 0;
  for (int i = 0; i < new->count; ++i) {
    string ident = new->vars[i]->ident;
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
  for (int label = 0; label < c->block_labels; ++label) {
    linear_vars use = {.count=0};
    linear_vars def = {.count=0};

    table *exist_use = table_new();
    table *exist_def = table_new();

    linear_basic_block *block = c->blocks[label];

    linear_op *op = block->op;
    while (op != NULL) {
      // first param
      if (op->first.type == LINEAR_OPERAND_TYPE_VAR) {
        linear_operand_var *var = (linear_operand_var *) op->first.value;
        bool is_def = ssa_var_belong(var, def);
        if (!is_def && !table_exist(exist_use, var->ident)) {
          use.vars[use.count++] = var;
          table_set(exist_use, var->ident, var);
        }
      }

      // second param
      if (op->second.type == LINEAR_OPERAND_TYPE_VAR) {
        linear_operand_var *var = (linear_operand_var *) op->second.value;
        bool is_def = ssa_var_belong(var, def);
        if (!is_def && !table_exist(exist_use, var->ident)) {
          use.vars[use.count++] = var;
          table_set(exist_use, var->ident, var);
        }
      }

      if (op->result.type == LINEAR_OPERAND_TYPE_VAR) {
        linear_operand_var *var = (linear_operand_var *) op->result.value;
        if (!table_exist(exist_def, var->ident)) {
          def.vars[def.count++] = var;
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
  for (int label = 0; label < c->block_labels; ++label) {
    linear_df df = {.count = 0};
    c->blocks[label]->df = df;
  }

  for (int label = 0; label < c->block_labels; ++label) {
    linear_basic_block *current_block = c->blocks[label];
    if (current_block->preds_count < 2) {
      continue;
    }

    for (int i = 0; i < current_block->preds_count; ++i) {
      linear_basic_block *pred = current_block->preds[i];
      // 只要 pred 不是 当前块的最近支配者, pred 的支配边界就一定包含着 current_block
      // 是否存在 idom[current_block] != pred, 但是 dom[current_block] = pred?
      // 不可能， 因为是从 current_block->pred->idom(pred)
      // pred 和 idom(pred) 之间如果存在节点支配 current,那么其一定会支配 current->pred，则其就是 idom(pred)
      while (pred->label != current_block->idom->label) {
        pred->df.blocks[pred->df.count++] = current_block;
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
  for (int label = 0; label < c->block_labels; ++label) {
    linear_be_idom be_idom = {.count = 0};
    c->blocks[label]->be_idom = be_idom;
  }
  for (int label = 0; label < c->block_labels; ++label) {
    linear_basic_block *block = c->blocks[label];
    block->idom = block->dom.blocks[block->dom.count - -2];
    // 添加反向关系
    block->idom->be_idom.blocks[block->idom->be_idom.count] = block;
  }
}

void ssa_dom(closure *c) {
  // 初始化, dom[n0] = {l0}
  linear_dom dom = {.count = 0};
  dom.blocks[dom.count++] = c->blocks[0];
  c->blocks[0]->dom = dom;

  // 初始化其他 dom
  for (int i = 1; i < c->block_labels; ++i) {
    linear_dom other = {.count = 0};

    for (int k = 0; k < c->block_labels; ++k) {
      other.blocks[other.count++] = c->blocks[k];
    }

    c->blocks[i]->dom = other;
  }

  // 求不动点
  bool changed = true;
  while (changed) {
    changed = false;

    // dom[0] 自己支配自己，没必要进一步深挖了,所以从 1 开始遍历
    for (int label = 1; label < c->block_labels; ++label) {
      linear_dom new_dom = ssa_calc_dom_blocks(c, c->blocks[label]);
      // 判断 dom 是否不同
      if (ssa_dom_changed(&c->blocks[label]->dom, &new_dom)) {
        changed = true;
        c->blocks[label]->dom = new_dom;
      }
    }
  }
}

bool ssa_dom_changed(linear_dom *old, linear_dom *new) {
  if (old->count != new->count) {
    true;
  }

  // 因此是根据 block label 从小到大排序的，所以可以这么遍历
  for (int i = 0; i < old->count; ++i) {
    if (old->blocks[i]->label != new->blocks[i]->label) {
      return true;
    }
  }

  return false;
}

linear_dom ssa_calc_dom_blocks(closure *c, linear_basic_block *block) {
  linear_dom dom = {.count = 0};

  // 遍历当前 block 的 preds 的 dom_list, 然后求交集
  // key => label
  // value => a number of
  uint8_t block_label_count[UINT8_MAX];
  for (int i = 0; i < block->preds_count; ++i) {
    // 找到 pred
    linear_basic_block *pred = block->preds[i];
    // 通过 pred->label，从 dom_list 中找到对应的 dom
    linear_dom pred_dom = pred->dom;
    // 遍历 pred_dom 为 label 计数
    for (int k = 0; k < pred_dom.count; ++k) {
      block_label_count[pred_dom.blocks[k]->label]++;
    }
  }

  // 如果 block 的count 和 preds_count 的数量一致则表示全部相交，即
  for (int i = 0; i < c->block_labels; ++i) {
    if (block_label_count[i] == block->preds_count) {
      dom.blocks[dom.count++] = c->blocks[i];
    }
  }

  // 由于 n 的支配者的 label 肯定小于 n, 所以最后添加 n,这样支配者就是按从小往大编号了
  // 从而便于比较，和找出 idom
  dom.blocks[dom.count++] = block;

  return dom;
}

void ssa_rename(closure *c) {
  // 遍历所有名字
}
