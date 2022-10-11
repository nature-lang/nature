#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir/lir.h"
#include "utils/list.h"

typedef enum {
    LOOP_DETECTION_FLAG_VISITED = 1,
    LOOP_DETECTION_FLAG_ACTIVE,
} loop_detection_flag;

typedef enum {
    USE_KIND_NULL = 0, // 默认值，不强制分配寄存器
    USE_KIND_SHOULD = 1, // 尽量分配寄存器，但不强制
    USE_KIND_MUST = 2, // 必须分配寄存器
} use_kind_e;

typedef struct {
    int value;
    use_kind_e kind;
} use_pos_t;

typedef struct {
    int from; // 包含
    int to; // 不包含
} interval_range_t;

// interval 分为两种，一种是虚拟寄存器，一种是固定寄存器
typedef struct interval_t {
    int index; // 对应的 var 对应的 interval 编号，可能是物理寄存器，也可能是虚拟寄存器产生的 index
    interval_range_t *first_range;
    interval_range_t *last_range;
    list *ranges;
    list *use_positions; // 存储 use_position 列表
    struct interval_t *parent;
    list *children; // 动态数组

    lir_operand_var *var; // var 中存储着 stack slot

    int16_t *stack_slot;
    // 当有多个空闲 register 时，优先分配 hint 对应的 register
    struct interval_t *reg_hint;
    uint8_t assigned; // 分配的 reg id, 通过 alloc_regs[assigned] 可以定位唯一寄存器

    bool fixed; // 是否是物理寄存器所产生的 interval, index 对应物理寄存器的编号，通常小于 40
} interval_t;

/**
 * 标记循环路线和每个基本块是否处于循环中，以及循环的深度
 * @param c
 */
void interval_loop_detection(closure_t *c);

/**
 * 根据块的权重（loop.depth） 对所有基本块进行排序插入到 closure_t->order_blocks 中
 * 权重越大越靠前
 * @param c
 */
void interval_block_order(closure_t *c);

/**
 * 为 operation 进行编号，采用偶数编号，编号时忽略 phi 指令
 * @param c
 */
void interval_mark_number(closure_t *c);

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
 * @param var
 * @param from
 * @param to
 */
void interval_add_range(closure_t *c, lir_operand_var *var, int from, int to);

/**
 * 上面的 add_range 在倒序 blocks 和 operation(op) 时，通常会将整个 block.first - block.last 添加到 range 中
 * 当遇到 def operation 是会缩减  range.from, 该方法则用于缩减
 * @param c
 * @param var
 * @param from
 */
void interval_cut_first_range_from(closure_t *c, lir_operand_var *var, int from);

/**
 * 添加 use_position 到 c->interval_table[var->ident].user_positions 中
 * @param c
 * @param var
 * @param position
 */
void interval_add_use_position(closure_t *c, lir_operand_var *var, int position, use_kind_e kind);

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
int interval_optimal_position(closure_t *c, interval_t *current, int before);

/**
 * interval 的 range 是否包含了 position
 * @param i
 * @param position
 * @return
 */
bool interval_is_covers(interval_t *i, int position);

/**
 * 寻找 current 和 select 的第一个相交点
 * position = current.first_from, 并且 position not cover by select
 * position++,直到 select 和 current 都 cover 了这个 position
 * 如果一直到 current.last_to 都没由找到相交点，则直接返回current.last_to 即可
 * @param current
 * @param select
 * @return
 */
int interval_next_intersection(interval_t *current, interval_t *select);

/**
 * 寻找 select interval 大于 after 的第一个 use_position
 * first_use_position != first_from
 * @param select
 * @param after_position
 * @return
 */
int interval_next_use_position(interval_t *i, int after_position);

/**
 * interval 的第一个使用位置
 * first_use_position != first_from
 * @param i
 * @return
 */
int interval_first_use_pos(interval_t *i);

void interval_spill_slot(closure_t *c, interval_t *i);

use_pos_t *interval_use_pos_of_kind(interval_t *i);

#endif
