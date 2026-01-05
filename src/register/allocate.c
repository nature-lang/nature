#include "allocate.h"
#include <assert.h>

/**
 * 根据 interval 列表 初始化 unhandled 列表
 * 采用链表结构是因为跟方便排序插入，有序遍历
 * @return
 */
static linked_t *unhandled_new(closure_t *c) {
    linked_t *unhandled = linked_new();
    table_t *exists = table_new();
    // 遍历所有变量,根据 interval from 进行排序
    for (int i = 0; i < c->var_defs->count; ++i) {
        lir_var_t *var = c->var_defs->take[i];
        if (table_exist(exists, var->ident)) {
            assertf(false, "duplicate var %s register interval", var->ident);
        }

        interval_t *item = table_get(c->interval_table, var->ident);
        assert(item);

        table_set(exists, var->ident, var);

        sort_to_unhandled(unhandled, item);
    }

    return unhandled;
}

/**
 * 所有的 fixed interval 在初始化时加入到 inactive 中,后续计算相交时都是使用 inactive 计算的
 * @return
 */
static linked_t *inactive_new(closure_t *c) {
    linked_t *inactive = linked_new();

    // 遍历所有固定寄存器生成 fixed_interval
    for (int i = 1; i < alloc_reg_count(); ++i) {
        reg_t *reg = alloc_regs[i];
        interval_t *interval = table_get(c->interval_table, reg->name);
        assert(interval && "physic reg interval not found");

        // 如果一个物理寄存器从未被使用过,就没有 ranges
        // 所以也不需要写入到 inactive 中进行处理
        if (interval->first_range == NULL) {
            continue;
        }

        // free_pos = int_max
        linked_push(inactive, interval);
    }

    return inactive;
}

/**
 * @param operand
 * @param i
 */
void var_replace(lir_operand_t *operand, interval_t *i) {
    lir_var_t *var = operand->value;
    if (i->spilled) {
        lir_stack_t *stack = NEW(lir_stack_t);
        stack->slot = *i->stack_slot;
        stack->size = type_kind_sizeof(var->type.kind);
        stack->kind = var->type.kind;
        operand->assert_type = LIR_OPERAND_STACK;
        operand->value = stack;
        operand->size = stack->size;
        operand->ref_var = var;
    } else {
        // 如果 var 有 must_hint，直接使用目标寄存器而非分配的寄存器
        // 这样 mov x0 -> x0(v1) 会被自动消除
        reg_t *reg;
        if (var->must_hint != NULL) {
            reg = var->must_hint;
        } else {
            reg = alloc_regs[i->assigned];
        }
        assert(reg);

        uint8_t index = reg->index;
        reg = reg_select(index, var->type.kind);

        assert(reg);

        operand->assert_type = LIR_OPERAND_REG;
        operand->value = reg;
        operand->size = type_kind_sizeof(var->type.kind); // 实际 size
        operand->ref_var = var;
    }
}

/**
 * 智能查找 hint 寄存器: 查找 reg_hint 及其 children，以及 phi_hints
 * 类似 C1 Linear Scan 的 Interval::register_hint(bool search_split_child) 策略
 * @param current 当前需要分配寄存器的 interval
 * @return hint 对应的 reg_id，如果没有找到返回 0
 */
static uint8_t find_hint_reg(interval_t *current) {
    uint8_t hint_reg_id = 0;

    // 策略 1: 检查 reg_hint
    interval_t *hint = current->reg_hint;
    if (hint != NULL) {
        // 如果 hint 本身已经分配了寄存器
        if (hint->assigned > 0) {
            return hint->assigned;
        }

        // 如果 hint 没有分配，但是它的 children 中有已分配的
        // 这对应 hint 被 split 后，child 获得了寄存器的情况
        if (hint->children != NULL && hint->children->count > 0) {
            linked_node *child_node = linked_first(hint->children);
            while (child_node->value != NULL) {
                interval_t *child = (interval_t *) child_node->value;
                if (child->assigned > 0) {
                    return child->assigned;
                }
                child_node = child_node->succ;
            }
        }
    }

    // 策略 2: 从 phi_hints 中查找
    // phi_hints 是 phi def interval 对应的多个 body interval
    if (current->phi_hints != NULL && current->phi_hints->count > 0) {
        for (int i = 0; i < current->phi_hints->count; ++i) {
            interval_t *phi_hint = current->phi_hints->take[i];
            if (phi_hint->assigned > 0) {
                return phi_hint->assigned;
            }
            // 也检查 phi_hint 的 children
            if (phi_hint->children != NULL && phi_hint->children->count > 0) {
                linked_node *child_node = linked_first(phi_hint->children);
                while (child_node->value != NULL) {
                    interval_t *child = (interval_t *) child_node->value;
                    if (child->assigned > 0) {
                        return child->assigned;
                    }
                    child_node = child_node->succ;
                }
            }
        }
    }

    return hint_reg_id;
}

