#include "allocate.h"

static void handle_active(allocate *a) {
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

static void handle_inactive(allocate *a) {
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

static void set_pos(uint32_t list[UINT8_MAX], uint8_t index, uint32_t position) {
    if (position > list[index]) {
        return;
    }

    list[index] = position;
}

void allocate_walk(closure *c) {
    allocate *a = malloc(sizeof(allocate));
    a->unhandled = init_unhandled(c);
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
        bool allocated = allocate_free_reg(a);
        if (allocated) {
            list_push(a->active, a->current);
            continue;
        }

        allocated = allocate_block_reg(a);
        if (allocated) {
            list_push(a->active, a->current);
        }
    }
}

list *init_unhandled(closure *c) {
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

void to_unhandled(list *unhandled, interval_t *to) {
    // 从头部取出,最小的元素
    list_node *temp = unhandled->front;
    list_node *prev = NULL;
    while (temp->value != NULL && ((interval_t *) temp->value)->first_range->from < to->first_range->from) {
        prev = temp;
        temp = temp->succ;
    }

    // temp is rear
    if (temp->value == NULL) {
        list_node *empty = list_new_node();
        temp->value = to;
        temp->succ = empty;
        unhandled->rear = empty;
        unhandled->count++;
        return;
    }

    // temp->value->first_range->from > await_to->first_range->from > prev->value->first_range->from
    list_node *await_node = list_new_node();
    await_node->value = to;
    await_node->succ = temp;
    unhandled->count++;
    if (prev != NULL) {
        prev->succ = await_node;
    }
}

static uint8_t max_pos_index(const uint32_t list[UINT8_MAX]) {
    uint8_t max_index = 0;
    for (int i = 1; i < UINT8_MAX; ++i) {
        if (list[i] > list[max_index]) {
            max_index = i;
        }
    }

    return max_index;
}

bool allocate_free_reg(allocate *a) {
    uint32_t free_pos[UINT8_MAX] = {0};
    for (int i = 0; i < regs->count; ++i) {
        set_pos(free_pos, SLICE_TACK(reg_t, regs, i)->index, UINT32_MAX);
    }

    // active interval 不予分配，所以 pos 设置为 0
    list_node *curr = a->active->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        set_pos(free_pos, select->assigned->index, 0);
        curr = curr->succ;
    }

    curr = a->inactive->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        uint32_t position = interval_next_intersection(a->current, select);
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
    if (free_pos[max_reg_index] > a->current->last_range->to) {
        a->current->assigned = regs->take[max_reg_index];
        return true;
    }

    // 当前位置有处于空闲位置的寄存器可用，那就不需要 spill 任何区间
    uint32_t optimal_position = interval_optimal_position(a->current, free_pos[max_reg_index]);
    // 从最佳位置切割 interval, 切割后的 interval 并不是一定会溢出，而是可能会再次被分配到寄存器
    interval_split_interval(a->current, optimal_position);
    a->current->assigned = regs->take[max_reg_index];

    return true;
}

bool allocate_block_reg(allocate *a) {
    // 用于判断寄存器的空闲时间
    uint32_t use_pos[UINT8_MAX];
    // 被固定物理寄存器强制使用位置,有一些指令需要使用目标机器的固定寄存器，比如 ret eax 就需要强制使用 eax 寄存器
    uint32_t fixed_pos[UINT8_MAX];
    for (int i = 0; i < regs->count; ++i) {
        set_pos(use_pos, SLICE_TACK(reg_t, regs, i)->index, UINT32_MAX);
        set_pos(fixed_pos, SLICE_TACK(reg_t, regs, i)->index, UINT32_MAX);
    }
    uint32_t first_use_position = interval_first_use_position(a->current);

    // 遍历固定寄存器
    list_node *curr = a->active->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        // 是否为固定间隔
        if (select->fixed) {
            // 正在使用中的 fixed register,所有使用了该寄存器的 interval 都要让路
            set_pos(use_pos, select->assigned->index, 0);
            set_pos(fixed_pos, select->assigned->index, 0);
        } else {
            // 找一个大于 current first use_position 的位置(可以为0，0 表示没找到)
            uint32_t pos = interval_next_use_position(select, first_use_position);
            set_pos(use_pos, select->assigned->index, pos);
        }

        curr = curr->succ;
    }

    // 遍历非固定寄存器
    uint32_t pos;
    curr = a->inactive->front;
    while (curr->value != NULL) {
        interval_t *select = (interval_t *) curr->value;
        // 判断是否和当前 current 相交
        pos = interval_next_intersection(a->current, select);
        if (pos >= a->current->last_range->to) {
            continue;
        }

        if (select->fixed) {
            set_pos(fixed_pos, select->assigned->index, pos);
            set_pos(use_pos, select->assigned->index, pos);
        } else {
            pos = interval_next_use_position(select, first_use_position);
            set_pos(use_pos, select->assigned->index, pos);
        }

        curr = curr->succ;
    }

    uint8_t max_reg_id = max_pos_index(use_pos);
    // TODO split and spill

    return 0;
}

