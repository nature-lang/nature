#include "schedule.h"

#include "register/register.h"
#include <assert.h>
#include <string.h>

/**
 * 根据寄存器的 alloc_id 生成唯一的 key
 */
static char *reg_key(uint8_t alloc_id) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "_reg_%d", alloc_id);
    return buf;
}

/**
 * 根据小寄存器找到需要进行寄存器分配的寄存器
 */
static reg_t *reg_alloc_find(reg_t *reg) {
    lir_flag_t alloc_type = LIR_FLAG_ALLOC_INT;
    int size = QWORD;
    if (reg->flag & FLAG(LIR_FLAG_ALLOC_FLOAT)) {
        alloc_type = LIR_FLAG_ALLOC_FLOAT;
        if (BUILD_ARCH != ARCH_RISCV64) {
            size = OWORD;
        }
    }
    return reg_find(alloc_type, reg->index, size);
}

/**
 * 判断指令是否是内存读取操作
 */
bool schedule_is_mem_read(lir_op_t *op) {
    // 从 indirect_addr 读取数据
    if (op->first && op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        return true;
    }
    if (op->second && op->second->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        return true;
    }
    // CALL 和 RT_CALL 可能读取内存
    if (op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL) {
        return true;
    }
    return false;
}

/**
 * 判断指令是否是内存写入操作
 */
bool schedule_is_mem_write(lir_op_t *op) {
    // 写入 indirect_addr
    if (op->output && op->output->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        return true;
    }
    // CALL 和 RT_CALL 可能写入内存
    if (op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL) {
        return true;
    }
    return false;
}

/**
 * 判断指令是否需要固定位置，不能参与调度
 * 这些指令作为调度屏障，调度只在屏障之间的"间隙"中进行
 */
static bool schedule_is_fixed(lir_op_t *op) {
    // 基本固定指令
    if (op->code == LIR_OPCODE_LABEL ||
        op->code == LIR_OPCODE_PHI ||
        op->code == LIR_OPCODE_SAFEPOINT ||
        op->code == LIR_OPCODE_FN_BEGIN ||
        op->code == LIR_OPCODE_FN_END) {
        return true;
    }
    // CALL 指令有副作用，不能跨越调度
    if (op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL) {
        return true;
    }
    // 控制流指令必须保持在末尾
    if (lir_op_branch(op) ||
        op->code == LIR_OPCODE_RET ||
        op->code == LIR_OPCODE_RETURN) {
        return true;
    }
    return false;
}


/**
 * 计算指令的执行延迟 (cycles)
 * 用于关键路径分析和调度优先级计算
 */
static int schedule_compute_latency(lir_op_t *op) {
    switch (op->code) {
        // 除法和求余指令延迟最高
        case LIR_OPCODE_SDIV:
        case LIR_OPCODE_UDIV:
        case LIR_OPCODE_SREM:
        case LIR_OPCODE_UREM:
            return 20; // 除法延迟约 20-60 cycles

        // 乘法指令
        case LIR_OPCODE_MUL:
            return 4; // 乘法延迟约 3-5 cycles

        // 内存读取操作
        case LIR_OPCODE_CALL:
        case LIR_OPCODE_RT_CALL:
            return 10; // 调用有较高延迟

        // 浮点转换
        case LIR_OPCODE_FTOSI:
        case LIR_OPCODE_FTOUI:
        case LIR_OPCODE_SITOF:
        case LIR_OPCODE_UITOF:
        case LIR_OPCODE_FTRUNC:
        case LIR_OPCODE_FEXT:
            return 4; // 浮点转换延迟

        // 默认单周期操作
        default:
            break;
    }

    // 内存读取操作延迟
    if (schedule_is_mem_read(op)) {
        return 4; // L1 cache hit 延迟
    }

    // 默认延迟为 1 cycle
    return 1;
}


/**
 * 计算指令的评分
 */
