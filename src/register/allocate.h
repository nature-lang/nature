#ifndef NATURE_SRC_REGISTER_ALLOCATE_H_
#define NATURE_SRC_REGISTER_ALLOCATE_H_

#include "src/lib/slice.h"
#include "src/lib/list.h"
#include "interval.h"

typedef struct {
  list *unhandled;
  list *handled;
  list *active;
  list *inactive;
  interval *current; // 正在分配的东西
} allocate;

/**
 * 主分配方法
 */
void allocate_walk(closure *c);
bool allocate_free_reg(allocate *a);
bool allocate_block_reg(allocate *a);

/**
 * 将 interval 按照 first_range.from 有序插入到 unhandled 中
 * @param to
 * @param unhandled
 */
void to_unhandled(list *unhandled, interval *to);

/**
 * 根据 interval 列表 初始化 unhandled 列表
 * 采用链表结构是因为跟方便排序插入，有序遍历
 * @return
 */
list *init_unhandled(closure *c);

#endif //NATURE_SRC_REGISTER_ALLOCATE_H_
