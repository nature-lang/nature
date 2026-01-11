#ifndef NATURE_SRC_PEEPHOLE_PEEPHOLE_H_
#define NATURE_SRC_PEEPHOLE_PEEPHOLE_H_

#include "src/lir.h"

// 公共窥孔优化函数

/**
 * 检查变量是否在 block 的 live_out 中
 */
static inline bool peephole_is_in_live_out(basic_block_t *block, lir_var_t *var) {
    if (!block || !block->live_out || !var) {
        return false;
    }
    for (int i = 0; i < block->live_out->count; i++) {
        lir_var_t *live_var = block->live_out->take[i];
        if (live_var && str_equal(live_var->ident, var->ident)) {
            return true;
        }
    }
    return false;
}

/**
 * 构建 block 的变量使用计数表
 * 仅统计 USE 标志的变量出现次数
 */
static inline void peephole_build_use_count(basic_block_t *block) {
    if (block->use_count) {
        table_free(block->use_count);
    }

    block->use_count = table_new();

    linked_node *current = linked_first(block->operations);
    while (current != NULL && current->value != NULL) {
        lir_op_t *op = current->value;

        // 提取该指令中所有 use 的变量
        slice_t *use_operands = extract_op_operands(op, FLAG(LIR_OPERAND_VAR), FLAG(LIR_FLAG_USE), true);
        for (int i = 0; i < use_operands->count; ++i) {
            lir_var_t *use_var = use_operands->take[i];
            intptr_t count = (intptr_t) table_get(block->use_count, use_var->ident);
            table_set(block->use_count, use_var->ident, (void *) (count + 1));
        }
        slice_free(use_operands); // 释放临时 slice

        current = current->succ;
    }
}

/**
 * 重置 block 的 use_count 表（优化后需要重新构建）
 */
static inline void peephole_free_use_count(basic_block_t *block) {
    if (block->use_count != NULL) {
        table_free(block->use_count);
        block->use_count = NULL;
    }
}

/**
 * 检查变量是否可以被安全删除（定义该变量的指令可以被删除）
 * 
 * 满足以下条件时返回 true：
 * 1. 变量在 block 中使用次数 <= 1
 * 2. 变量不在 block.live_out 中（即 block 结束后不需要该变量）
 * 
 * @param block 当前基本块
 * @param var 需要检查的变量
 * @return 如果变量可以安全删除返回 true
 */
static inline bool peephole_can_remove_var(basic_block_t *block, lir_var_t *var) {
    if (!block || !var) {
        return false;
    }

    // 首先检查变量是否在 live_out 中
    // 如果在 live_out 中，说明离开 block 后还会被使用，不能删除
    if (peephole_is_in_live_out(block, var)) {
        return false;
    }

    // use_count 在 block 优化开始时已构建，直接使用
    // 从 hash table 查询使用次数 - O(1)
    intptr_t use_count = (intptr_t) table_get(block->use_count, var->ident);

    // 变量使用次数 <= 1 时可以删除
    return use_count <= 1;
}

#endif //NATURE_SRC_PEEPHOLE_PEEPHOLE_H_
