#include "interval.h"
#include "src/lib/list.h"
#include "src/lib/stack.h"

// 每个块需要存储什么数据？
// loop flag
// loop index,只存储最内层的循环的 index
// loop depth
// 每个块的前向和后向分支 count
// 当且仅当循环块的开头
// 假如使用广度优先遍历，编号按照广度优先的层级来编号,则可以方便的计算出树的高度,顶层 为 0，然后依次递增
// 使用树的高度来标记 loop_index，如果一个块被标记了两个 index ,则 index 大的为内嵌循环
// 当前层 index 等于父节点 + 1
void interval_loop_detection(closure *c) {
  c->entry->loop.flag = LOOP_DETECTION_FLAG_VISITED;
  c->entry->loop.tree_high = 1; // 从 1 开始标号，避免出现 0 = 0 的判断
  list *work_list = list_new();
  list_push(work_list, c->entry);

  lir_blocks loop_headers = {.count = 0};
  lir_blocks loop_ends = {.count = 0};

  // 1. 探测出循环头与循环尾部
  while (!list_empty(work_list)) {
    lir_basic_block *block = list_pop(work_list);

    // 是否会出现 succ 的 flag 是 visited?
    // 如果当前块是 visited,则当前块的正向后继一定是 null, 当前块的反向后继一定是 active,不可能是 visited
    // 因为一个块的所有后继都进入到 work_list 之后，才会进行下一次 work_list 提取操作
    lir_blocks forward_succs = {.count = 0};
    for (int i = 0; i < block->succs.count; ++i) {
      lir_basic_block *succ = block->succs.list[i];
      succ->loop.tree_high = block->loop.tree_high + 1;

      // 如果发现循环, backward branches
      if (succ->loop.flag == LOOP_DETECTION_FLAG_ACTIVE) {
        // 当前 succ 是 loop_headers, loop_headers 的 tree_high 为 loop_index
        succ->loop.index = succ->loop.tree_high;
        succ->loop.index_list[succ->loop.depth++] = succ->loop.tree_high;
        loop_headers.list[loop_headers.count++] = succ;

        // 当前 block 是 loop_ends, loop_ends, index = loop_headers.index
        block->loop.index = succ->loop.tree_high;
        block->loop.index_list[block->loop.depth++] = succ->loop.tree_high;
        loop_ends.list[loop_ends.count++] = block;
        continue;
      }

      forward_succs.list[forward_succs.count++] = succ; // 后继的数量
      succ->incoming_forward_count++; // 前驱中正向进我的数量
      succ->loop.flag = LOOP_DETECTION_FLAG_VISITED;
    }

    // 添加正向数据流
    block->forward_succs = forward_succs;
    // 变更 flag
    block->loop.flag = LOOP_DETECTION_FLAG_ACTIVE;
  }

  // 2. 标号, 这里有一个严肃的问题，如果一个节点有两个前驱时，也能够被标号吗？如果是普通结构不考虑 goto 的情况下，则不会初选这种 cfg
  for (int i = 0; i < loop_ends.count; ++i) {
    lir_basic_block *end = loop_ends.list[i];
    list_push(work_list, end);
    table *exist_table = table_new();
    table_set(exist_table, lir_label_to_string(end->label), end);

    while (!list_empty(work_list)) {
      lir_basic_block *block = list_pop(work_list);
      if (block->label != end->label && block->loop.index == end->loop.index) {
        continue;
      }
      // 标号
      block->loop.index_list[block->loop.depth++] = end->loop.index;

      for (int k = 0; k < block->preds.count; ++k) {
        lir_basic_block *pred = block->preds.list[k];

        // 判断是否已经入过队(标号)
        if (table_exist(exist_table, lir_label_to_string(pred->label))) {
          continue;
        }
        table_set(exist_table, lir_label_to_string(block->label), block);
        list_push(work_list, pred);
      }
    }
    table_free(exist_table);
  }

  // 3. 遍历所有 basic_block ,通过 loop.index_list 确定 index
  for (int label = 0; label < c->blocks.count; ++label) {
    lir_basic_block *block = c->blocks.list[label];
    if (block->loop.index != 0) {
      continue;
    }
    if (block->loop.depth == 0) {
      continue;
    }

    // 值越大，树高越低
    uint8_t index = 0;
    for (int i = 0; i < block->loop.depth; ++i) {
      if (block->loop.index_list[i] > index) {
        index = block->loop.index_list[i];
      }
    }

    block->loop.index = index;
  }
}

