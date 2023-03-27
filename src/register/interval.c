#include "interval.h"
#include "src/ssa.h"
#include "utils/stack.h"
#include "assert.h"
#include "src/debug/debug.h"


static bool interval_need_move(interval_t *from, interval_t *to) {
    if (from->assigned && to->assigned && from->assigned == to->assigned) {
        return false;
    }

    if (from->spilled && to->spilled && from->stack_slot == to->stack_slot) {
        return false;
    }

    return true;
}

/**
 * 深度优先(右侧优先) 遍历，并标记 loop index/header
 * 深度优先有前序和后续遍历，此时采取后续遍历的方式来为 loop header 标号，
 * 这样可以保证所有的子循环都标号完毕后才标号当前 block
 */
static void loop_header_detect(closure_t *c, basic_block_t *current, basic_block_t *parent) {
    // 探测到循环，current is loop header,parent is loop end
    // current 可能被多个 loop ends 进入,所以这里不能收集 loop headers
    if (current->loop.active) {
        assert(current->loop.visited);
        assert(parent);

        current->loop.header = true;
        parent->loop.end = true;

        parent->backward_succ = current;

        assert(parent->succs->count == 1 && parent->succs->take[0] == current && "critical edge must broken");

        slice_push(current->loop_ends, parent);
        slice_push(c->loop_ends, parent); // 一个 header 可能对应多个 end
        return;
    }

    // increment count of incoming forward branches parent -> current is forward
    current->incoming_forward_count += 1;
    if (parent) {
        slice_push(parent->forward_succs, current);
    }

    if (current->loop.visited) {
        return;
    }

    // num block++
    current->loop.visited = true;
    current->loop.active = true;

    for (int i = current->succs->count - 1; i >= 0; --i) {
        basic_block_t *succ = current->succs->take[i];
        loop_header_detect(c, succ, current);
    }

    current->loop.active = false;

    // 后序操作(此时 current 可能在某次 backward succ 中作为 loop header 打上了标记)
    // 深度优先遍历加上 visited 机制，保证一个节点只会被 iteration
    if (current->loop.header) {
        assert(current->loop.index == -1);
        // 所有的内循环已经处理完毕了，所以外循环的编号总是大于内循环
        current->loop.index = c->loop_count++;
        slice_push(c->loop_headers, current);
    }
}

/**
 * 遍历所有 loop ends，找到这个 loop 下的所有 block 即可。
 * 如果一个 block 被多个 loop 经过，则 block index_list 的 key 就是 loop_index, value 就是是否被改 loop 穿过
 */
static void loop_mark(closure_t *c) {
    linked_t *work_list = linked_new();

    for (int i = 0; i < c->loop_ends->count; ++i) {
        basic_block_t *end = c->loop_ends->take[i];

        assert(end->succs->count == 1 && "critical edge must broken");

        basic_block_t *header = end->succs->take[0];
        assert(header->loop.header);
        assert(header->loop.index >= 0);
        int8_t loop_index = header->loop.index;

        linked_push(work_list, end);
        end->loop.index_map[loop_index] = true;

        do {
            basic_block_t *current = linked_pop(work_list);

            assert(current->loop.index_map[loop_index]);

            if (current == header) {
                continue;
            }

            // end -> preds -> preds -> header 之间的所有 block 都属于当前 index
            for (int j = 0; j < current->preds->count; ++j) {
                basic_block_t *pred = current->preds->take[j];
                if (pred->loop.index_map[loop_index]) {
                    // 已经配置过了，直接跳过
                    continue;
                }

                linked_push(work_list, pred);
                pred->loop.index_map[loop_index] = true;
            }

        } while (!linked_empty(work_list));
    }
}

/**
 * 遍历所有 block 分配 index 和 depth
 * @param c
 */
static void loop_assign_depth(closure_t *c) {
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        assert(block->loop.depth == 0);
        int max_depth = 0;
        int8_t min_index = -1;
        for (int j = c->loop_count - 1; j >= 0; --j) {
            if (!block->loop.index_map[j]) {
                continue;
            }

            // block 在 loop j 中
            max_depth++;
            min_index = j;
        }

        block->loop.index = min_index;
        block->loop.depth = max_depth;
    }
}

static void loop_detect(closure_t *c) {
    loop_header_detect(c, c->entry, NULL);
    loop_mark(c);
    loop_assign_depth(c);
}

