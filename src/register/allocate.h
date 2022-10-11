#ifndef NATURE_SRC_REGISTER_ALLOCATE_H_
#define NATURE_SRC_REGISTER_ALLOCATE_H_

#include "utils/slice.h"
#include "utils/list.h"
#include "interval.h"

#define SET_USE_POS(_index, _pos) \
    set_pos(use_pos, _index, _pos)

#define SET_BLOCK_POS(_index, _pos) \
    set_pos(block_pos, _index, _pos); \
    set_pos(use_pos, _index, _pos);

typedef struct {
    list *unhandled;
    list *handled;
    list *active;
    list *inactive;
    interval_t *current; // 正在分配的寄存器
} allocate_t;

/**
 * 主分配方法
 */
void allocate_walk(closure_t *c);

bool allocate_free_reg(closure_t *c, allocate_t *a);

/**
 * 当没有物理寄存器用于分配时，则需要使用该方法，选择最长时间空闲的 interval 进行溢出
 * @param a
 * @return
 */
bool allocate_block_reg(closure_t *c, allocate_t *a);

/**
 * 将 interval 按照 first_range.from 有序插入到 unhandled 中
 * @param to
 * @param unhandled
 */
void to_unhandled(list *unhandled, interval_t *to);

/**
 * 根据 interval 列表 初始化 unhandled 列表
 * 采用链表结构是因为跟方便排序插入，有序遍历
 * @return
 */
list *unhandled_new(closure_t *c);

void spill_interval(closure_t *c, allocate_t *a, interval_t *i, int before_pos);

#endif //NATURE_SRC_REGISTER_ALLOCATE_H_