schedule_score_t schedule_compute_score(lir_op_t *op) {
    switch (op->code) {
        case LIR_OPCODE_PHI:
            return SCORE_PHI;
        case LIR_OPCODE_LABEL:
            return SCORE_LABEL;
        case LIR_OPCODE_FN_BEGIN:
            return SCORE_FN_BEGIN;
        case LIR_OPCODE_FN_END:
            return SCORE_FN_END;
        case LIR_OPCODE_SAFEPOINT:
            return SCORE_SAFEPOINT;

        // 分支和控制流指令
        case LIR_OPCODE_BAL:
        case LIR_OPCODE_BLT:
        case LIR_OPCODE_BLE:
        case LIR_OPCODE_BGT:
        case LIR_OPCODE_BGE:
        case LIR_OPCODE_BULT:
        case LIR_OPCODE_BULE:
        case LIR_OPCODE_BUGT:
        case LIR_OPCODE_BUGE:
        case LIR_OPCODE_BEE:
        case LIR_OPCODE_BNE:
        case LIR_OPCODE_RET:
        case LIR_OPCODE_RETURN:
            return SCORE_CONTROL;

        // 比较指令 (设置标志位)
        case LIR_OPCODE_SLT:
        case LIR_OPCODE_SLE:
        case LIR_OPCODE_SGT:
        case LIR_OPCODE_SGE:
        case LIR_OPCODE_SEE:
        case LIR_OPCODE_SNE:
        case LIR_OPCODE_USLT:
        case LIR_OPCODE_USLE:
        case LIR_OPCODE_USGT:
        case LIR_OPCODE_USGE:
            return SCORE_FLAGS;

        // 调用指令
        case LIR_OPCODE_CALL:
        case LIR_OPCODE_RT_CALL:
            return SCORE_MEMORY_WRITE; // 调用可能有副作用

        // NOP 用于参数定义
        case LIR_OPCODE_NOP:
            return SCORE_ARG;

        default:
            break;
    }

    // 内存操作
    if (schedule_is_mem_write(op)) {
        return SCORE_MEMORY_WRITE;
    }
    if (schedule_is_mem_read(op)) {
        return SCORE_MEMORY_READ;
    }

    return SCORE_DEFAULT;
}

/**
 * 创建调度节点
 */
static schedule_node_t *schedule_node_new(lir_op_t *op, linked_node *node, int id) {
    schedule_node_t *sn = NEW(schedule_node_t);
    sn->op = op;
    sn->node = node;
    sn->score = schedule_compute_score(op);
    sn->dep_count = 0;
    sn->successors = slice_new();
    sn->scheduled = false;
    sn->original_id = id;
    sn->latency = schedule_compute_latency(op);
    sn->priority = 0; // 将在依赖图构建后计算
    return sn;
}

/**
 * 添加依赖关系: from -> to (to 依赖 from)
 */
static void schedule_add_dep(schedule_node_t *from, schedule_node_t *to) {
    if (from == to) {
        return;
    }
    // 检查是否已存在该依赖
    for (int i = 0; i < from->successors->count; i++) {
        if (from->successors->take[i] == to) {
            return;
        }
    }
    slice_push(from->successors, to);
    to->dep_count++;
}

/**
 * 比较两个调度节点的优先级
 * 返回 true 表示 a 优先级更高 (应该先调度)
 */
static bool schedule_node_less(schedule_node_t *a, schedule_node_t *b) {
    // 1. 首先按分数比较 (保证指令类型的基本顺序)
    if (a->score != b->score) {
        return a->score < b->score;
    }
    // 2. 分数相同时，按关键路径优先级排序
    //    优先调度关键路径更长的指令 (更高的 priority)
    if (a->priority != b->priority) {
        return a->priority > b->priority;
    }
    // 3. 关键路径也相同时，优先调度高延迟指令
    if (a->latency != b->latency) {
        return a->latency > b->latency;
    }
    // 4. 最后保持原始顺序
    return a->original_id < b->original_id;
}

/**
 * 从就绪队列中取出优先级最高的节点
 */
static schedule_node_t *schedule_pop_best(slice_t *ready) {
    if (ready->count == 0) {
        return NULL;
    }

    int best_idx = 0;
    schedule_node_t *best = ready->take[0];

    for (int i = 1; i < ready->count; i++) {
        schedule_node_t *current = ready->take[i];
        if (schedule_node_less(current, best)) {
            best = current;
            best_idx = i;
        }
    }

    // 从队列中移除
    for (int i = best_idx; i < ready->count - 1; i++) {
        ready->take[i] = ready->take[i + 1];
    }
    ready->count--;

    return best;
}


/**
 * 构建依赖图 (仅用于 segment 内部的非固定指令)
 */