// 大值在栈顶被优先处理
static void interval_insert_to_stack_by_depth(stack *work_list, lir_basic_block *block) {
  // next->next->next
  stack_node *p = work_list->top;
  while (p->next != NULL && ((lir_basic_block *) p->next->value)->loop.depth > block->loop.depth) {
    p = p->next;
  }

  // p->next == NULL 或者 p->next 小于等于 当前 block
  // p = 3 block = 2  p_next = 2
  stack_node *last_node = p->next;
  // 初始化一个 node
  stack_node *await_node = stack_new_node();
  await_node->value = block;
  p->next = await_node;

  if (last_node != NULL) {
    await_node->next = last_node;
  }
}

// 优秀的排序从而构造更短更好的 lifetime interval
// 权重越大排序越靠前
// 权重的本质是？或者说权重越大一个基本块？
void interval_block_order(closure *c) {
  stack *work_list = stack_new();
  stack_push(work_list, c->entry);

  while (!stack_empty(work_list)) {
    lir_basic_block *block = stack_pop(work_list);
    c->order_blocks.list[c->order_blocks.count++] = block;

    // 需要计算每一个块的正向前驱的数量
    for (int i = 0; i < block->forward_succs.count; ++i) {
      lir_basic_block *succ = block->forward_succs.list[i];
      succ->incoming_forward_count--;
      if (succ->incoming_forward_count == 0) {
        // sort into work_list by loop.depth, 权重越大越靠前，越先出栈
        interval_insert_to_stack_by_depth(work_list, succ);
      }
    }
  }
}

void interval_mark_number(closure *c) {
  int next_id = 0;
  for (int i = 0; i < c->order_blocks.count; ++i) {
    lir_op *op = c->order_blocks.list[i]->first_op;
    while (op != NULL) {
      if (op->type == LIR_OP_TYPE_PHI) {
        op = op->succ;
        continue;
      }

      op->id = next_id;
      next_id += 2;

      op = op->succ;
    }
  }
}

void interval_build(closure *c) {
  // init interval
  c->interval_table = table_new();
  for (int i = 0; i < c->globals.count; ++i) {
    lir_operand_var *var = c->globals.list[i];
    table_set(c->interval_table, var->ident, interval_new(var));
  }

  // 倒序遍历基本块
  for (int i = c->order_blocks.count - 1; i >= 0; --i) {
    lir_basic_block *block = c->order_blocks.list[i];
    int block_from = block->first_op->id;
    int block_to = block->first_op->id + 2; // 为什么加 2 我也没想好

    // 遍历所有的 live_out,直接添加最长间隔,后面会逐渐缩减该间隔
    for (int k = 0; k < block->live_out.count; ++k) {
      interval_add_range(c, block->live_out.list[k], block_from, block_to);
    }

    // 倒序遍历所有块指令
    lir_op *op = block->last_op;
    while (op != NULL) {
      // 判断是否是 call op,是的话就截断所有物理寄存器

      // result param
      if (op->result.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->result.value;
        interval_add_first_range_from(c, var, op->id); // 截断操作
        interval_add_use_position(c, var, op->id);
      }

      // first param
      if (op->first.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->first.value;
        interval_add_range(c, var, block_from, op->id);
        interval_add_use_position(c, var, op->id);
      }

      // second param
      if (op->second.type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = (lir_operand_var *) op->second.value;
        interval_add_range(c, var, block_from, op->id);
        interval_add_use_position(c, var, op->id);
      }

      op = op->pred;
    }
  }
}

interval *interval_new(lir_operand_var *var) {
  interval *entity = malloc(sizeof(interval));
  entity->var = var;
  entity->ranges = slice_new();
  entity->use_positions = slice_new();
  entity->split_children = slice_new();
  entity->split_parent = NULL;
  return entity;
}

void interval_add_range(closure *c, lir_operand_var *var, int from, int to) {
  // 排序，合并
}


