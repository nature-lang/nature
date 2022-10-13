#include "allocate.h"

/**
 * 在 free 中找到一个尽量空闲的寄存器分配给 current, 优先考虑 register hint 分配的寄存器
 * 如果相应的寄存器不能使用到 current 结束
 * @param current
 * @param free_pos
 * @return
 */
static uint8_t find_free_reg(interval_t *current, uint8_t *free_pos) {
    uint8_t full_reg = 0; // 直接分配不用 split
    uint8_t part_reg = 0; // 需要 split current to unhandled
    uint8_t hint_reg = 0; // register hint 对应的 interval 分配的 reg
    if (current->reg_hint != NULL && current->reg_hint->assigned > 0) {
        hint_reg = current->reg_hint->assigned;
    }

    for (int alloc_id = 0; alloc_id < alloc_reg_count(); ++alloc_id) {
        if (free_pos[alloc_id] > current->last_range->to) {
            // 寄存器 alloc_id 足够空闲，可以直接分配给 current，不需要任何 spilt
            // 不过还是进行一下最优挑选, 要么是 hint_reg, 要么是空闲时间最长
            // 直接使用 hint reg
            if (full_reg != 0 && full_reg == hint_reg) {
                continue;
            }

            // 挑选空闲时间最长的寄存器
            if (free_pos[alloc_id] > free_pos[full_reg]) {
                full_reg = alloc_id;
            }
        } else if (free_pos[alloc_id] > current->first_range->from + 1) {
            // alloc_id 当前处于空闲状态，但是空闲时间不够长，需要进行 split current
            if (part_reg != 0 && part_reg == hint_reg) {
                continue;
            }
            if (free_pos[alloc_id] > free_pos[part_reg]) {
                part_reg = alloc_id;
            }
        }
    }

    if (full_reg > 0) {
        return full_reg;
    }

    if (part_reg > 0) {
        return part_reg;
    }

    return 0;
}

/**
 * 只要找到一个在 current 第一次使用时空闲的寄存器即可
 * @param current
 * @param free_pos
 * @return
 */
static uint8_t find_block_reg(interval_t *current, uint8_t *use_pos, uint8_t *block_pos) {
    uint8_t max_reg = 0; // 直接分配不用 split
    uint8_t hint_reg = 0; // register hint 对应的 interval 分配的 reg
    if (current->reg_hint != NULL && current->reg_hint->assigned > 0) {
        hint_reg = current->reg_hint->assigned;
    }

    for (int alloc_id = 0; alloc_id < alloc_reg_count(); ++alloc_id) {
        if (use_pos[alloc_id] <= current->first_range->from + 1) {
            continue;
        }

        // 优先选择 hint reg
        if (max_reg != 0 && max_reg == hint_reg) {
            continue;
        }

        if (use_pos[alloc_id] > use_pos[max_reg]) {
            max_reg = alloc_id;
        }
    }

    return max_reg;
}


static void handle_active(allocate_t *a) {
    int position = a->current->first_range->from;
    list_node *active_curr = a->active->front;
    list_node *active_prev = NULL;
    while (active_curr->value != NULL) {
        interval_t *select = (interval_t *) active_curr->value;
        bool is_expired = select->last_range->to < position;
        bool is_covers = interval_is_covers(select, position);

        if (!is_covers || is_expired) {
            if (active_prev == NULL) {
                a->active->front = active_curr->succ;
            } else {
                active_prev->succ = active_curr->succ;
            }
            a->active->count--;
            if (is_expired) {
                list_push(a->handled, select);
            } else {
                list_push(a->inactive, select);
            }
        }

        active_prev = active_curr;
        active_curr = active_curr->succ;
    }
}

static void handle_inactive(allocate_t *a) {
    int position = a->current->first_range->from;
    list_node *inactive_curr = a->inactive->front;
    list_node *inactive_prev = NULL;
    while (inactive_curr->value != NULL) {
        interval_t *select = (interval_t *) inactive_curr->value;
        bool is_expired = select->last_range->to < position;
        bool is_covers = interval_is_covers(select, position);

        if (is_covers || is_expired) {
            if (inactive_prev == NULL) {
                a->inactive->front = inactive_curr->succ;
            } else {
                inactive_prev->succ = inactive_curr->succ;
            }
            a->inactive->count--;
            if (is_expired) {
                list_push(a->handled, select);
            } else {
                list_push(a->active, select);
            }
        }

        inactive_prev = inactive_curr;
        inactive_curr = inactive_curr->succ;
    }
}

static void set_pos(int list[UINT8_MAX], uint8_t index, int position) {
    if (position > list[index]) {
        return;
    }

    list[index] = position;
}