static interval_t *interval_new_child(closure_t *c, interval_t *i) {
    interval_t *child = interval_new(c);
    interval_t *parent = i;
    if (parent->parent) {
        parent = parent->parent;
    }

    if (i->var) {
        lir_var_t *var = temp_var_operand(c, i->var->type)->value;
        child->var = var;
        table_set(c->interval_table, var->ident, child);
    } else {
        assert(parent->fixed);
        child->fixed = parent->fixed;
        child->assigned = parent->assigned;
        table_set(c->interval_table, alloc_regs[child->assigned]->name, child);
    }

    child->parent = parent;
    child->reg_hint = parent;
    child->alloc_type = parent->alloc_type;
    child->stack_slot = parent->stack_slot;

    return child;
}

static bool resolve_blocked(int8_t *block_regs, interval_t *from, interval_t *to) {
    if (to->spilled) {
        return false;
    }

    if (block_regs[to->assigned] == 0) {
        return false;
    }

    if (block_regs[to->assigned] == 1 && to->assigned == from->assigned) {
        return false;
    }

    return true;
}

static void block_insert_mov(basic_block_t *block, int id, interval_t *src_i, interval_t *dst_i) {
    LINKED_FOR(block->operations) {
        lir_op_t *op = LINKED_VALUE();
        if (op->id < id) {
            continue;
        }

        // last->id < id < op->id
        lir_operand_t *dst = LIR_NEW_OPERAND(LIR_OPERAND_VAR, dst_i->var);
        lir_operand_t *src = LIR_NEW_OPERAND(LIR_OPERAND_VAR, src_i->var);
        lir_op_t *mov_op = lir_op_move(dst, src);
        mov_op->id = id;
        linked_insert_before(block->operations, LINKED_NODE(), mov_op);

        if (block->first_op == LINKED_NODE()) {
            block->first_op = LINKED_NODE()->prev;
        }
        return;
    }
}

static void closure_insert_mov(closure_t *c, int insert_id, interval_t *src_i, interval_t *dst_i) {
    SLICE_FOR(c->blocks) {
        basic_block_t *block = SLICE_VALUE(c->blocks);
        if (OP(block->first_op)->id > insert_id || OP(block->last_op)->id < insert_id) {
            continue;
        }

        block_insert_mov(block, insert_id, src_i, dst_i);
    }
}

static interval_t *operand_interval(closure_t *c, lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *var = operand->value;
        interval_t *interval = table_get(c->interval_table, var->ident);
        assert(interval);
        return interval;
    }
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = covert_alloc_reg(operand->value);
        if (!reg->alloc_id) {
            return NULL;
        }

        interval_t *interval = table_get(c->interval_table, reg->name);
        assert(interval);
        return interval;
    }

    return NULL;
}

/**
 * output 没有特殊情况就必须分一个寄存器，主要是 amd64 的指令基本都是需要寄存器参与的
 * @param c
 * @param op
 * @param i
 * @return
 */
static use_kind_e use_kind_of_def(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (lir_op_contain_cmp(op)) {
        if (var->flag & FLAG(VR_FLAG_OUTPUT)) { // 顶层 var 才不用分配寄存器，否则 var 可能只是 indirect_addr base
            return USE_KIND_SHOULD;
        }
    }
    if (op->code == LIR_OPCODE_ADD || op->code == LIR_OPCODE_SUB) {
        return USE_KIND_SHOULD;
    }

    return USE_KIND_MUST;
}

/**
 * TODO 这里的 use_kind 应该是要按 arch 适配的
 * 如果 op type is var, 且是 indirect addr, 则必须分配一个寄存器，用于地址 indirect addr
 * output 已经必须要有寄存器了, input 就无所谓了
 * @param c
 * @param op
 * @param i
 * @return
 */
