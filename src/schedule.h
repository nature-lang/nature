#ifndef NATURE_SRC_SCHEDULE_H_
#define NATURE_SRC_SCHEDULE_H_

#include "src/lir.h"
#include "src/types.h"
#include "utils/linked.h"
#include "utils/slice.h"

/**
 * 指令评分 - 参考 Go 编译器设计
 * 分数越低的指令越先调度
 */
typedef enum {
    SCORE_LABEL = 0, // LABEL 指令
    SCORE_PHI,
    SCORE_FN_BEGIN, // 函数开始
    SCORE_SAFEPOINT,
    SCORE_ARG, // 参数相关指令 (NOP def)
    SCORE_NIL_CHECK, // 空指针检查
    SCORE_MEMORY_READ, // 内存读取操作
    SCORE_MEMORY_WRITE, // 内存写入操作
    SCORE_DEFAULT, // 普通指令 (算术、逻辑等)
    SCORE_FLAGS, // 设置标志位的指令 (cmp/test)
    SCORE_CONTROL, // 控制流指令 (分支、跳转)
    SCORE_FN_END, // 函数结束
} schedule_score_t;

/**
 * 调度节点 - 表示一条待调度的指令
 */
typedef struct {
    lir_op_t *op; // 指向的 LIR 指令
    linked_node *node; // 在 operations 链表中的节点
    schedule_score_t score; // 指令评分
    int dep_count; // 未满足的前驱依赖数量
    slice_t *successors; // 后继节点列表 (依赖当前指令的指令)
    bool scheduled; // 是否已被调度
    int original_id; // 原始顺序编号
} schedule_node_t;

/**
 * 调度上下文 - 用于单个基本块的调度
 */
typedef struct {
    basic_block_t *block; // 当前基本块
    slice_t *nodes; // 所有调度节点
    slice_t *ready; // 就绪队列 (依赖已满足的节点)
    slice_t *result; // 调度结果
    table_t *var_def_table; // 变量定义位置表 (var_ident -> node)
    table_t *last_mem_op; // 最后的内存操作节点
    table_t *reg_def_table; // 寄存器定义位置表 (reg_index -> node)
    table_t *reg_use_table; // 寄存器最后使用位置表 (reg_index -> node)
} schedule_ctx_t;

/**
 * 对闭包中的所有基本块进行指令调度
 * @param c 闭包
 */
void schedule(closure_t *c);

/**
 * 计算指令的评分
 * @param op LIR 指令
 * @return 评分
 */
schedule_score_t schedule_compute_score(lir_op_t *op);

/**
 * 判断指令是否是内存读取操作
 * @param op LIR 指令
 * @return 是否为内存读
 */
bool schedule_is_mem_read(lir_op_t *op);

/**
 * 判断指令是否是内存写入操作
 * @param op LIR 指令
 * @return 是否为内存写
 */
bool schedule_is_mem_write(lir_op_t *op);

#endif // NATURE_SRC_SCHEDULE_H_
