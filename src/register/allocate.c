#include "allocate.h"

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

void allocate_walk(closure *c) {
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
        }
    }
}

list *unhandled_new(closure *c) {
    list *unhandled = list_new();
    // 遍历所有变量
    for (int i = 0; i < c->globals.count; ++i) {
        void *raw = table_get(c->interval_table, c->globals.list[i]->ident);
        if (raw == NULL) {
            continue;
        }
        interval_t *item = (interval_t *) raw;
        to_unhandled(unhandled, item);
    }
    // 遍历所有固定寄存器
    for (int i = 0; i < c->fixed_regs->count; ++i) {
        void *raw = table_get(c->interval_table, SLICE_TACK(reg_t, c->fixed_regs, i)->name);
        if (raw == NULL) {
            continue;
        }
        interval_t *item = (interval_t *) raw;
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

bool allocate_free_reg(closure *c, allocate_t *allocate) {
    int free_pos[UINT8_MAX] = {0};
    for (int i = 0; i < regs->count; ++i) {
        set_pos(free_pos, SLICE_TACK(reg_t, regs, i)->index, UINT32_MAX);
    }

    // active interval 不予分配，所以 pos 设置为 0
    list_node *curr = allocate->active->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        set_pos(free_pos, select->assigned->index, 0);
        curr = curr->succ;
    }

    curr = allocate->inactive->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        int position = interval_next_intersection(allocate->current, select);
        // potions 表示两个 interval 重合，重合点之前都是可以自由分配的区域
        set_pos(free_pos, select->assigned->index, position);

        curr = curr->succ;
    }

    // 找到权重最大的寄存器寄存器进行分配
    uint8_t max_reg_index = max_pos_index(free_pos);
    // 没有可用的寄存器用于分配
    if (free_pos[max_reg_index] == 0) {
        return false;
    }

    // 寄存器有足够的 free 空间供当前寄存器使用，可以直接分配
    // 一旦 assigned， 就表示整个 var 对应的 interval 都将获得寄存器
    // 如果后续需要 spill, 则会创建一个新的 temp var + 对应的 interval
    if (free_pos[max_reg_index] > allocate->current->last_range->to) {
        allocate->current->assigned = regs->take[max_reg_index];
        return true;
    }

    // 当前位置有处于空闲位置的寄存器可用，那就不需要 spill 任何区间
    int optimal_position = interval_optimal_position(allocate->current, free_pos[max_reg_index]);

    // 从最佳位置切割 interval, 切割后的 interval 并不是一定会溢出，而是可能会再次被分配到寄存器(加入到 unhandled 中)
    interval_t *i = interval_split_at(c, allocate->current, optimal_position);
    to_unhandled(allocate->unhandled, i);

    // 为当前 interval 分配寄存器
    allocate->current->assigned = regs->take[max_reg_index];

    return true;
}

bool allocate_block_reg(closure *c, allocate_t *a) {
    // 用于判断寄存器的空闲时间
    int use_pos[UINT8_MAX];
    int block_pos[UINT8_MAX];
    for (int i = 0; i < regs->count; ++i) {
        // INT32_MAX 表示寄存器完全空闲
        set_pos(use_pos, SLICE_TACK(reg_t, regs, i)->index, INT32_MAX);
        set_pos(block_pos, SLICE_TACK(reg_t, regs, i)->index, INT32_MAX);
    }
    int first_use_position = interval_first_use_position(a->current);

    // 遍历固定寄存器
    LIST_FOR(a->active) {
        interval_t *select = LIST_VALUE();
        // 是否为固定间隔
        if (select->fixed) {
            // 正在使用中的 fixed register,所有使用了该寄存器的 interval 都要让路
            set_pos(use_pos, select->assigned->index, 0);
            set_pos(block_pos, select->assigned->index, 0);
        } else {
            // 找一个大于 current first use_position 的位置(可以为0，0 表示没找到)
            int pos = interval_next_use_position(select, first_use_position);
            set_pos(use_pos, select->assigned->index, pos);
        }
    }

    // 遍历非固定寄存器
    int pos;
    LIST_FOR(a->inactive) {
        interval_t *select = LIST_VALUE();
        // 判断是否和当前 current 相交
        pos = interval_next_intersection(a->current, select);
        if (pos >= a->current->last_range->to) {
            continue;
        }

        if (select->fixed) {
            set_pos(block_pos, select->assigned->index, pos);
            set_pos(use_pos, select->assigned->index, pos);
        } else {
            pos = interval_next_use_position(select, first_use_position);
            set_pos(use_pos, select->assigned->index, pos);
        }
    }

    uint8_t max_reg = max_pos_index(use_pos);
    if (use_pos[max_reg] < first_use_position) {
        //  active/inactive interval 的下一个 pos 都早于 current first use pos, 所以最好直接 spill 整个 current
        // assign spill slot to current
        interval_spill_slot(a->current);
        // current 已经是 spill 了，但如果 current 存在某个 use pos 必须使用分配寄存器,
        // 则需要将 current 重新分配到寄存器，这称为 reload
        use_position_t *kind_pos = interval_use_pos_of_kind(a->current);
        if (kind_pos > 0) {
            int split_pos = interval_optimal_position(c, a->current, kind_pos->value);
            interval_t *child = interval_split_at(c, a->current, split_pos);
            to_unhandled(a->unhandled, child);
        }
    } else if (block_pos[max_reg] > a->current->last_range->to) {
        // TODO ??
    }

    return 0;
}