static use_kind_e use_kind_of_use(closure_t *c, lir_op_t *op, lir_var_t *var) {
    if (op->code == LIR_OPCODE_LEA && var->flag & FLAG(VR_FLAG_FIRST)) {
        return USE_KIND_NOT;
    }

    if (lir_op_is_arithmetic(op)) {
        assertf(op->first->assert_type == LIR_OPERAND_VAR, "arithmetic op first operand must var for assign reg");
        if (var->flag & FLAG(VR_FLAG_FIRST)) {
            return USE_KIND_MUST;
        }
    }

    // 比较运算符实用了 op cmp, 所以 cmp 的 first 或者 second 其中一个必须是寄存器
    // 如果优先分配给 first, 如果 first 不是寄存器，则分配给 second
    if (lir_op_contain_cmp(op)) { // cmp indirect addr
        assert((op->first->assert_type == LIR_OPERAND_VAR || op->second->assert_type == LIR_OPERAND_VAR) &&
               "cmp must have var, var can allocate registers");

        if (var->flag & FLAG(VR_FLAG_FIRST)) {
            return USE_KIND_MUST;
        }

        // second 只能是在 first 非 var 的期刊下才能分配寄存器
        if (var->flag & FLAG(VR_FLAG_SECOND) && op->first->assert_type != LIR_OPERAND_VAR) {
            // 优先将寄存器分配给 first, 仅当 first 不是 var 时才分配给 second
            return USE_KIND_MUST;
        }
    }

    // var 是 indirect addr 的 base 部分， native indirect addr 则必须借助寄存器
    if (var->flag & FLAG(VR_FLAG_INDIRECT_ADDR_BASE)) {
        return USE_KIND_MUST;
    }

    return USE_KIND_SHOULD;
}

static bool in_range(interval_range_t *range, int position) {
    return range->from <= position && position < range->to;
}

// 大值在栈顶被优先处理 block_to_stack
static void block_to_depth_stack(stack_t *work_list, basic_block_t *block) {
    // next->next->next
    stack_node *p = work_list->top; // top 指向栈中的下一个可用元素，总是为 NULL
    while (p->next != NULL && ((basic_block_t *) p->next->value)->loop.depth > block->loop.depth) {
        p = p->next;
    }

    // p->next == NULL 或者 p->next 小于等于 当前 block
    // block 插入到 p = 3 -> new = 2 ->  p->next = 2
    stack_node *next_node = p->next;
    // 初始化一个 node
    stack_node *new_node = stack_new_node(block);
    p->next = new_node;
    work_list->count++;

    if (next_node != NULL) {
        new_node->next = next_node;
    }
}

// 优秀的排序从而构造更短更好的 lifetime interval
// 权重越大排序越靠前
// 权重的本质是？或者说权重越大一个基本块？
void interval_block_order(closure_t *c) {
    loop_detect(c);

    slice_t *order_blocks = slice_new();
    stack_t *work_list = stack_new();
    stack_push(work_list, c->entry);

    while (!stack_empty(work_list)) {
        basic_block_t *block = stack_pop(work_list);
        assertf(block && block->first_op, "block or block->first_op is null");

        slice_push(order_blocks, block);

        // 需要计算每一个块的正向前驱的数量
        for (int i = 0; i < block->forward_succs->count; ++i) {
            basic_block_t *succ = block->forward_succs->take[i];
            succ->incoming_forward_count--;
            // 如果一个块的正向进入的节点已经处理完毕了，则将当前块压入到栈中
            if (succ->incoming_forward_count == 0) {
                // sort into work_list by loop.depth, 权重越大越靠前，越先出栈
                block_to_depth_stack(work_list, succ);
            }
        }
    }

    c->blocks = order_blocks;
}

void interval_mark_number(closure_t *c) {
    int next_id = 0;
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        linked_node *current = linked_first(block->operations);
        lir_op_t *label_op = current->value;
        assert(label_op->code == LIR_OPCODE_LABEL);

        while (current->value != NULL) {
            lir_op_t *op = current->value;
            if (op->code == LIR_OPCODE_PHI) {
                op->id = label_op->id;
                current = current->succ;
                continue;
            }

            op->id = next_id;
            next_id += 2;
            current = current->succ;
        }
    }
}