/**
 * 在 free 中找到一个尽量空闲的寄存器分配给 current, 优先考虑 register hint 分配的寄存器
 * 如果相应的寄存器不能使用到 current 结束
 * @param current
 * @param free_pos
 * @return
 */
static uint8_t find_free_reg(interval_t *current, int *free_pos) {
    uint8_t full_reg_id = 0; // 能够用于整个 current lifetime 的寄存器中空闲时间最长的
    uint8_t max_part_reg_id = 0; // 需要 split current to unhandled

    // 使用智能查找策略获取 hint 寄存器
    uint8_t hint_reg_id = find_hint_reg(current);

    for (int i = 1; i < alloc_reg_count(); ++i) {
        if (free_pos[i] >= current->last_range->to) {
            // 寄存器在整个 interval 生命周期内都空闲
            // 优先选择 hint 寄存器，其次选择空闲时间最长的
            if (full_reg_id == 0 || i == hint_reg_id ||
                (full_reg_id != hint_reg_id && free_pos[i] > free_pos[full_reg_id])) {
                full_reg_id = i;
            }
        } else if (free_pos[i] >= current->first_range->from + 1) {
            // 寄存器部分时间空闲，需要后续 split
            // 优先选择 hint 寄存器，其次选择空闲时间最长的
            if (max_part_reg_id == 0 || i == hint_reg_id ||
                (max_part_reg_id != hint_reg_id && free_pos[i] > free_pos[max_part_reg_id])) {
                max_part_reg_id = i;
            }
        }
    }

    if (full_reg_id > 0) {
        return full_reg_id;
    }

    if (max_part_reg_id > 0) {
        return max_part_reg_id;
    }

    return 0;
}

/**
 * 只要找到一个在 current 第一次使用时空闲的寄存器即可
 * @param current
 * @param free_pos
 * @return
 */
static uint8_t find_block_reg(interval_t *current, int *use_pos, int *block_pos) {
    uint8_t max_reg_id = 0; // 直接分配不用 split

    // 使用智能查找策略获取 hint 寄存器
    uint8_t hint_reg_id = find_hint_reg(current);

    for (int i = 1; i < alloc_reg_count(); ++i) {
        if (use_pos[i] <= current->first_range->from + 1) {
            continue;
        }

        if (max_reg_id == 0 || i == hint_reg_id ||
            (max_reg_id != hint_reg_id && use_pos[i] > use_pos[max_reg_id])) {
            max_reg_id = i;
        }
    }

    return max_reg_id;
}


static void handle_active(allocate_t *a) {
    // output position
    int position = a->current->first_range->from;
    linked_node *current = a->active->front;
    while (current->value != NULL) {
        interval_t *select = (interval_t *) current->value;
        bool is_expired = interval_expired(select, position, false);
        bool is_covers = interval_covered(select, position, false);

        if (!is_covers || is_expired) {
            // TODO remove 了，current 就没有了，所以后面就不能再读取 succ
            linked_remove(a->active, current);

            if (is_expired) {
                linked_push(a->handled, select);
            } else {
                linked_push(a->inactive, select);
            }
        }

        current = current->succ;
    }
}

/**
 * 包含 fixed interval
 * @param a
 */
static void handle_inactive(allocate_t *a) {
    int position = a->current->first_range->from;
    linked_node *current = a->inactive->front;
    while (current->value != NULL) {
        interval_t *select = (interval_t *) current->value;
        bool is_expired = interval_expired(select, position, false);
        bool is_covers = interval_covered(select, position, false);

        if (is_covers || is_expired) {
            linked_remove(a->inactive, current);
            if (is_expired) {
                linked_push(a->handled, select);
            } else {
                linked_push(a->active, select);
            }
        }

        current = current->succ;
    }
}

static void set_pos(int *list, uint8_t index, int position) {
    if (list[index] != -1 && position > list[index]) {
        return;
    }

    list[index] = position;
}


