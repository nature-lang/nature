#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "lir.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"
#include "utils/error.h"

// 目前仅支持 var 的 copy
lir_operand_t *lir_operand_copy(lir_operand_t *operand) {
    if (!operand) {
        return NULL;
    }

    lir_operand_t *new_operand = NEW(lir_operand_t);
    new_operand->type = operand->type;
    new_operand->value = operand->value;

    if (new_operand->type == LIR_OPERAND_VAR) {
        lir_var_t *var = new_operand->value;
        lir_var_t *new_var = NEW(lir_var_t);
        new_var->ident = var->ident;
        new_var->old = var->old;
        new_var->type_base = var->type_base;
        new_var->flag = var->flag;
        new_var->decl = var->decl;
        new_var->indirect_addr = var->indirect_addr;
        new_operand->value = new_var;
        return new_operand;
    }
    return new_operand;
}

lir_operand_t *set_indirect_addr(lir_operand_t *operand) {
    if (operand->type == LIR_OPERAND_VAR) {
        lir_var_t *var = operand->value;
        var->indirect_addr = true;
        return operand;
    } else if (operand->type == LIR_OPERAND_ADDR) {
        lir_addr_t *addr = operand->value;
        addr->indirect_addr = true;
        return operand;
    }

    error_exit("[set_indirect_addr] operand_type != LIR_OPERAND_VAR or LIR_OPERAND_ADDR, actual %d",
               operand->type);
    return NULL;
}

lir_operand_t *lir_new_addr_operand(lir_operand_t *base, int offset, type_base_t type_base) {
    lir_addr_t *addr_operand = malloc(sizeof(lir_addr_t));
    addr_operand->base = base;
    addr_operand->offset = offset;
    addr_operand->type_base = type_base;

    lir_operand_t *operand = NEW(lir_operand_t);
    operand->type = LIR_OPERAND_ADDR;
    operand->value = addr_operand;
    return operand;
}

lir_op_t *lir_op_runtime_call(char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_RUNTIME_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op_t *lir_op_builtin_call(char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_BUILTIN_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op_t *lir_op_call(char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

/**
 * 临时变量是否影响变量入栈？
 * @param type
 * @return
 */
lir_operand_t *lir_new_temp_var_operand(closure_t *c, type_t type) {
    string unique_ident = LIR_UNIQUE_NAME(TEMP_IDENT);

    symbol_set_temp_ident(unique_ident, type);
    lir_new_var_decl(c, unique_ident, type);

    return LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, unique_ident));
}

lir_operand_t *lir_new_label_operand(char *ident, bool is_local) {
    lir_symbol_label_t *label = NEW(lir_symbol_label_t);
    label->ident = ident;
    label->is_local = is_local;

    lir_operand_t *operand = NEW(lir_operand_t);
    operand->type = LIR_OPERAND_SYMBOL_LABEL;
    operand->value = label;
    return operand;
}

lir_op_t *lir_op_label(char *ident, bool is_local) {
    return lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, lir_new_label_operand(ident, is_local));
}

lir_op_t *lir_op_unique_label(char *ident) {
    return lir_op_label(LIR_UNIQUE_NAME(ident), true);
}

lir_operand_t *lir_copy_label_operand(lir_operand_t *label_operand) {
    lir_symbol_label_t *label = label_operand->value;
    return lir_new_label_operand(label->ident, label->is_local);
}

lir_op_t *lir_op_bal(lir_operand_t *label) {
    return lir_op_new(LIR_OPCODE_BAL, NULL, NULL, lir_copy_label_operand(label));
}

lir_op_t *lir_op_move(lir_operand_t *dst, lir_operand_t *src) {
    return lir_op_new(LIR_OPCODE_MOVE, src, NULL, dst);
}

lir_op_t *lir_op_new(lir_opcode_e code, lir_operand_t *first, lir_operand_t *second, lir_operand_t *result) {
    lir_op_t *op = NEW(lir_op_t);
    op->code = code;
    op->first = lir_operand_copy(first); // 这里的 copy 并不深度，而是 copy 了指针！
    op->second = lir_operand_copy(second);
    op->output = lir_operand_copy(result);

    if (op->first && op->first->type == LIR_OPERAND_VAR) {
        lir_var_t *var = op->first->value;

        var->flag |= FLAG(VAR_FLAG_SECOND);
    }
    if (op->output && op->output->type == LIR_OPERAND_VAR) {
        lir_var_t *var = op->output->value;
        var->flag |= FLAG(VAR_FLAG_OUTPUT);
    }

    return op;
}