void interval_build(closure_t *c) {
    // new_interval for all physical registers
    for (int reg_id = 1; reg_id < alloc_reg_count(); ++reg_id) {
        reg_t *reg = alloc_regs[reg_id];
        interval_t *interval = interval_new(c);
        interval->index = reg_id;
        interval->fixed = true;
        interval->assigned = reg_id;
        assertf(reg->flag & (FLAG(VR_FLAG_ALLOC_FLOAT) | FLAG(VR_FLAG_ALLOC_INT)), "reg must be alloc float or int");
        interval->alloc_type = reg->flag & FLAG(VR_FLAG_ALLOC_FLOAT) ? VR_FLAG_ALLOC_FLOAT : VR_FLAG_ALLOC_INT;
        table_set(c->interval_table, reg->name, interval);
    }

    // new interval for all virtual registers in closure
    for (int i = 0; i < c->globals->count; ++i) {
        lir_var_t *var = c->globals->take[i];
        interval_t *interval = interval_new(c);
        interval->var = var;
        interval->alloc_type = type_base_trans_alloc(var->type.kind);
        table_set(c->interval_table, var->ident, interval);
    }

    // 倒序遍历顺序基本块基本块
    for (int i = c->blocks->count - 1; i >= 0; --i) {
        basic_block_t *block = c->blocks->take[i];
        slice_t *live = slice_new();

        // 1. calc live in = union of successor.liveIn for each successor of b
        table_t *exist_vars = table_new();
        for (int j = 0; j < block->succs->count; ++j) {
            basic_block_t *succ = block->succs->take[j];
            for (int k = 0; k < succ->live->count; ++k) {
                lir_var_t *var = succ->live->take[k];
                live_add(exist_vars, live, var);
            }
        }

        // 2. phi function phi of successors of b do
        for (int j = 0; j < block->succs->count; ++j) {
            basic_block_t *succ_block = block->succs->take[j];
            linked_node *current = linked_first(succ_block->operations)->succ;
            while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
                lir_op_t *op = OP(current);
                // TODO ssh_phi_body_of 有问题！
                lir_var_t *var = ssa_phi_body_of(op->first->value, succ_block->preds, block);
                live_add(exist_vars, live, var);

                current = current->succ;
            }
        }


        int block_from = OP(linked_first(block->operations))->id;
        int block_to = OP(block->last_op)->id + 2; // whether add 2?

        // live in add full range 遍历所有的 live(union all succ, so it similar live_out),直接添加最长间隔,后面会逐渐缩减该间隔
        for (int k = 0; k < live->count; ++k) {
            lir_var_t *var = live->take[k];
            interval_t *interval = table_get(c->interval_table, var->ident);
            interval_add_range(c, interval, block_from, block_to);
        }

        // 倒序遍历所有块指令
        linked_node *current = linked_last(block->operations);
        while (current != NULL && current->value != NULL) {
            // 判断是否是 call op,是的话就截断所有物理寄存器
            lir_op_t *op = current->value;

            // fixed all phy reg in call
            if (lir_op_is_call(op)) {
                // traverse all register
                for (int j = 1; j < alloc_reg_count(); ++j) {
                    reg_t *reg = alloc_regs[j];
                    interval_t *interval = table_get(c->interval_table, reg->name);
                    if (interval != NULL) {
                        interval_add_range(c, interval, op->id, op->id + 1);
                    }
                }
            }

            // add reg hint for move
            if (op->code == LIR_OPCODE_MOVE) {
                interval_t *src_interval = operand_interval(c, op->first);
                interval_t *dst_interval = operand_interval(c, op->output);
                if (src_interval != NULL && dst_interval != NULL) {
                    dst_interval->reg_hint = src_interval;
                }
            }

            // TODO add reg hint for phi, but phi a lot of input var?

            // interval by output params, so it contain opcode phi
            // 可能存在变量定义却未使用的情况, 此时直接加 op->id, op->id_1 即可
            // ssa 完成后会拿一个 pass 进行不活跃的变量进行清除
            slice_t *def_operands = lir_op_operands(op, FLAG(LIR_OPERAND_VAR) | FLAG(LIR_OPERAND_REG),
                                                    FLAG(VR_FLAG_DEF), false);
            for (int j = 0; j < def_operands->count; ++j) {
                lir_operand_t *operand = def_operands->take[j];
                interval_t *interval = operand_interval(c, operand);
                if (!interval) {
                    continue;
                }

                // first range 为 null,表示仅定义，未使用
                if (interval->first_range == NULL) {
                    interval_add_range(c, interval, op->id, op->id + 1);
                } else {
                    interval->first_range->from = op->id;
                }

                if (!interval->fixed) {
                    assertf(operand->assert_type == LIR_OPERAND_VAR, "only var can be live");
                    live_remove(exist_vars, live, interval->var);
                    interval_add_use_pos(c, interval, op->id, use_kind_of_def(c, op, operand->value));
                }
            }


            slice_t *use_operands = lir_op_operands(op, FLAG(LIR_OPERAND_VAR) | FLAG(LIR_OPERAND_REG),
                                                    FLAG(VR_FLAG_USE), false);
            for (int j = 0; j < use_operands->count; ++j) {
                lir_operand_t *operand = use_operands->take[j];
                interval_t *interval = operand_interval(c, operand);
                if (!interval) {
                    continue;
                }

                interval_add_range(c, interval, block_from, op->id);

                if (!interval->fixed) {
                    assertf(operand->assert_type == LIR_OPERAND_VAR, "only var can be live");
                    live_add(exist_vars, live, interval->var);
                    interval_add_use_pos(c, interval, op->id, use_kind_of_use(c, op, operand->value));
                }
            }

            current = current->prev;
        }

        // live in 中不能包含 phi output
        current = linked_first(block->operations)->succ;
        while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
            lir_op_t *op = OP(current);

            lir_var_t *var = op->output->value;
            live_remove(exist_vars, live, var);
            current = current->succ;
        }

        /**
         * 由于采取了倒序遍历的方式，loop end 已经被遍历过了，但是 header 却还没有被处理。
         */
        if (block->loop.header) {
            for (int j = 0; j < block->loop_ends->count; ++j) {
                basic_block_t *end = block->loop_ends->take[j];
                for (int k = 0; k < live->count; ++k) {
                    lir_var_t *var = live->take[k];
                    interval_t *interval = table_get(c->interval_table, var->ident);
                    interval_add_range(c, interval, block_from, OP(end->last_op)->id + 2);
                }
            }
        }
        block->live = live;
    }
}

