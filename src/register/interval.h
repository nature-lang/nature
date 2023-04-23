#ifndef NATURE_SRC_REGISTER_INTERVAL_H_
#define NATURE_SRC_REGISTER_INTERVAL_H_

#include "src/lir.h"
#include "utils/linked.h"

// 如果仅仅使用
#define ALLOC_USE_MIN 4

typedef enum {
    USE_KIND_NULL = 0, // 默认值，不强制分配寄存器
    USE_KIND_NOT = 1, // 不能分配寄存器, 例如 LEA 的左值
    USE_KIND_MUST = 2, // 必须分配寄存器
    USE_KIND_SHOULD = 3, // 尽量分配寄存器，但不强制
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
    linked_t *ranges;
    linked_t *use_pos_list; // 存储 use_position 列表
    struct interval_t *parent;
    linked_t *children; // 动态数组

    lir_var_t *var; // var 中存储着 stack slot

    int64_t *stack_slot; // slot 对栈帧顶部的偏移(0), 值向上增长，比如 stack_slot = -8, size = 8，表示值存储在 (top-8) ~ top
    bool spilled; // 当前 interval 是否是溢出状态,去 stack_slot 中找对应的插槽
    // 当有多个空闲 register 时，优先分配 hint 对应的 register
    struct interval_t *reg_hint;
    slice_t *phi_hints; // phi def interval 对应的多个 body interval,def interval 优先分配 body var 已经分配的寄存器
    uint8_t assigned; // 分配的 reg id, 通过 alloc_regs[assigned] 可以定位唯一寄存器

    vr_flag_t alloc_type; //    VR_FLAG_ALLOC_INT,VR_FLAG_ALLOC_FLOAT
    bool fixed; // 是否是物理寄存器所产生的 interval, index 对应物理寄存器的编号，通常小于 40
} interval_t;


typedef struct {
    slice_t *from_list;
    slice_t *to_list;
    basic_block_t *insert_block;
    int insert_id;
} resolver_t;

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
void interval_add_use_pos(closure_t *c, interval_t *i, int position, use_kind_e kind);

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

bool range_covered(interval_range_t *r, int position, bool is_input);


bool interval_expired(interval_t *i, int position, bool is_input);

/**
 * interval 的 range 是否包含了 position
 * @param i
 * @param position
 * @return
 */
bool interval_covered(interval_t *i, int position, bool is_input);

/**
 * interval 的 range 是否包含了 position
 * @param i
 * @param position
 * @return
 */
bool interval_covered(interval_t *i, int position, bool is_input);

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

void interval_spill_slot(closure_t *c, interval_t *i);

interval_t *interval_child_at(interval_t *i, int op_id, bool is_use);

use_pos_t *interval_must_reg_pos(interval_t *i);

use_pos_t *interval_must_stack_pos(interval_t *i);

void resolve_data_flow(closure_t *c);

void resolve_find_insert_pos(resolver_t *r, basic_block_t *from, basic_block_t *to);

void resolve_mappings(closure_t *c, resolver_t *r);

use_pos_t *first_use_pos(interval_t *i, use_kind_e kind);

void var_replace(lir_operand_t *operand, interval_t *i);

#endif