/**
 * 在 before_pos 之前需要将 i 在一个合适的位置 split 一个 child，并将 child spilt spill 到内存中
 * 由于 spill 的部分后续不会在 handle，所以如果包含 use_position kind 则需要再次 spilt 丢出去
 * @param c
 * @param i
 * @param before_pos
 * @return
 */
static void spill_interval(closure_t *c, allocate_t *a, interval_t *i, int before_pos) {
    int split_pos;
    interval_t *child = i;
    if (before_pos > i->first_range->from) {
        // spill current before current first use position
        split_pos = interval_find_optimal_split_pos(c, i, before_pos);
        child = interval_split_at(c, i, split_pos);
    }

    /**
        TODO 必须要使用寄存器的部分是一个 output 值，会被覆盖，那这里溢出回去也没有意义呀！
        8		NOP    	STACK[-8|8]
        16		ADD  	REG[rcx], REG[rax] -> REG[rcx]
        17		MOVE 	STACK[-8|8] -> REG[rax] // 分配了寄存器，但是会被立刻覆盖
        18		MOVE 	REG[rcx] -> REG[rax]
     */
    // 必须要使用寄存器的部分被后分到 unhandled 中了
    use_pos_t *must_pos = interval_must_reg_pos(child);
    if (must_pos) {
        split_pos = interval_find_optimal_split_pos(c, child, must_pos->value);
        assert(split_pos < must_pos->value);
        interval_t *unhandled = interval_split_at(c, child, split_pos);
        sort_to_unhandled(a->unhandled, unhandled);
    }

    // child to slot
    interval_spill_slot(c, child);
}

/**
 * @param c
 * @param a
 * @param i
 * @param assigned_reg
 */
static void assign_interval(closure_t *c, allocate_t *a, interval_t *i, uint8_t assigned) {
    assertf(assigned > 0, "assign reg id must > 0");
    i->assigned = assigned; // interval 已经成果分配到了寄存器 assigned id,

    use_pos_t *stack_pos = interval_must_stack_pos(i); // 但是部分 pos 中，var 必须保持在 stack 中！
    if (!stack_pos) {
        return;
    }

    // spill current before current first use position
    spill_interval(c, a, i, stack_pos->value);
}

void allocate_walk(closure_t *c) {
    allocate_t *a = malloc(sizeof(allocate_t));
    a->unhandled = unhandled_new(c);
    a->handled = linked_new();
    a->active = linked_new();
    a->inactive = inactive_new(c);

    while (a->unhandled->count != 0) {
        a->current = (interval_t *) linked_pop(a->unhandled);
        //        assertf(a->current->assigned == 0, "interval must not assigned");
        if (a->current->assigned) {
            linked_push(a->handled, a->current);
            continue;
        }

        // handle active
        handle_active(a);
        // handle inactive
        handle_inactive(a);

        use_pos_t *first_use = first_use_pos(a->current, 0);
        if (!first_use || first_use->kind == ALLOC_KIND_STACK) {
            spill_interval(c, a, a->current, 0);
            linked_push(a->handled, a->current);
            continue;
        }
        // interval 使用时间过短，无法分配寄存器
        use_pos_t *first_not = first_use_pos(a->current, ALLOC_KIND_STACK);
        if (first_use->kind != ALLOC_KIND_MUST && first_not && first_not - first_use < ALLOC_USE_MIN) {
            spill_interval(c, a, a->current, 0);
            linked_push(a->handled, a->current);
            continue;
        }

        if (a->current->is_const_float) {
            spill_interval(c, a, a->current, 0);
            linked_push(a->handled, a->current);
            continue;
        }

        // current is phi def, set reg_hint
        if (a->current->phi_hints->count > 0) {
            for (int i = 0; i < a->current->phi_hints->count; ++i) {
                interval_t *hint_interval = a->current->phi_hints->take[i];
                if (hint_interval->assigned) {
                    a->current->reg_hint = hint_interval;
                    continue;
                }
            }
        }

        // 尝试为 current 分配寄存器
        bool processed = allocate_free_reg(c, a);
        if (processed) {
            continue;
        }

        // 如果当前的 var 的使用时间长,则可以考虑从正在使用的寄存器中抢一个过来
        bool allocated = allocate_block_reg(c, a);
        if (allocated) {
            linked_push(a->active, a->current);
            continue;
        }

        // 分不到寄存器，只能 spill 了， spill 的 interval 放到 handled 中，再也不会被 traverse 了
        linked_push(a->handled, a->current);
    }
}