interval_t *interval_new(closure_t *c) {
    interval_t *i = malloc(sizeof(interval_t));
    memset(i, 0, sizeof(interval_t));
    i->ranges = linked_new();
    i->use_pos_list = linked_new();
    i->children = linked_new();
    i->stack_slot = NEW(int);
    *i->stack_slot = 0;
    i->spilled = false;
    i->fixed = false;
    i->parent = NULL;
    i->index = c->interval_count++; // 基于 closure_t 做自增 id 即可
    return i;
}

bool range_covered(interval_range_t *range, int position, bool is_input) {
    if (is_input) {
        position -= 1;
    }

    // range to 不包含在 range 里面
    if (range->from <= position && position < range->to) {
        return true;
    }
    return false;
}

bool interval_expired(interval_t *i, int position, bool is_input) {
    if (is_input) {
        position -= 1;
    }

    int last_to = i->last_range->to; // interval < last_to
    // 由于 interval < last_to, 所以 position == last_to 时，interval 已经开始 expired 了
    return position >= last_to;
}


bool interval_covered(interval_t *i, int position, bool is_input) {
    linked_node *current = linked_first(i->ranges);
    while (current->value != NULL) {
        interval_range_t *range = current->value;
        if (range_covered(range, position, is_input)) {
            return true;
        }

        current = current->succ;
    }
    return false;
}

int interval_next_intersection(interval_t *current, interval_t *select) {
    int position = current->first_range->from; // first_from 指向 range 的开头
    while (position < current->last_range->to) {
        if (interval_covered(current, position, false) && interval_covered(select, position, false)) {
            return position;
        }
        position++;
    }

    return 0;
}

// 在 before 前挑选一个最佳的位置进行 split
int interval_find_optimal_split_pos(closure_t *c, interval_t *current, int before) {
    return before - 1;
}

/**
 * add range 总是基于 block_from ~ input.id,
 * 且从后往前遍历，所以只需要根据 to 和 first_from 比较，就能判断出是否重叠
 * @param c
 * @param i
 * @param from
 * @param to
 */
void interval_add_range(closure_t *c, interval_t *i, int from, int to) {
    assert(from < to);

    if (linked_empty(i->ranges)) {
        interval_range_t *range = NEW(interval_range_t);
        range->from = from;
        range->to = to;
        linked_push(i->ranges, range);
        i->first_range = range;
        i->last_range = range;
        return;
    }

    if (i->first_range->from <= to) {
        // form 选小的， to 选大的
        if (from < i->first_range->from) {
            i->first_range->from = from;
        }
        if (to > i->first_range->to) {
            i->first_range->to = to;

            // to 可能跨越了多个 range
            linked_node *current = linked_first(i->ranges)->succ;
            while (current->value && ((interval_range_t *) current->value)->from <= to) {
                i->first_range->to = ((interval_range_t *) current->value)->to;
                linked_remove(i->ranges, current);

                current = current->succ;
            }
            // 重新计算 last range
            i->last_range = linked_last(i->ranges)->value;
        }


    } else {
        // 不重叠,则 range 插入到 ranges 的最前面
        interval_range_t *range = NEW(interval_range_t);
        range->from = from;
        range->to = to;

        linked_insert_before(i->ranges, NULL, range);
        i->first_range = range;
        if (i->ranges->count == 1) {
            i->last_range = range;
        }
    }
}

