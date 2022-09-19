#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir/lir.h"
#include "utils/list.h"

typedef enum {
    LOOP_DETECTION_FLAG_VISITED = 1,
    LOOP_DETECTION_FLAG_ACTIVE,
} loop_detection_flag;

typedef struct {
    int from;
    int to;
} interval_range_t;

// interval 分为两种，一种是虚拟寄存器，一种是固定寄存器
typedef struct interval {
    int first_from;
    int last_to;
    list *ranges;
    list *use_positions; // 存储 use_position 列表
    struct interval_t *split_parent;
    list *split_children; // 动态数组

    lir_operand_var *var; // 变量名称
    reg_t *reg;
    reg_t *assigned;

    bool fixed; // 是否是固定寄存器,固定寄存器有 reg 没有 var
} interval_t;

/**
 * 标记循环路线和每个基本块是否处于循环中，以及循环的深度
 * @param c
 */
void interval_loop_detection(closure *c);

/**
 * 根据块的权重（loop.depth） 对所有基本块进行排序插入到 closure->order_blocks 中
 * 权重越大越靠前
 * @param c
 */
void interval_block_order(closure *c);

/**
 * 为 operation 进行编号，采用偶数编号，编号时忽略 phi 指令
 * @param c
 */
void interval_mark_number(closure *c);

/**
 * 倒序遍历排好序，编好号的 block 和 operation 构造 interval 并添加到 closure->interval_table 中
 * @param c
 */
void interval_build(closure *c);

/**
 * 实例化 interval，包含内存申请，值初始化
 * @param var
 * @return
 */
interval_t *interval_new(lir_operand_var *var);

/**
 * 添加 range 到 interval.ranges 中
 * @param c
 * @param var
 * @param from
 * @param to
 */
void interval_add_range(closure *c, lir_operand_var *var, int from, int to);

/**
 * 上面的 add_range 在倒序 blocks 和 operation(op) 时，通常会将整个 block.first - block.last 添加到 range 中
 * 当遇到 def operation 是会缩减  range.from, 该方法则用于缩减
 * @param c
 * @param var
 * @param from
 */
void interval_cut_first_range_from(closure *c, lir_operand_var *var, int from);

/**
 * 添加 use_position 到 c->interval_table[var->ident].user_positions 中
 * @param c
 * @param var
 * @param position
 */
void interval_add_use_position(closure *c, lir_operand_var *var, int position);

/**
 * 在 position 位置，将 interval 切割成两份
 * [first_from, position) 为 split_parent
 * [position, last_to) 为 split_children
 * @param current
 * @param position
 */
void interval_split_interval(interval_t *current, uint32_t position);

/**
 * current 需要在 before 之前被 split,需要找到一个最佳的位置
 * 比如不能在循环体中 split
 * @param current
 * @param before
 * @return
 */
uint32_t interval_optimal_position(interval_t *current, uint32_t before);

/**
 * interval 的 range 是否包含了 position
 * @param i
 * @param position
 * @return
 */
bool interval_is_covers(interval_t *i, uint32_t position);

/**
 * 寻找 current 和 select 的第一个相交点
 * position = current.first_from, 并且 position not cover by select
 * position++,直到 select 和 current 都 cover 了这个 position
 * 如果一直到 current.last_to 都没由找到相交点，则直接返回current.last_to 即可
 * @param current
 * @param select
 * @return
 */
uint32_t interval_next_intersection(interval_t *current, interval_t *select);

/**
 * 寻找 select interval 大于 after 的第一个 use_position
 * first_use_position != first_from
 * @param select
 * @param after
 * @return
 */
uint32_t interval_next_use_position(interval_t *select, uint32_t after);

/**
 * interval 的第一个使用位置
 * first_use_position != first_from
 * @param i
 * @return
 */
uint32_t interval_first_use_position(interval_t *i);

#endif
