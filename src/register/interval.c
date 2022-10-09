#include "interval.h"
#include "utils/list.h"
#include "utils/stack.h"
#include "utils/helper.h"
#include "assert.h"

static bool in_range(interval_range_t *range, int position) {
    return range->from <= position && position < range->to;
}

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

    lir_basic_blocks loop_headers = {.count = 0};
    lir_basic_blocks loop_ends = {.count = 0};

    // 1. 探测出循环头与循环尾部
    while (!list_empty(work_list)) {
        lir_basic_block *block = list_pop(work_list);

        // 是否会出现 succ 的 flag 是 visited?
        // 如果当前块是 visited,则当前块的正向后继一定是 null, 当前块的反向后继一定是 active,不可能是 visited
        // 因为一个块的所有后继都进入到 work_list 之后，才会进行下一次 work_list 提取操作
        slice_t *forward_succs = slice_new();
        for (int i = 0; i < block->succs->count; ++i) {
            lir_basic_block *succ = block->succs->take[i];
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

            slice_push(forward_succs, succ);
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
        table_set(exist_table, itoa(end->label), end);

        while (!list_empty(work_list)) {
            lir_basic_block *block = list_pop(work_list);
            if (block->label != end->label && block->loop.index == end->loop.index) {
                continue;
            }
            // 标号
            block->loop.index_list[block->loop.depth++] = end->loop.index;

            for (int k = 0; k < block->preds->count; ++k) {
                lir_basic_block *pred = block->preds->take[k];

                // 判断是否已经入过队(标号)
                if (table_exist(exist_table, itoa(pred->label))) {
                    continue;
                }
                table_set(exist_table, itoa(block->label), block);
                list_push(work_list, pred);
            }
        }
        table_free(exist_table);
    }

    // 3. 遍历所有 basic_block ,通过 loop.index_list 确定 index
    for (int label = 0; label < c->blocks->count; ++label) {
        lir_basic_block *block = c->blocks->take[label];
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
static void interval_insert_to_stack_by_depth(stack_t *work_list, lir_basic_block *block) {
    // next->next->next
    stack_node *p = work_list->top; // top 指向栈中的下一个可用元素，总是为 NULL
    while (p->next != NULL && ((lir_basic_block *) p->next->value)->loop.depth > block->loop.depth) {
        p = p->next;
    }

    // p->next == NULL 或者 p->next 小于等于 当前 block
    // p = 3 block = 2  p_next = 2
    stack_node *last_node = p->next;
    // 初始化一个 node
    stack_node *await_node = stack_new_node(block);
    p->next = await_node;

    if (last_node != NULL) {
        await_node->next = last_node;
    }
}

// 优秀的排序从而构造更短更好的 lifetime interval
// 权重越大排序越靠前
// 权重的本质是？或者说权重越大一个基本块？
void interval_block_order(closure *c) {
    stack_t *work_list = stack_new();
    stack_push(work_list, c->entry);

    while (!stack_empty(work_list)) {
        lir_basic_block *block = stack_pop(work_list);
        slice_push(c->order_blocks, block);

        // 需要计算每一个块的正向前驱的数量
        for (int i = 0; i < block->forward_succs->count; ++i) {
            lir_basic_block *succ = block->forward_succs->take[i];
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
    for (int i = 0; i < c->order_blocks->count; ++i) {
        lir_basic_block *block = c->order_blocks->take[i];
        list_node *current = list_first(block->operations);

        while (current->value != NULL) {
            lir_op *op = current->value;
            if (op->code == LIR_OPCODE_PHI) {
                current = current->succ;
                continue;
            }

            op->id = next_id;
            next_id += 2;
            current = current->succ;
        }
    }
}

void interval_build(closure *c) {
    // init interval
    c->interval_table = table_new();
    for (int i = 0; i < c->globals.count; ++i) {
        lir_operand_var *var = c->globals.list[i];
        table_set(c->interval_table, var->ident, interval_new(c, var));
    }

    // 倒序遍历顺序基本块基本块
    for (int i = c->order_blocks->count - 1; i >= 0; --i) {
        lir_basic_block *block = c->order_blocks->take[i];
        lir_op *first_op = list_first(block->operations)->value;
        int block_from = first_op->id;
        int block_to = first_op->id + 2;

        // 遍历所有的 live_out,直接添加最长间隔,后面会逐渐缩减该间隔
        for (int k = 0; k < block->live_out.count; ++k) {
            interval_add_range(c, block->live_out.list[k], block_from, block_to);
        }

        // 倒序遍历所有块指令
        list_node *current = list_last(block->operations);
        while (current->value != NULL) {
            // 判断是否是 call op,是的话就截断所有物理寄存器
            lir_op *op = current->value;

            lir_vars output_vars = lir_output_vars(op);
            for (int j = 0; j < output_vars.count; ++j) {
                lir_operand_var *var = output_vars.list[j];
                interval_cut_first_range_from(c, var, op->id); // 截断操作
                interval_add_use_position(c, var, op->id, 0); // TODO 计算 use_kind
            }

            lir_vars input_vars = lir_input_vars(op);
            for (int j = 0; j < input_vars.count; ++j) {
                lir_operand_var *var = input_vars.list[j];
                interval_add_range(c, var, block_from, op->id); // 添加整段长度
                interval_add_use_position(c, var, op->id, 0); // TODO 计算 use_kind
            }

            current = current->prev;
        }
    }
}

interval_t *interval_new(closure *c, lir_operand_var *var) {
    interval_t *entity = malloc(sizeof(interval_t));
    entity->var = var;
    entity->ranges = list_new();
    entity->use_positions = list_new();
    entity->children = list_new();
    entity->parent = NULL;
    entity->index = c->interval_count++; // 基于 closure 做自增 id 即可
    return entity;
}

bool interval_is_covers(interval_t *i, int position) {
    list_node *current = list_first(i->ranges);
    while (current->value != NULL) {
        interval_range_t *range = current->value;
        if (range->from <= position && range->to >= position) {
            return true;
        }

        current = current->succ;
    }
    return 0;
}

int interval_next_intersection(interval_t *current, interval_t *select) {
    int position = current->first_range->from; // first_from 指向 range 的开头
    while (position < current->last_range->to) {
        if (interval_is_covers(current, position) && interval_is_covers(select, position)) {
            return position;
        }
        position++;
    }

    return position;
}

// 在 before 前挑选一个最佳的位置进行 split
int interval_optimal_position(closure *c, interval_t *current, int before) {
    return before;
}

// TODO 是否需要处理重叠部分？
void interval_add_range(closure *c, lir_operand_var *var, int from, int to) {
    // 排序，合并
    interval_t *i = table_get(c->interval_table, var->ident);
    list *ranges = i->ranges;
    // 否则按从小到大的顺序插入 ranges
    interval_range_t *range = NEW(interval_range_t);
    range->from = from;
    range->to = to;
    if (list_empty(ranges)) {
        i->last_range = range;
    }

    list_insert_after(ranges, NULL, range);
    i->first_range = range;
}

/**
 * 按从小到大排序
 * @param c
 * @param var
 * @param position
 * @param kind
 */
void interval_add_use_position(closure *c, lir_operand_var *var, int position, int kind) {
    interval_t *i = table_get(c->interval_table, var->ident);
    list *pos_list = i->use_positions;

    use_position_t *new_pos = NEW(use_position_t);
    new_pos->kind = kind;
    new_pos->value = position;

    list_node *current = list_first(pos_list);
    while (current->value != NULL) {
        use_position_t *current_pos = current->value;
        // 找到一个大于当前位置的节点
        if (current_pos->value > new_pos->value) {
            // 当前位置一旦大于 await
            // 就表示 current->prev < await < current
            // 或者 await < current, 也就是 current 就是第一个元素
            list_insert_after(pos_list, current->prev, new_pos);
            return;
        }

        current = current->succ;
    }
}

void interval_cut_first_range_from(closure *c, lir_operand_var *var, int from) {
    interval_t *i = table_get(c->interval_table, var->ident);
    i->first_range->from = from;
}

int interval_first_use_position(interval_t *i) {
    list *pos_list = i->use_positions;
    if (list_empty(pos_list)) {
        return 0;
    }
    use_position_t *use_pos = list_first(pos_list)->value;
    return use_pos->value;
}

int interval_next_use_position(interval_t *i, int after_position) {
    list *pos_list = i->use_positions;
    list_node *current = list_first(pos_list);
    while (current->value != NULL) {
        use_position_t *current_pos = current->value;
        if (current_pos->value > after_position) {
            return current_pos->value;
        }

        current = current->succ;
    }
    return 0;
}

/**
 * 从 position 将 interval 分成两端，多个 child interval 在一个 list 中，而不是多级 list
 * 如果 position 被 range cover, 则对 range 进行切分
 * @param c
 * @param i
 * @param position
 */
interval_t *interval_split_at(closure *c, interval_t *i, int position) {
    assert(i->last_range->to < position);
    assert(i->first_range->from < position);

    interval_t *child = interval_new(c, i->var);

    interval_t *parent = i;
    if (i->parent != NULL) {
        parent = i->parent;
    }

    // 将 child 加入 parent 的 children 中,
    // 因为是从 i 中分割出来的，所以需要插入到 i 对应到 node 的后方
    LIST_FOR(parent->children) {
        interval_t *current = LIST_VALUE();
        if (current->index == i->index) {
            list_insert_after(parent->children, LIST_NODE(), child);
            break;
        }
    }

    // 切割 range
    LIST_FOR(i->ranges) {
        interval_range_t *range = LIST_VALUE();
        if (!in_range(range, position)) {
            continue;
        }

        // 如果 position 在 range 的起始位置，则直接将 ranges list 的当前部分和剩余部分分给 child interval 即可
        // 否则 对 range 进行切割
        // tips: position 必定不等于 range->to, 因为 to 是 excluded
        if (position == range->from) {
            child->ranges = list_split(i->ranges, LIST_NODE());
            break;
        }

        interval_range_t *new_range = NEW(interval_range_t);
        new_range->from = position;
        new_range->to = range->to;
        range->to = position;

        // 将 new_range 插入到 ranges 中
        list_insert_after(i->ranges, LIST_NODE(), new_range);
        child->ranges = list_split(i->ranges, LIST_NODE());
        break;
    }

    // 划分 position
    LIST_FOR(i->use_positions) {
        use_position_t *pos = LIST_VALUE();
        if (pos->value < position) {
            continue;
        }

        // pos->value >= position, pos 和其之后的 pos 都需要加入到 new child 中
        child->use_positions = list_split(i->use_positions, LIST_NODE());
        break;
    }

    return child;
}

/**
 * 其实啥也不用做,早就分配了 slot
 * @param i
 */
void interval_spill_slot(interval_t *i) {
    i->assigned = NULL;
}

/**
 * use_positions 是否包含 kind > 0 的position, 有则返回 use_position，否则返回 NULL
 * @param i
 * @return
 */
use_position_t *interval_use_pos_of_kind(interval_t *i) {
    LIST_FOR(i->use_positions) {
        use_position_t *pos = LIST_VALUE();
        if (pos->kind > 0) {
            return pos;
        }
    }
    return NULL;
}

