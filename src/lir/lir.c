#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "lir.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"

lir_operand *lir_new_memory_operand(lir_operand *base, size_t offset, size_t length) {
    lir_operand_memory *memory_operand = malloc(sizeof(lir_operand_memory));
    memory_operand->base = base;
    // 根据 index + 类型计算偏移量
    memory_operand->offset = offset;
    memory_operand->length = length;

    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_MEMORY;
    operand->value = memory_operand;
    return operand;
}

lir_op *lir_runtime_call(char *name, lir_operand *result, int arg_count, ...) {
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        params_operand->list[params_operand->count++] = param;
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
    return lir_op_new(LIR_OP_TYPE_RUNTIME_CALL, lir_new_label_operand(name), call_params_operand, result);
}


lir_op *lir_builtin_call(char *name, lir_operand *result, int arg_count, ...) {
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        params_operand->list[params_operand->count++] = param;
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
    return lir_op_new(LIR_OP_TYPE_BUILTIN_CALL, lir_new_label_operand(name), call_params_operand, result);
}


lir_operand *lir_new_var_operand(char *ident) {
    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_VAR;
    operand->value = LIR_NEW_VAR_OPERAND(ident);
    return operand;
}

/**
 * 临时变量是否影响变量入栈？
 * @param type
 * @return
 */
lir_operand *lir_new_temp_var_operand(ast_type type) {
    string unique_ident = LIR_UNIQUE_NAME(TEMP_IDENT);

    symbol_set_temp_ident(unique_ident, type);

    return lir_new_var_operand(unique_ident);
}

lir_operand *lir_new_label_operand(char *ident) {
    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_LABEL;
    operand->value = LIR_NEW_LABEL_OPERAND(ident);
    return operand;
}

lir_op *lir_op_label(char *ident) {
    return lir_op_new(LIR_OP_TYPE_LABEL, NULL, NULL, lir_new_label_operand(ident));
}

lir_op *lir_op_unique_label(char *ident) {
    return lir_op_label(LIR_UNIQUE_NAME(ident));
}

lir_op *lir_op_goto(lir_operand *label) {
    return lir_op_new(LIR_OP_TYPE_GOTO, NULL, NULL, label);
}

lir_op *lir_op_move(lir_operand *dst, lir_operand *src) {
    return lir_op_new(LIR_OP_TYPE_MOVE, src, NULL, dst);
}

lir_op *lir_op_new(lir_op_type type, lir_operand *first, lir_operand *second, lir_operand *result) {
    // 字符串 copy
    if (first != NULL && first->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = first->value;
        first = lir_new_var_operand(operand_var->ident);
    }

    if (second != NULL && second->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = second->value;
        second = lir_new_var_operand(operand_var->ident);
    }

    if (result != NULL && result->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = result->value;
        result = lir_new_var_operand(operand_var->ident);
    }

    lir_op *op = NEW(lir_op);
    op->op = type;
    op->first = first;
    op->second = second;
    op->result = result;
    op->pred = NULL;
    op->succ = NULL;

#ifdef DEBUG_COMPILER_LIR
    debug_lir(lir_line, op);
#endif

    return op;
}

list_op *list_op_new() {
    list_op *list = NEW(list_op);
    list->count = 0;

    list->front = NULL;
    list->rear = NULL;

    return list;
}

void list_op_push(list_op *l, lir_op *op) {
    if (l->count == 0) {
        l->front = op;
        l->rear = op;
        l->count = 1;
        return;
    }

    l->rear->succ = op;
    op->pred = l->rear;
    l->rear = op;

    l->count++;
}

list_op *list_op_append(list_op *dst, list_op *src) {
    if (src->count == 0) {
        return dst;
    }

    // src.count 肯定 > 0
    if (dst->count == 0) {
        dst->front = src->front;
        dst->rear = src->rear;
        dst->count = src->count;
        return dst;
    }

    dst->count += src->count;
    // 链接
    dst->rear->succ = src->front;
    // 关联关系
    dst->rear = src->rear;
    return dst;
}

closure *lir_new_closure(ast_closure_decl *ast) {
    closure *new = NEW(closure);
    new->name = ast->function->name;
    new->env_name = ast->env_name;
    new->parent = NULL;
    new->operates = NULL;
    new->entry = NULL;
    new->globals.count = 0;
    new->fixed_regs.count = 0;
    new->blocks.count = 0;
    new->order_blocks.count = 0;

    new->interval_table = table_new();

    return new;
}

lir_basic_block *lir_new_basic_block() {
    lir_basic_block *basic_block = NEW(lir_basic_block);
    basic_block->operates = list_op_new();
    basic_block->preds.count = 0;
    basic_block->succs.count = 0;

    basic_block->forward_succs.count = 0;
    basic_block->incoming_forward_count = 0;
    basic_block->use.count = 0;
    basic_block->def.count = 0;
    basic_block->live_in.count = 0;
    basic_block->live_out.count = 0;
    basic_block->dom.count = 0;
    basic_block->df.count = 0;
    basic_block->be_idom.count = 0;
    basic_block->loop.tree_high = 0;
    basic_block->loop.index = 0;
    basic_block->loop.depth = 0;
    basic_block->loop.flag = 0;

    return basic_block;
}

bool list_op_is_null(lir_op *op) {
    return op == NULL;
}

bool lir_blocks_contains(lir_basic_blocks blocks, uint8_t label) {
    for (int i = 0; i < blocks.count; ++i) {
        if (blocks.list[i]->label == label) {
            return true;
        }
    }
    return false;
}

lir_operand *lir_new_phi_body(lir_operand_var *var, uint8_t count) {
    lir_operand *operand = NEW(lir_operand);

    lir_operand_phi_body *phi_body = NEW(lir_operand_phi_body);
    for (int i = 0; i < count; ++i) {
        phi_body->list[i] = LIR_NEW_VAR_OPERAND(var->ident);
    }

    operand->type = LIR_OPERAND_TYPE_PHI_BODY;
    operand->value = phi_body;
    return operand;
}