/**
 * 按从小到大排序
 * @param c
 * @param i
 * @param position
 * @param kind
 */
void interval_add_use_pos(closure_t *c, interval_t *i, int position, use_kind_e kind) {
    linked_t *pos_list = i->use_pos_list;

    use_pos_t *new_pos = NEW(use_pos_t);
    new_pos->kind = kind;
    new_pos->value = position;
    if (linked_empty(pos_list)) {
        linked_push(pos_list, new_pos);
        return;
    }

    linked_node *current = linked_first(pos_list);
    while (current->value != NULL && ((use_pos_t *) current->value)->value < position) {
        current = current->succ;
    }

    linked_insert_before(pos_list, current, new_pos);
}


int interval_next_use_position(interval_t *i, int after_position) {
    linked_t *pos_list = i->use_pos_list;

    LINKED_FOR(pos_list) {
        use_pos_t *current_pos = LINKED_VALUE();
        if (current_pos->value > after_position) {
            return current_pos->value;
        }
    }
    return 0;
}

/**
 * 从 position 将 interval 分成两端，多个 child interval 在一个 list 中，而不是多级 list
 * 如果 position 被 range cover, 则对 range 进行切分
 * child 的 register_hint 指向 parent
 * @param c
 * @param i
 * @param position
 */
interval_t *interval_split_at(closure_t *c, interval_t *i, int position) {
    assert(position < i->last_range->to);
    assert(position >= i->first_range->from);

    interval_t *child = interval_new_child(c, i);

    // mov id = position - 1
    closure_insert_mov(c, position, i, child);

    // 将 child 加入 parent 的 children 中,
    // 因为是从 i 中分割出来的，所以需要插入到 i 对应到 node 的后方
    interval_t *parent = i;
    if (parent->parent) {
        parent = parent->parent;
    }
    if (linked_empty(parent->children)) {
        linked_push(parent->children, child);
    } else {
        LINKED_FOR(parent->children) {
            interval_t *current = LINKED_VALUE();
            if (current->index == i->index) {
                linked_insert_after(parent->children, LINKED_NODE(), child);
                break;
            }
        }
    }

    // 切割 range
    LINKED_FOR(i->ranges) {
        interval_range_t *range = LINKED_VALUE();
        if (position <= range->from) {
            continue;
        }

        // position 大于 range->from, 此时有三种情况，
        // pos == range->from
        //  range->from < pos < range->to
        //  range->to <= pos, pos 等于 range->to 就表示没有被 range 覆盖

        // 如果 position 在 range 的起始位置，则直接将 ranges list 的当前部分和剩余部分分给 child interval 即可
        // 否则 对 range 进行切割
        // tips: position 必定不等于 range->to, 因为 to 是 excluded
        if (position == range->from) {
            child->ranges = linked_split(i->ranges, LINKED_NODE());
        } else if (position >= range->to) {
            child->ranges = linked_split(i->ranges, LINKED_NODE()->succ);
        } else {
            // new range for child
            interval_range_t *new_range = NEW(interval_range_t);
            new_range->from = position;
            new_range->to = range->to;


            range->to = position; // 截短

            // 将 new_range 插入到 ranges 中
            linked_insert_after(i->ranges, LINKED_NODE(), new_range);

            child->ranges = linked_split(i->ranges, LINKED_NODE()->succ);
        }

        child->first_range = linked_first(child->ranges)->value;
        child->last_range = linked_last(child->ranges)->value;

        i->first_range = linked_first(i->ranges)->value;
        i->last_range = linked_last(i->ranges)->value;
        break;
    }

    // 划分 position
    LINKED_FOR(i->use_pos_list) {
        use_pos_t *pos = LINKED_VALUE();
        if (pos->value < position) {
            continue;
        }

        // pos->value >= position, pos 和其之后的 pos 都需要加入到 new child 中
        child->use_pos_list = linked_split(i->use_pos_list, LINKED_NODE());
        break;
    }


    return child;
}

/**
 * spill slot，清空 assign
 * 所有 slit_child 本质上是一个 var, 所以都溢出到同一个 stack_slot（存储在_canonical_spill_slot中）
 * @param i
 */
