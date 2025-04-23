#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir.h"
#include "utils/linked.h"

// 如果仅仅使用
#define ALLOC_USE_MIN 8

/**
 * 根据块的权重（loop.depth） 对所有基本块进行排序插入到 closure_t->order_blocks 中
 * 权重越大越靠前
 * @param c
 */
void interval_block_order(closure_t *c);

/**
 * 倒序遍历排好序，编好号的 block 和 operation 构造 interval 并添加到 closure_t->interval_table 中
 * @param c
 */
void interval_build(closure_t *c);

/**
 * 实例化 interval，包含内存申请，值初始化
 * @param var
 * @return
 */
interval_t *interval_new(closure_t *c);

/**
 * 添加 range 到 interval.ranges 头中,并调整 first_range 的指向
 * 由于使用了后续遍历操作数，所以并不需要考虑单个 interval range 重叠等问题
 * @param c
 * @param i
 * @param from
 * @param to
 */
void interval_add_range(closure_t *c, interval_t *i, int from, int to);

/**
 * 添加 use_position 到 c->interval_table[var->ident].user_positions 中
 * @param c
 * @param i
 * @param position
 */
void interval_add_use_pos(closure_t *c, interval_t *i, int position, alloc_kind_e kind);

/**
 * @param i
 * @param position
 */
interval_t *interval_split_at(closure_t *c, interval_t *i, int position);

/**
 * current 需要在 before 之前被 split,需要找到一个最佳的位置
 * 比如不能在循环体中 split
 * @param current
 * @param before
 * @return
 */
int interval_find_optimal_split_pos(closure_t *c, interval_t *current, int before);

bool interval_expired(interval_t *i, int position, bool is_input);

/**
 * interval 的 range 是否包含了 position
 * @param i
 * @param adjust_position
 * @return
 */
bool interval_covered(interval_t *i, int position, bool is_input);

/**
 * 寻找 current 和 select 的第一个相交点
 * position = current.first_from, 并且 position not cover by select
 * position++,直到 select 和 current 都 cover 了这个 position
 * @param current
 * @param select
 * @return
 */
int interval_next_intersect(closure_t *c, interval_t *current, interval_t *select);
int old_interval_next_intersect(closure_t *c, interval_t *current, interval_t *select);

bool interval_is_intersect(interval_t *current, interval_t *select);

/**
 * 寻找 select interval 大于 after 的第一个 use_position
 * first_use_position != first_from
 * @param select
 * @param after_position
 * @return
 */
int interval_next_use_position(interval_t *i, int after_position);

void interval_spill_slot(closure_t *c, interval_t *i);

use_pos_t *interval_must_reg_pos(interval_t *i);

use_pos_t *interval_must_stack_pos(interval_t *i);

void resolve_data_flow(closure_t *c);

void resolve_find_insert_pos(resolver_t *r, basic_block_t *from, basic_block_t *to);

void resolve_mappings(closure_t *c, resolver_t *r);

use_pos_t *first_use_pos(interval_t *i, alloc_kind_e kind);

void var_replace(lir_operand_t *operand, interval_t *i);

/**
 * 虚拟寄存器替换成 stack slot 和 physical register
 * @param c
 */
void replace_virtual_register(closure_t *c);

#endif