void allocate_walk(closure_t *c) {
    allocate_t *a = malloc(sizeof(allocate_t));
    a->unhandled = unhandled_new(c);
    a->handled = list_new();
    a->active = list_new();
    a->inactive = list_new();

    while (a->unhandled->count != 0) {
        a->current = (interval_t *) list_pop(a->unhandled);
        // handle active
        handle_active(a);
        // handle inactive
        handle_inactive(a);

        // 尝试为 current 分配寄存器
        bool allocated = allocate_free_reg(c, a);
        if (allocated) {
            list_push(a->active, a->current);
            continue;
        }

        allocated = allocate_block_reg(c, a);
        if (allocated) {
            list_push(a->active, a->current);
            continue;
        }

        // 分不到寄存器，只能 spill 了， spill 的 interval 放到 handled 中，再也不会被 traverse 了
        list_push(a->handled, a->current);
    }
}

list *unhandled_new(closure_t *c) {
    list *unhandled = list_new();
    // 遍历所有变量,根据 interval from 进行排序
    for (int i = 0; i < c->globals->count; ++i) {
        interval_t *item = table_get(c->interval_table, ((lir_operand_var *) c->globals->take[i])->ident);
        if (item == NULL) {
            continue;
        }
        to_unhandled(unhandled, item);
    }
    // 遍历所有固定寄存器生成 fixed_interval
    for (int i = 0; i < alloc_reg_count(); ++i) {
        reg_t *reg = alloc_regs[i];
        interval_t *item = table_get(c->interval_table, reg->name);
        if (item == NULL) {
            continue;
        }
        to_unhandled(unhandled, item);
    }

    return unhandled;
}

/**
 * 将 to 根据 LIST_VALUE 的 from 字段排序，值越小越靠前
 * @param unhandled
 * @param to
 */
void to_unhandled(list *unhandled, interval_t *to) {
    LIST_FOR(unhandled) {
        interval_t *i = LIST_VALUE();
        // 当 i->from 大于 to->from 时，将 to 插入到 i 前面
        if (i->first_range->from > to->first_range->from) {
            list_insert_before(unhandled, LIST_NODE(), to);
            return;
        }
    }
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
    int free_pos[UINT8_MAX] = {0};
    for (int i = 0; i < alloc_reg_count(); ++i) {
        reg_t *reg = alloc_regs[i];
        if (a->current->alloc_type != reg->type) { // already set 0
            continue;
        }
        set_pos(free_pos, i, UINT32_MAX);
    }

    // active(已经分配到了 reg) interval 不予分配，所以 pos 设置为 0
    LIST_FOR(a->active) {
        interval_t *select = LIST_VALUE();
        set_pos(free_pos, select->assigned, 0);
    }


    LIST_FOR(a->inactive) {
        interval_t *select = LIST_VALUE();
        int pos = interval_next_intersection(a->current, select);
        // potions 表示两个 interval 重合，重合点之前都是可以自由分配的区域
        set_pos(free_pos, select->assigned, pos);
    }

    // 找到权重最大的寄存器寄存器进行分配
    uint8_t alloc_id = find_free_reg(a->current, free_pos);
    // 没有可用的寄存器用于分配
    if (alloc_id == 0) {
        return false;
    }

    a->current->assigned = alloc_id;
    if (free_pos[alloc_id] < a->current->last_range->to) {
        int optimal_position = interval_find_optimal_split_pos(c, a->current, free_pos[alloc_id]);

        // 从最佳位置切割 interval, 切割后的 interval 并不是一定会溢出，而是可能会再次被分配到寄存器(加入到 unhandled 中)
        interval_t *child = interval_split_at(c, a->current, optimal_position);
        to_unhandled(a->unhandled, child);

        // 为当前 interval 分配寄存器
        a->current->assigned = alloc_id;
    }

    return true;
}