static void schedule_build_deps(closure_t *c, schedule_ctx_t *ctx) {
    // 遍历所有节点，构建依赖关系
    for (int i = 0; i < ctx->nodes->count; i++) {
        schedule_node_t *node = ctx->nodes->take[i];
        lir_op_t *op = node->op;

        // 1. 数据依赖: 使用的变量必须先被定义
        slice_t *uses = extract_var_operands(op, FLAG(LIR_FLAG_USE));
        for (int j = 0; j < uses->count; j++) {
            lir_var_t *var = uses->take[j];
            schedule_node_t *def_node = table_get(ctx->var_def_table, var->ident);
            if (def_node && def_node != node) {
                schedule_add_dep(def_node, node);
            }
        }

        // 2. 更新变量定义表
        slice_t *defs = extract_var_operands(op, FLAG(LIR_FLAG_DEF));
        for (int j = 0; j < defs->count; j++) {
            lir_var_t *var = defs->take[j];
            table_set(ctx->var_def_table, var->ident, node);
        }

        // 3. 寄存器依赖: RAW/WAW/WAR 依赖
        // 3.1 RAW: 当前指令使用的寄存器必须先被定义
        slice_t *reg_uses = extract_op_operands(op, FLAG(LIR_OPERAND_REG), FLAG(LIR_FLAG_USE), true);
        for (int j = 0; j < reg_uses->count; j++) {
            reg_t *reg = reg_alloc_find(reg_uses->take[j]);

            char *key = reg_key(reg->alloc_id);
            schedule_node_t *def_node = table_get(ctx->reg_def_table, key);
            if (def_node && def_node != node) {
                schedule_add_dep(def_node, node); // RAW 依赖
            }
            // 更新最后使用位置 (用于 WAR)
            table_set(ctx->reg_use_table, key, node);
        }

        // 3.2 WAW 和 WAR: 当前指令定义的寄存器
        slice_t *reg_defs = extract_op_operands(op, FLAG(LIR_OPERAND_REG), FLAG(LIR_FLAG_DEF), true);
        for (int j = 0; j < reg_defs->count; j++) {
            reg_t *reg = reg_alloc_find(reg_defs->take[j]);
            char *key = reg_key(reg->alloc_id);

            // WAW: 依赖上一个定义该寄存器的指令
            schedule_node_t *last_def = table_get(ctx->reg_def_table, key);
            if (last_def && last_def != node) {
                schedule_add_dep(last_def, node);
            }

            // WAR: 依赖上一个使用该寄存器的指令
            schedule_node_t *last_use = table_get(ctx->reg_use_table, key);
            if (last_use && last_use != node) {
                schedule_add_dep(last_use, node);
            }

            // 更新寄存器定义表
            table_set(ctx->reg_def_table, key, node);
        }

        // 4. 内存依赖: 保持内存操作的相对顺序
        bool is_mem_op = schedule_is_mem_read(op) || schedule_is_mem_write(op);
        if (is_mem_op) {
            schedule_node_t *last_mem = table_get(ctx->last_mem_op, "_last_mem_");
            if (last_mem && last_mem != node) {
                schedule_add_dep(last_mem, node);
            }
            table_set(ctx->last_mem_op, "_last_mem_", node);
        }
    }
}

/**
 * 递归计算节点的关键路径优先级
 * 返回从该节点到出口的最长加权路径长度
 */
static int schedule_compute_priority_recursive(schedule_node_t *node) {
    // 如果已经计算过，直接返回
    if (node->priority > 0) {
        return node->priority;
    }

    // 叶子节点 (没有后继): priority = 自身延迟
    if (node->successors->count == 0) {
        node->priority = node->latency;
        return node->priority;
    }

    // 非叶子节点: priority = 自身延迟 + max(后继的priority)
    int max_succ_priority = 0;
    for (int i = 0; i < node->successors->count; i++) {
        schedule_node_t *succ = node->successors->take[i];
        int succ_priority = schedule_compute_priority_recursive(succ);
        if (succ_priority > max_succ_priority) {
            max_succ_priority = succ_priority;
        }
    }
    node->priority = node->latency + max_succ_priority;
    return node->priority;
}

/**
 * 计算所有节点的关键路径优先级
 */
static void schedule_compute_priorities(schedule_ctx_t *ctx) {
    // 从每个节点开始计算，递归会处理依赖
    for (int i = 0; i < ctx->nodes->count; i++) {
        schedule_node_t *node = ctx->nodes->take[i];
        schedule_compute_priority_recursive(node);
    }
}

/**
 * 执行调度算法
 */