/**
 * 将 to 根据 interval 的 from 字段排序，值越小越靠前
 * i1 < i2 < i3 < to < i4 < i5
 * @param unhandled
 * @param to
 */
void sort_to_unhandled(linked_t *unhandled, interval_t *to) {
    if (unhandled->count == 0) {
        linked_push(unhandled, to);
        return;
    }

    linked_node *current = linked_first(unhandled);
    while (current->value != NULL && ((interval_t *) current->value)->first_range->from < to->first_range->from) {
        current = current->succ;
    }
    //  to < current, 将 to 插入到 current 前面
    linked_insert_before(unhandled, current, to);
}

static uint8_t max_pos_index(const int list[UINT8_MAX]) {
    uint8_t max_index = 0;
    for (int i = 1; i < UINT8_MAX; ++i) {
        if (list[i] > list[max_index]) {
            max_index = i;
        }
    }

    return max_index;
}

/**
 * 由于不需要 spill 任何寄存器，所以不需要考虑 fixed interval
 * @param c
 * @param a
 * @return
 */
bool allocate_free_reg(closure_t *c, allocate_t *a) {
    int free_pos[UINT8_MAX];
    memset(free_pos, -1, sizeof(free_pos));

    for (int i = 1; i < alloc_reg_count(); ++i) {
        reg_t *reg = alloc_regs[i];
        if (!(FLAG(a->current->alloc_type) & reg->flag)) {
            // already set 0
            continue;
        }
        set_pos(free_pos, i, INT32_MAX);
    }

    // active(已经分配到了 reg) interval 不予分配，所以 pos 设置为 0
    LINKED_FOR(a->active) {
        interval_t *select = LINKED_VALUE();
        if (select->alloc_type != a->current->alloc_type) {
            continue;
        }
        set_pos(free_pos, select->assigned, 0);
    }

    // ssa 表单中不会因为 redefine 产生 lifetime hole，只会由于 if-else block 产生少量的 hole
    LINKED_FOR(a->inactive) {
        interval_t *select = LINKED_VALUE();
        if (select->alloc_type != a->current->alloc_type) {
            continue;
        }

        int pos = interval_next_intersect(c, a->current, select);
        //        int old_pos = old_interval_next_intersect(c, a->current, select);
        assert(pos);
        // potions 表示两个 interval 重合，重合点之前都是可以自由分配的区域
        set_pos(free_pos, select->assigned, pos);
    }


    // 找到空闲时间最长的寄存器,返回空闲直接最长的寄存器的 id
    uint8_t reg_id = find_free_reg(a->current, free_pos);
    if (!reg_id || free_pos[reg_id] == 0) {
        // 最长空闲的寄存器也不空闲
        return false;
    }

    // free_pos[reg_id] 和 a->current->first_range->start 如果只有 N 个空间空闲，
    // 且不是 must kind 需求,可以不分配寄存器
    // 虽然有可用的空闲寄存器，但是空闲时间过短，此时直接进行溢出
    use_pos_t *first_use = first_use_pos(a->current, 0);
    if (first_use->kind != ALLOC_KIND_MUST && free_pos[reg_id] - a->current->first_range->from < ALLOC_USE_MIN) {
        spill_interval(c, a, a->current, 0);
        linked_push(a->handled, a->current);
        return true;
    }

    // current 是拆分过的寄存器范围，first_use 可能在找到的寄存器可用声明周期之后，此时分配寄存器是没有意义的
    // first_use 是否需要 must
    if (free_pos[reg_id] < first_use->value) {
        if (first_use->kind == ALLOC_KIND_MUST) { // first use 必须分配寄存器，所以需要 split 后再进行 spill
            int optimal_position = interval_find_optimal_split_pos(c, a->current, first_use->value);
            interval_t *child = interval_split_at(c, a->current, optimal_position);
            sort_to_unhandled(a->unhandled, child);
        }

        spill_interval(c, a, a->current, 0);
        linked_push(a->handled, a->current);
        return true;
    }

    // TODO last_range 之前可能存在 stack pos, find optimal split post 应该找到这之前
    // 有空闲的寄存器，但是空闲时间小于当前 current 的生命周期,需要 split current
    if (free_pos[reg_id] < a->current->last_range->to) {
        int optimal_position = interval_find_optimal_split_pos(c, a->current, free_pos[reg_id]);
        if (optimal_position == a->current->first_range->from) {
            optimal_position = free_pos[reg_id]; // 使用点进行切割
        }

        // 从最佳位置切割 interval 得到 child, child 并不是一定会溢出，而是可能会再次被分配到寄存器(加入到 unhandled 中)
        interval_t *child = interval_split_at(c, a->current, optimal_position);
        sort_to_unhandled(a->unhandled, child);
    }

    assign_interval(c, a, a->current, reg_id);
    linked_push(a->active, a->current);
    return true;
}