closure_t *lir_new_closure(ast_closure_t *ast) {
    closure_t *new = NEW(closure_t);
    new->name = ast->function->name;
    new->env_name = ast->env_name;
    new->parent = NULL;
    new->operations = NULL;
    new->asm_operations = slice_new();
    new->asm_var_decls = slice_new();
    new->entry = NULL;
    new->globals = slice_new();
    new->blocks = slice_new(); // basic_block_t
//    new->order_blocks = slice_new(); // basic_block_t

    new->interval_table = table_new();

    new->var_decl_table = table_new();
    new->var_decls = list_new();
    new->formal_params = slice_new();
//    new->stack_length = 0;
    new->stack_slot = 0;
    new->loop_count = 0;
    new->loop_ends = slice_new();
    new->loop_headers = slice_new();

    new->interval_count = alloc_reg_count() + 1;

    return new;
}

basic_block_t *lir_new_basic_block(char *name, uint8_t label_index) {
    basic_block_t *basic_block = NEW(basic_block_t);
    basic_block->name = name;
    basic_block->id = label_index;

    basic_block->operations = list_new();
    basic_block->preds = slice_new();
    basic_block->succs = slice_new();

    basic_block->forward_succs = slice_new();
    basic_block->incoming_forward_count = 0;
    basic_block->use = slice_new();
    basic_block->def = slice_new();
    basic_block->loop_ends = slice_new();
    basic_block->live_in = slice_new();
    basic_block->live_out = slice_new();
    basic_block->dom = slice_new();
    basic_block->df = slice_new();
    basic_block->be_idom = slice_new();
    basic_block->loop.index = -1;
    memset(basic_block->loop.index_map, 0, sizeof(basic_block->loop.index_map));
    basic_block->loop.depth = 0;
    basic_block->loop.visited = false;
    basic_block->loop.active = false;
    basic_block->loop.header = false;
    basic_block->loop.end = false;

    return basic_block;
}

bool lir_blocks_contains(slice_t *blocks, uint8_t label) {
    for (int i = 0; i < blocks->count; ++i) {
        if (((basic_block_t *) blocks->take[i])->id == label) {
            return true;
        }
    }
    return false;
}

lir_operand_t *lir_new_phi_body(closure_t *c, lir_var_t *var, uint8_t count) {
    lir_operand_t *operand = NEW(lir_operand_t);

    slice_t *phi_body = slice_new();
    for (int i = 0; i < count; ++i) {
        slice_push(phi_body, lir_new_var_operand(c, var->ident));
    }

    operand->type = LIR_OPERAND_PHI_BODY;
    operand->value = phi_body;
    return operand;
}

lir_var_t *lir_new_var_operand(closure_t *c, char *ident) {
    lir_var_t *var = NEW(lir_var_t);
    var->ident = ident;
    var->old = ident;
    var->flag = 0;

    // 1. 读取符号信息
    lir_var_decl_t *local = table_get(c->var_decl_table, ident);
    var->decl = local;
    var->type_base = local->type.base;

    return var;
}

/**
 * @param c
 * @param ident
 * @param type
 * @return
 */
lir_var_decl_t *lir_new_var_decl(closure_t *c, char *ident, type_t type) {
    lir_var_decl_t *var_decl = NEW(lir_var_decl_t);
    var_decl->type = type;
    var_decl->ident = ident;
    list_push(c->var_decls, var_decl);
    table_set(c->var_decl_table, ident, var_decl);
    return var_decl;
}

lir_operand_t *lir_new_empty_operand() {
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->type = 0;
    operand->value = NULL;
    return operand;
}

type_base_t lir_operand_type_base(lir_operand_t *operand) {
    assert(operand->type != LIR_OPERAND_REG);

    if (operand->type == LIR_OPERAND_VAR) {
        lir_var_t *var = operand->value;
        return var->type_base;
    }

    if (operand->type == LIR_OPERAND_ADDR) {
        lir_addr_t *addr = operand->value;
        return addr->type_base;
    }

    if (operand->type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *s = operand->value;
        return s->type;
    }

    if (operand->type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = operand->value;
        return imm->type;
    }

    return TYPE_UNKNOWN;
}