static void schedule_execute(schedule_ctx_t *ctx) {
    // 1. 初始化就绪队列
    for (int i = 0; i < ctx->nodes->count; i++) {
        schedule_node_t *node = ctx->nodes->take[i];
        if (node->dep_count == 0) {
            slice_push(ctx->ready, node);
        }
    }

    // 2. 按优先级调度
    while (ctx->ready->count > 0) {
        schedule_node_t *best = schedule_pop_best(ctx->ready);
        if (!best) {
            break;
        }

        best->scheduled = true;
        slice_push(ctx->result, best);

        // 更新后继节点的依赖计数
        for (int i = 0; i < best->successors->count; i++) {
            schedule_node_t *succ = best->successors->take[i];
            succ->dep_count--;
            if (succ->dep_count == 0 && !succ->scheduled) {
                slice_push(ctx->ready, succ);
            }
        }
    }

    // 3. 检查是否有循环依赖 (理论上不应该发生)
    if (ctx->result->count != ctx->nodes->count) {
        assert(false);
    }
}


/**
 * 对单个 segment（固定指令之间的间隙）进行调度
 */
static void schedule_segment(closure_t *c, basic_block_t *block, slice_t *segment_ops, slice_t *result) {
    // segment 太小，不需要调度，直接添加到结果
    if (segment_ops->count <= 2) {
        for (int i = 0; i < segment_ops->count; i++) {
            slice_push(result, segment_ops->take[i]);
        }
        return;
    }

    // 初始化上下文
    schedule_ctx_t ctx;
    ctx.block = block;
    ctx.nodes = slice_new();
    ctx.ready = slice_new();
    ctx.result = slice_new();
    ctx.var_def_table = table_new();
    ctx.last_mem_op = table_new();
    ctx.reg_def_table = table_new();
    ctx.reg_use_table = table_new();

    // 收集 segment 内的指令节点
    for (int i = 0; i < segment_ops->count; i++) {
        lir_op_t *op = segment_ops->take[i];
        schedule_node_t *node = schedule_node_new(op, NULL, i);
        slice_push(ctx.nodes, node);
    }

    // 构建依赖图
    schedule_build_deps(c, &ctx);

    // 计算关键路径优先级
    schedule_compute_priorities(&ctx);

    // 执行调度
    schedule_execute(&ctx);

    // 将调度结果添加到总结果
    for (int i = 0; i < ctx.result->count; i++) {
        schedule_node_t *node = ctx.result->take[i];
        slice_push(result, node->op);
    }
}

/**
 * 对单个基本块进行指令调度
 * 采用分段调度策略：固定指令作为屏障，只在屏障之间的 segment 内进行调度
 */
static void schedule_block(closure_t *c, basic_block_t *block) {
    if (!block || !block->operations || linked_count(block->operations) <= 1) {
        return;
    }

    // 收集所有指令
    slice_t *all_ops = slice_new();
    linked_node *current = linked_first(block->operations);
    while (current->value) {
        slice_push(all_ops, current->value);
        current = current->succ;
    }

    // 如果指令数太少，不需要调度
    if (all_ops->count <= 2) {
        return;
    }

    // 结果列表
    slice_t *result = slice_new();
    // 当前 segment 中的非固定指令
    slice_t *segment = slice_new();

    // 遍历所有指令，按固定指令分段
    for (int i = 0; i < all_ops->count; i++) {
        lir_op_t *op = all_ops->take[i];

        if (schedule_is_fixed(op)) {
            // 遇到固定指令：先调度当前 segment，再添加固定指令
            if (segment->count > 0) {
                schedule_segment(c, block, segment, result);
                segment->count = 0; // 清空 segment
            }
            // 固定指令直接加入结果
            slice_push(result, op);
        } else {
            // 非固定指令加入当前 segment
            slice_push(segment, op);
        }
    }

    // 处理最后一个 segment
    if (segment->count > 0) {
        schedule_segment(c, block, segment, result);
    }

    // 应用调度结果
    linked_t *new_ops = linked_new();
    for (int i = 0; i < result->count; i++) {
        linked_push(new_ops, result->take[i]);
    }
    block->operations = new_ops;
}

/**
 * 对闭包中的所有基本块进行指令调度
 */
void schedule(closure_t *c) {
    if (!c || !c->blocks) {
        return;
    }

    for (int i = 0; i < c->blocks->count; i++) {
        basic_block_t *block = c->blocks->take[i];
        schedule_block(c, block);
        lir_set_quick_op(block);
    }
}