void interval_spill_slot(closure_t *c, interval_t *i) {
    assert(i->stack_slot);

    i->assigned = 0;
    i->spilled = true;
    if (*i->stack_slot != 0) { // 已经分配了 slot 了
        return;
    }
    // TODO 将 stack var 信息记录在 closure 中
    // TODO 进行 stack offset 对齐
    // 根据 closure stack slot 分配堆栈插槽,暂时不用考虑对齐，直接从 0 开始分配即可
    c->stack_size += type_kind_sizeof(i->var->type.kind);
    *i->stack_slot = -c->stack_size; // 取负数，一般栈都是高往低向下增长
}

/**
 * use_positions 是否包含 kind > 0 的position, 有则返回 use_position，否则返回 NULL
 * @param i
 * @return
 */
use_pos_t *interval_must_reg_pos(interval_t *i) {
    LINKED_FOR(i->use_pos_list) {
        use_pos_t *pos = LINKED_VALUE();
        if (pos->kind == USE_KIND_MUST) {
            return pos;
        }
    }
    return NULL;
}

/**
 * @param i
 * @return
 */
use_pos_t *interval_must_stack_pos(interval_t *i) {
    LINKED_FOR(i->use_pos_list) {
        use_pos_t *pos = LINKED_VALUE();
        if (pos->kind == USE_KIND_NOT) {
            return pos;
        }
    }
    return NULL;
}


void resolve_data_flow(closure_t *c) {
    SLICE_FOR(c->blocks) {
        basic_block_t *from = SLICE_VALUE(c->blocks);
        for (int i = 0; i < from->succs->count; ++i) {
            basic_block_t *to = from->succs->take[i];

            resolver_t r = {
                    .from_list = slice_new(),
                    .to_list = slice_new(),
                    .insert_block = NULL,
                    .insert_id = 0,
            };

            // to 入口活跃则可能存在对同一个变量在进入到当前块之前就已经存在了，所以可能会进行 spill/reload
            // for each interval it live at begin of successor do ? 怎么拿这样的 interval? 最简单办法是通过 live
            // live not contain phi def interval
            for (int j = 0; j < to->live->count; ++j) {
                lir_var_t *var = to->live->take[j];
                interval_t *parent_interval = table_get(c->interval_table, var->ident);
                assert(parent_interval);

                // 判断是否在 form->to edge 最终的 interval TODO last_op + 1?
                interval_t *from_interval = interval_child_at(parent_interval, OP(from->last_op)->id + 1, false);
                interval_t *to_interval = interval_child_at(parent_interval, OP(to->first_op)->id, false);
                // 因为 from 和 interval 是相连接的 edge,
                // 如果from_interval != to_interval(指针对比即可)
                // 则说明在其他 edge 上对 interval 进行了 spilt/reload
                // 因此需要 link from and to interval
                if (interval_need_move(from_interval, to_interval)) {
                    slice_push(r.from_list, from_interval);
                    slice_push(r.to_list, to_interval);
                }
            }

            // phi def interval(label op -> phi op -> ... -> phi op -> other op)
            linked_node *current = linked_first(to->operations)->succ;
            while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
                lir_op_t *op = OP(current);
                //  to phi.inputOf(pred def) will is from interval
                // TODO ssa body constant handle
                lir_var_t *var = ssa_phi_body_of(op->first->value, to->preds, from);
                interval_t *temp_interval = table_get(c->interval_table, var->ident);
                assert(temp_interval);
                interval_t *from_interval = interval_child_at(temp_interval, OP(from->last_op)->id, false);

                lir_var_t *def = op->output->value; // result must assign reg
                temp_interval = table_get(c->interval_table, def->ident);
                assert(temp_interval);
                interval_t *to_interval = interval_child_at(temp_interval, OP(to->first_op)->id, false);

                if (interval_need_move(from_interval, to_interval)) {
                    slice_push(r.from_list, from_interval);
                    slice_push(r.to_list, to_interval);
                }

                current = current->succ;
            }


            // 对一条边的处理完毕，可能涉及多个寄存器，stack, 也有可能一个寄存器被多次操作,所以需要处理覆盖问题
            // 同一条边上的所有 resolve 操作只插入在同一块中，要么是在 from、要么是在 to。 tips: 所有的 critical edge 都已经被处理了
            resolve_find_insert_pos(&r, from, to);

            // 按顺序插入 move
            resolve_mappings(c, &r);
        }
    }
}

/**
 * 如果是 input 则 使用 op_id < last_range->to+1
 * output 则不用
 * @param i
 * @param op_id
 * @return
 */