bool allocate_block_reg(closure_t *c, allocate_t *a) {
    // 用于判断寄存器的空闲时间
    // key is physical register index, value is position
    int use_pos[UINT8_MAX] = {0}; // use_pos 一定小于等于 block_pos
    int block_pos[UINT8_MAX] = {0}; // 设置 block pos 的同时需要隐式设置 use pos
    for (int alloc_id = 0; alloc_id < alloc_reg_count(); ++alloc_id) {
        reg_t *reg = alloc_regs[alloc_id];
        if (a->current->alloc_type != reg->type) {
            continue;
        }
        SET_BLOCK_POS(alloc_id, UINT32_MAX);
    }

    int first_use = a->current->first_range->from;

    // 遍历固定寄存器(active)
    LIST_FOR(a->active) {
        interval_t *select = LIST_VALUE();
        // 固定间隔本身就是 short range 了，但如果还在 current pos is active,so will set that block and use to 0
        if (select->fixed) {
            // 正在使用中的 fixed register,所有使用了该寄存器的 interval 都要让路
            SET_BLOCK_POS(select->assigned, 0);
        } else {
            // 找一个大于 current first use_position 的位置(可以为0，0 表示没找到)
            // 查找 active interval 的下一个使用位置,用来判断其空闲时长
            int pos = interval_next_use_position(select, first_use);
            SET_USE_POS(select->assigned, pos);
        }
    }

    // 遍历非固定寄存器(active intersect current)
    // 如果 lifetime hole 没有和 current intersect 在 allocate free 的时候已经用完了
    int pos;
    LIST_FOR(a->inactive) {
        interval_t *select = LIST_VALUE();
        pos = interval_next_intersection(a->current, select);
        if (pos >= a->current->last_range->to) {
            continue;
        }

        if (select->fixed) {
            // 该 interval 虽然是固定 interval(short range), 但是当前正处于 hole 中
            // 所以在 pos 之前的位置都是可以使用该 assigned 对应都寄存器
            // 但是一旦到了 pos 位置，current 必须 split at and spill child
            SET_BLOCK_POS(select->assigned, pos);
        } else {
            pos = interval_next_use_position(select, first_use);
            SET_USE_POS(select->assigned, pos);
        }
    }


    // max use pos 表示空闲时间最长的寄存器
    uint8_t alloc_id = find_block_reg(a->current, use_pos, block_pos);

    if (use_pos[alloc_id] < first_use) {
        //  active/inactive interval 的下一个 pos 都早于 current first use pos, 所以最好直接 spill 整个 current
        // assign spill slot to current
        interval_spill_stack(NULL, a->current);

        // 一旦 current spill 到了内存中，则后续就再也不会处理了
        // 所以 current 已经是 spill 了，但如果 current 存在某个 use pos 必须使用分配寄存器,
        // 则需要将 current 重新分配到寄存器，这称为 reload, 在需要 reload 之前，将 interval spilt
        use_pos_t *must_pos = interval_must_reg_pos(a->current);
        if (must_pos) {
            int split_pos = interval_find_optimal_split_pos(c, a->current, must_pos->value);
            interval_t *child = interval_split_at(c, a->current, split_pos);
            to_unhandled(a->unhandled, child);
        }
    } else if (block_pos[alloc_id] > a->current->last_range->to) {
        // 一般都会进入到这一条件中
        // alloc_id 对应的寄存器的空闲时间 大于 current.first_use
        // 甚至大于 current->last_range->to，所以优先溢出 alloc_id 对应都 interval
        // 所有和 current intersecting 的 interval 都需要在 current start 之前 split 并且 spill 到内存中
        // 当然，如果 child interval 存在 use pos 必须要加载 reg, 则需要二次 spilt into unhandled
        LIST_FOR(a->active) {
            interval_t *i = LIST_VALUE();
            if (i->assigned != alloc_id) {
                continue;
            }
            // first_use 表示必须在 first_use 之前 spill, 否则会影响 current 使用 alloc_id
            spill_interval(c, a, i, first_use);
        }
        LIST_FOR(a->inactive) {
            interval_t *i = LIST_VALUE();
            if (i->assigned != alloc_id) {
                continue;
            }
            spill_interval(c, a, i, first_use);
        }

        // assign register reg to interval current
        a->current->assigned = alloc_id;
        return true;
    } else {
        // 1. current.first_use < use_pos < current.last_use
        // 2. block_pos < current.last_use
        // 虽然 first use pos < reg_index 对应的 interval 的使用位置，但是
        // current start ~ end 之间被 block_pos 所截断，所以必须 split  current in block pos 之前, child in to unhandled list
        // split and spill interval active/inactive intervals for reg
        LIST_FOR(a->active) {
            interval_t *i = LIST_VALUE();
            if (i->assigned != alloc_id) {
                continue;
            }
            spill_interval(c, a, i, first_use);
        }
        LIST_FOR(a->inactive) {
            interval_t *i = LIST_VALUE();
            if (i->assigned != alloc_id) {
                continue;
            }
            spill_interval(c, a, i, first_use);
        }

        // assign register reg to interval current
        a->current->assigned = alloc_id;

        // split current at block_pos
        int split_current_pos = interval_find_optimal_split_pos(c, a->current, block_pos[alloc_id]);
        interval_t *child = interval_split_at(c, a->current, split_current_pos);
        to_unhandled(a->unhandled, child);
        return true;
    }

    return false;
}

/**
 * 在 before_pos 之前需要切割出一个合适的位置，并 spilt spill 到内存中
 * 由于 spill 的部分后续不会在 handle，所以如果包含 use_position kind 则需要再次 spilt 丢出去
 * @param c
 * @param i
 * @param before_pos
 * @return
 */
void spill_interval(closure_t *c, allocate_t *a, interval_t *i, int before_pos) {
    // spill current before current first use position
    int split_pos = interval_find_optimal_split_pos(c, i, before_pos);
    interval_t *child = interval_split_at(c, i, split_pos);
    use_pos_t *must_pos = interval_must_reg_pos(child);
    if (must_pos) {
        split_pos = interval_find_optimal_split_pos(c, child, must_pos->value);
        interval_t *unhandled = interval_split_at(c, child, split_pos);
        list_push(a->unhandled, unhandled);
    }

    // child to slot
    interval_spill_stack(c, child);
}

