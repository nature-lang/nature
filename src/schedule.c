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
 */
static bool schedule_is_fixed(lir_op_t *op) {
    return op->code == LIR_OPCODE_LABEL ||
           op->code == LIR_OPCODE_PHI ||
           op->code == LIR_OPCODE_SAFEPOINT ||
           op->code == LIR_OPCODE_FN_BEGIN ||
           op->code == LIR_OPCODE_FN_END;
}

/**
 * 判断指令是否是控制流指令
 */
static bool schedule_is_control(lir_op_t *op) {
    return lir_op_branch(op) ||
           op->code == LIR_OPCODE_RET ||
           op->code == LIR_OPCODE_RETURN;
}

/**
 * 判断指令是否是 call 屏障
 * call 指令会产生副作用，不能跨越调度
 */
static bool schedule_is_call_barrier(lir_op_t *op) {
    return op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL;
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
    // 1. 首先按分数比较
    if (a->score != b->score) {
        return a->score < b->score;
    }
    // 2. 分数相同时，保持原始顺序 (源码顺序)
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
 * 构建依赖图
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

        // 注意: CALL 的 clobber 寄存器依赖已由 call 屏障（步骤5）隐式保证
        // 因为 call 屏障阻止了所有跨 CALL 的指令重排

        // 4. 内存依赖: 保持内存操作的相对顺序
        bool is_mem_op = schedule_is_mem_read(op) || schedule_is_mem_write(op);
        if (is_mem_op) {
            schedule_node_t *last_mem = table_get(ctx->last_mem_op, "_last_mem_");
            if (last_mem && last_mem != node) {
                schedule_add_dep(last_mem, node);
            }
            table_set(ctx->last_mem_op, "_last_mem_", node);
        }

        // 5. CALL 屏障依赖: CALL 指令前后的指令不能跨越 CALL 重排
        // - CALL 之前的所有指令必须在 CALL 之前执行完成
        // - CALL 之后的所有指令必须在 CALL 之后执行
        if (schedule_is_call_barrier(op)) {
            // 当前 CALL 依赖于它之前的所有指令
            for (int j = 0; j < i; j++) {
                schedule_node_t *prev_node = ctx->nodes->take[j];
                schedule_add_dep(prev_node, node);
            }
            // 更新 last_call_barrier
            table_set(ctx->last_mem_op, "_last_call_", node);
        } else {
            // 非 CALL 指令需要依赖于前面最近的 CALL
            schedule_node_t *last_call = table_get(ctx->last_mem_op, "_last_call_");
            if (last_call && last_call != node) {
                schedule_add_dep(last_call, node);
            }
        }

        // 6. 控制流依赖: 控制流指令必须在所有其他指令之后
        if (schedule_is_control(op)) {
            for (int j = 0; j < ctx->nodes->count; j++) {
                schedule_node_t *other = ctx->nodes->take[j];
                if (other != node && !schedule_is_control(other->op)) {
                    schedule_add_dep(other, node);
                }
            }
        }
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
        // 存在循环依赖，退回原始顺序
        ctx->result->count = 0;
        for (int i = 0; i < ctx->nodes->count; i++) {
            slice_push(ctx->result, ctx->nodes->take[i]);
        }
    }
}

/**
 * 将调度结果应用到基本块
 */
static void schedule_apply(schedule_ctx_t *ctx) {
    basic_block_t *block = ctx->block;

    // 创建新的操作链表
    linked_t *new_ops = linked_new();

    for (int i = 0; i < ctx->result->count; i++) {
        schedule_node_t *node = ctx->result->take[i];
        linked_push(new_ops, node->op);
    }

    // 替换原链表
    block->operations = new_ops;
}

/**
 * 对单个基本块进行指令调度
 */
static void schedule_block(closure_t *c, basic_block_t *block) {
    if (!block || !block->operations || linked_count(block->operations) <= 1) {
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

    // 收集所有指令节点
    int id = 0;
    linked_node *current = linked_first(block->operations);
    while (current->value) {
        lir_op_t *op = current->value;
        schedule_node_t *node = schedule_node_new(op, current, id++);
        slice_push(ctx.nodes, node);
        current = current->succ;
    }

    // 如果指令数太少，不需要调度
    if (ctx.nodes->count <= 2) {
        return;
    }

    // 构建依赖图
    schedule_build_deps(c, &ctx);

    // 执行调度
    schedule_execute(&ctx);

    // 应用调度结果
    schedule_apply(&ctx);
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