/**

 * @param c
 * @param a
 * @return
 */
bool allocate_block_reg(closure_t *c, allocate_t *a) {
    // 用于判断寄存器的空闲时间
    // key is physical register index, value is position
    int use_pos[UINT8_MAX] = {0}; // use_pos 一定小于等于 block_pos
    int block_pos[UINT8_MAX] = {0}; // 设置 block pos 的同时需要隐式设置 use pos
    memset(use_pos, -1, sizeof(use_pos));
    memset(block_pos, -1, sizeof(block_pos)); // 用于记录寄存器从什么时候开始 block

    for (int reg_id = 1; reg_id < alloc_reg_count(); ++reg_id) {
        reg_t *reg = alloc_regs[reg_id];
        if (!(FLAG(a->current->alloc_type) & reg->flag)) {
            continue;
        }
        // 这里包含了 use_pos 的设置
        SET_BLOCK_POS(reg_id, INT32_MAX);
    }

    int first_from = a->current->first_range->from;
    // 遍历固定寄存器(active) TODO 固定间隔也进不来呀？
    LINKED_FOR(a->active) {
        interval_t *select = LINKED_VALUE();
        if (select->alloc_type != a->current->alloc_type) {
            continue;
        }

        assert(a->current->index != select->index);

        // 固定间隔本身就是 short range 了，但如果还在 current pos is active,so will set that block and use to 0
        if (select->fixed) {
            // 正在使用中的 fixed register,所有使用了该寄存器的 interval 都要让路
            SET_BLOCK_POS(select->assigned, 0);
        } else {
            // 找一个大于 current first use_position 的位置(可以为0，0 表示没找到)
            // 查找 active interval 的下一个使用位置,用来判断其空闲时长
            assert(a->current->first_range->from > select->first_range->from);

            int pos = 0;
            // 如果 select->first_from 和 first from 的差值 < 1, 则后续 split 分配会异常, 无法在 first_from 之前讲 select 进行 split
            // select interval  42 ~ 48 (must alloc in 42)
            // current interval 43 ~ 46 (should alloc in 46)
            // 如果将 select 的寄存器分配给 current, 则 select 需要在 43 之前(可选位置只有 42) 进行 split, 这是无法切割的

            if (a->current->first_range->from - select->first_range->from <= 1) {
                pos = 0;
            } else {
                pos = interval_next_use_position(select, first_from);
            }


            SET_USE_POS(select->assigned, pos);
        }
    }

    // 遍历非固定寄存器(active intersect current)
    // 如果 lifetime hole 没有和 current intersect 在 allocate free 的时候已经用完了
    int pos;
    LINKED_FOR(a->inactive) {
        interval_t *select = LINKED_VALUE();
        if (select->alloc_type != a->current->alloc_type) {
            continue;
        }
        assert(a->current->index != select->index);

        pos = interval_next_intersect(c, a->current, select);
        //        int old_pos = old_interval_next_intersect(c, a->current, select);
        assert(pos);

        if (select->fixed) {
            // 该 interval 虽然是固定 interval(short range), 但是当前正处于 hole 中
            // 所以在 pos 之前的位置都是可以使用该 assigned 对应都寄存器
            // 但是一旦到了 pos 位置，current 必须 split at and spill child
            SET_BLOCK_POS(select->assigned, pos);
        } else {
            pos = interval_next_use_position(select, first_from);
            SET_USE_POS(select->assigned, pos);
        }
    }

    // max use pos 表示空闲时间最长的寄存器(必定会有一个大于 0 的值, 因为寄存器从 1 开始算的)
    uint8_t reg_id = find_block_reg(a->current, use_pos, block_pos);

    // 经过多次 split，如果 first_use 是 must，其一定会小于所有 use_pos 和 block_pos, 从而分配到可用寄存器,然后在瞬间溢出
    use_pos_t *first_use = first_use_pos(a->current, 0);
    assert(first_use);

    if (!reg_id || use_pos[reg_id] < first_use->value) {
        // 此处最典型的应用就是 current 被 call fixed interval 阻塞，需要溢出自身

        //  a->current,range 为 4 ~ 18, 且 4 是 mov output first use pos, 所以必须占用一个寄存器, 此时是否会进入到 use_pos[reg_id] < first_use ？
        //  假设所有的寄存器都被 active interval 占用， use_pos 记录的是 first_use + 1(指令之间的间隔是 2) 之后的使用位置(还在 active 就表示至少还有一个使用位置)
        //  则不可能出现，所有的 use_pos min <  first_use + 1, 必定是 use pos min(下一条指令) > first_use + 1, 即使 id = 6 指令是 call, 同样如此
        //  并不影响 id = 4 的位置拿一个寄存器来用。 call 指令并不会添加 use_pos, 只是添加了 range, 此时所有的物理寄存器被 block，
        if (first_use->kind == ALLOC_KIND_MUST && first_use->value == first_from) {
            assert(false && "cannot spill and spilt current, use_pos collect exception");
        }

        //  active/inactive interval 的下一个 pos 都早于 current first use pos, 所以最好直接 spill 整个 current
        // assign spill slot to current
        spill_interval(c, a, a->current, 0);
    } else if (block_pos[reg_id] >= a->current->last_range->to) { // TODO 测试
        // block_pos 是由于 fixed 强制使用而产生的 pos, 此时寄存器必须要溢出
        // 一般都会进入到这一条件中
        // reg_id 对应的寄存器的空闲时间 大于 current.first_use 但是小于 current->last_range->to
        // 不过 block_use[reg_id] 则是大于 current->last_range->to，所以可以将整个寄存器直接分配给 current， 不用考虑 block 的问题
        // 所有和 current intersecting 的 interval 都需要在 current start 之前 split 并且 spill 到内存中
        // 当然，如果 spilt child interval 存在 use pos 必须要加载 reg, 则需要二次 spilt into unhandled
        LINKED_FOR(a->active) {
            interval_t *i = LINKED_VALUE();
            if (i->assigned != reg_id) {
                continue;
            }

            // 检查 i 与 current 是否存在交集，如果存在交集 i 才需要进行在 first_from 之前进行 spill
            if (!interval_is_intersect(a->current, i)) {
                continue;
            }

            // 寄存器分配给了 current, 所以需要将 i 在 first_form 之前的部分 spill 到内存中
            // first_use 表示必须在 first_use 之前 spill, 否则会影响 current 使用 reg_id
            spill_interval(c, a, i, first_from);
        }

        LINKED_FOR(a->inactive) {
            interval_t *i = LINKED_VALUE();
            if (i->assigned != reg_id) {
                continue;
            }
            if (!interval_is_intersect(a->current, i)) {
                continue;
            }

            if (!i->fixed) {
                spill_interval(c, a, i, first_from);
            }
        }

        // assign register reg to interval current
        assign_interval(c, a, a->current, reg_id);
        return true;
    } else {
        // 1. current.first_use < use_pos < current.last_use
        // 2. block_pos < current.last_use
        // 虽然 first use pos < reg_index 对应的 interval 的使用位置，但是
        // current start ~ end 之间被 block_pos 所截断，所以必须 split  current in block pos 之前, child in to unhandled list
        // split and spill interval active/inactive intervals for reg
        LINKED_FOR(a->active) {
            interval_t *i = LINKED_VALUE();
            if (i->assigned != reg_id) {
                continue;
            }
            spill_interval(c, a, i, first_from);
        }
        LINKED_FOR(a->inactive) {
            interval_t *i = LINKED_VALUE();
            if (i->assigned != reg_id) {
                continue;
            }

            // fixed interval 无法 spill
            if (i->fixed) {
                continue;
            }

            spill_interval(c, a, i, first_from);
        }

        // split current at block_pos(block pos 必须强制溢出)
        int split_current_pos = interval_find_optimal_split_pos(c, a->current, block_pos[reg_id]);
        interval_t *child = interval_split_at(c, a->current, split_current_pos);
        sort_to_unhandled(a->unhandled, child);

        // assign register reg to interval current
        assign_interval(c, a, a->current, reg_id);
        return true;
    }

    return false;
}