uint8_t lir_operand_sizeof(lir_operand_t *operand) {
    return type_base_sizeof(lir_operand_type_base(operand));
}

/**
 * 读取 operand 中包含的所有 var， 并返回 lir_var_tslice
 * @param operand
 * @return
 */
slice_t *lir_operand_vars(lir_operand_t *operand) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }
    slice_t *operands = lir_nest_operands(operand, FLAG(LIR_OPERAND_VAR));
    SLICE_FOR(operands) {
        lir_operand_t *o = SLICE_VALUE(operands);
        slice_push(result, o->value);
    }

    return result;
}

bool lir_op_is_branch(lir_op_t *op) {
    if (op->code == LIR_OPCODE_BAL || op->code == LIR_OPCODE_BEQ) {
        return true;
    }

    return false;
}

bool lir_op_is_call(lir_op_t *op) {
    if (op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_BUILTIN_CALL || op->code == LIR_OPCODE_RUNTIME_CALL) {
        return true;
    }
    return false;
}

bool lir_operand_equal(lir_operand_t *a, lir_operand_t *b) {
    if (a->type != b->type) {
        return false;
    }
    if (a->type == LIR_OPERAND_REG) {
        reg_t *reg_a = a->value;
        reg_t *reg_b = b->value;
        return reg_a->index == reg_b->index;
    }

    if (a->type == LIR_OPERAND_STACK) {
        lir_stack_t *stack_a = a->value;
        lir_stack_t *stack_b = b->value;
        return stack_a->slot == stack_b->slot;
    }

    return false;
}


/**
 * 返回 lir_operand，需要自己根据实际情况解析
 * @param c
 * @param operand
 * @param has_reg
 * @return
 */
slice_t *lir_nest_operands(lir_operand_t *operand, uint64_t flag) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }
    if (flag & FLAG(operand->type)) {
        slice_push(result, operand);
    }

    if (operand->type == LIR_OPERAND_ACTUAL_PARAMS) {
        slice_t *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand_t *o = operands->take[i];
            assert(o->type != LIR_OPERAND_ACTUAL_PARAMS && "ACTUAL_PARAM nesting is not allowed");

            if (flag & FLAG(o->type)) {
                slice_push(result, o);
            }
        }
    }

    // formal params only type lir_operand_var
    if (flag & FLAG(LIR_OPERAND_VAR) && operand->type == LIR_OPERAND_FORMAL_PARAMS) {
        slice_t *formal_params = operand->value;
        for (int i = 0; i < formal_params->count; ++i) {
            lir_var_t *var = formal_params->take[i];
            slice_push(result, LIR_NEW_OPERAND(LIR_OPERAND_VAR, var));
        }
    }

    // TODO phi body handle, phi body are var or imm

    return result;
}

slice_t *lir_input_operands(lir_op_t *op, uint64_t flag) {
    slice_t *result = lir_nest_operands(op->first, flag);
    slice_append(result, lir_nest_operands(op->second, flag));

    return result;
}

slice_t *lir_output_operands(lir_op_t *op, uint64_t flag) {
    return lir_nest_operands(op->output, flag);
}

slice_t *lir_op_nest_operands(lir_op_t *op, uint64_t flag) {
    slice_t *result = lir_nest_operands(op->output, flag);
    slice_append(result, lir_nest_operands(op->first, flag));
    slice_append(result, lir_nest_operands(op->second, flag));

    return result;
}
bool lir_op_contain_cmp(lir_op_t *op) {
    if (op->code == LIR_OPCODE_BEQ ||
        op->code == LIR_OPCODE_SGT ||
        op->code == LIR_OPCODE_SGE ||
        op->code == LIR_OPCODE_SEE ||
        op->code == LIR_OPCODE_SNE ||
        op->code == LIR_OPCODE_SLT ||
        op->code == LIR_OPCODE_SLE) {
        return true;
    }
    return false;
}

void lir_init() {
    var_unique_count = 0;
    lir_line = 0;
}