interval_t *interval_child_at(interval_t *i, int op_id, bool is_use) {
    assert(op_id >= 0 && "invalid op_id (method can not be called for spill moves)");

    if (linked_empty(i->children)) {
        return i;
    }

    int last_to_offset = is_use ? 1 : 0;

    if (i->first_range->from <= op_id && op_id < (i->last_range->to + last_to_offset)) {
        return i;
    }

    // i->var 在不同的指令处可能作为 input 也可能作为 output
    // 甚至在同一条指令处即作为 input，又作为 output， 比如 20: v1 + 1 -> v2
    LINKED_FOR(i->children) {
        interval_t *child = LINKED_VALUE();
        if (child->first_range->from <= op_id && op_id < (child->last_range->to + last_to_offset)) {
            return child;
        }
    }

    assert(false && "op_id not in interval");
}

/**
 * 由于 ssa resolve 的存在，所以存在从 interval A(stack A) 移动到 interval B(stackB)
 * 但是大多数寄存器不支持从 stack 移动到 stack，所以必须给 phi def 添加一个寄存器，也就是 use_pos kind = MUST
 *
 * 可能存在类似下面这样的冲突，此时无论先操作哪个移动指令都会造成 overwrite
 * mov rax -> rcx
 * mov rcx -> rax
 * 修改为
 * mov rax -> stack
 * mov stack -> rcx
 * mov rcx -> rax
 * @param c
 * @param r
 */
void resolve_mappings(closure_t *c, resolver_t *r) {
    if (r->from_list->count == 0) {
        return;
    }

    // block all from interval, value 表示被 input 引用的次数, 0 表示为被引用
    // 避免出现同一个寄存器的 output 覆盖 input
    int8_t block_regs[UINT8_MAX] = {0};

    SLICE_FOR(r->from_list) {
        interval_t *i = SLICE_VALUE(r->from_list);
        if (i->assigned) {
            block_regs[i->assigned] += 1;
        }
    }

    int spill_candidate = -1;
    while (r->from_list->count > 0) {
        bool processed = false;
        for (int i = 0; i < r->from_list->count; ++i) {
            interval_t *from = r->from_list->take[i];
            interval_t *to = r->to_list->take[i];
            assert(!(from->spilled && to->spilled) && "cannot move stack to stack");

            if (resolve_blocked(block_regs, from, to)) {
                // this interval cannot be processed now because target is not free
                // it starts in a register, so it is a possible candidate for spilling
                spill_candidate = i;
                continue;
            }

            block_insert_mov(r->insert_block, r->insert_id, from, to);

            if (from->assigned) {
                block_regs[from->assigned] -= 1;
            }

            slice_remove(r->from_list, i);
            slice_remove(r->to_list, i);

            processed = true;
        }

        // 已经卡死了，那进行再多次尝试也是没有意义的
        if (!processed) {
            assert(spill_candidate != -1 && "cannot resolve mappings");
            interval_t *from = r->from_list->take[spill_candidate];
            interval_t *spill_child = interval_new_child(c, from);
            interval_add_range(c, spill_child, 1, 2);
            interval_spill_slot(c, spill_child);

            // insert mov
            block_insert_mov(r->insert_block, r->insert_id, from, spill_child);

            // from update
            r->from_list->take[spill_candidate] = spill_child;

            // unlock interval reg
            block_regs[from->assigned] -= 1;
        }
    }

}

void resolve_find_insert_pos(resolver_t *r, basic_block_t *from, basic_block_t *to) {
    if (from->succs->count <= 1) {
        // from only one succ
        // insert before branch
        r->insert_block = from;

        lir_op_t *last_op = from->last_op->value;
        if (lir_op_is_branch(last_op)) {
            // insert before last op
            r->insert_id = last_op->id - 1;
        } else {
            r->insert_id = last_op->id + 1;
        }

    } else {
        r->insert_block = to;
        r->insert_id = OP(to->first_op)->id - 1;
    }
}

use_pos_t *first_use_pos(interval_t *i, use_kind_e kind) {
    assert(i->use_pos_list->count > 0);

    if (!kind) {
        return linked_first(i->use_pos_list)->value;
    }

    LINKED_FOR(i->use_pos_list) {
        use_pos_t *pos = LINKED_VALUE();
        if (pos->kind == kind) {
            return pos;
        }
    }

    assert(false && "no use pos found");
}