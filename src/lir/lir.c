#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "lir.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"
#include "utils/error.h"

lir_operand *set_indirect_addr(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_VAR) {
        lir_operand_var *var = operand->value;
        var->indirect_addr = true;
        return operand;
    } else if (operand->type == LIR_OPERAND_ADDR) {
        lir_operand_addr *addr = operand->value;
        addr->indirect_addr = true;
        return operand;
    }

    error_exit("[set_indirect_addr] operand_type != LIR_OPERAND_VAR or LIR_OPERAND_ADDR, actual %d",
               operand->type);
    return NULL;
}

lir_operand *lir_new_addr_operand(lir_operand *base, int offset, type_base_t type_base) {
    lir_operand_addr *addr_operand = malloc(sizeof(lir_operand_addr));
    addr_operand->base = base;
    addr_operand->offset = offset;
    addr_operand->type_base = type_base;

    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_ADDR;
    operand->value = addr_operand;
    return operand;
}

lir_op_t *lir_op_runtime_call(char *name, lir_operand *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_RUNTIME_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op_t *lir_op_builtin_call(char *name, lir_operand *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_BUILTIN_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op_t *lir_op_call(char *name, lir_operand *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_ACTUAL_PARAMS, params_operand);
    return lir_op_new(LIR_OPCODE_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

/**
 * 临时变量是否影响变量入栈？
 * @param type
 * @return
 */
lir_operand *lir_new_temp_var_operand(closure_t *c, type_t type) {
    string unique_ident = LIR_UNIQUE_NAME(TEMP_IDENT);

    symbol_set_temp_ident(unique_ident, type);
    lir_var_decl *local = lir_new_var_decl(c, unique_ident, type);
    list_push(c->var_decls, local);

    return LIR_NEW_OPERAND(LIR_OPERAND_VAR, lir_new_var_operand(c, unique_ident));
}

lir_operand *lir_new_label_operand(char *ident, bool is_local) {
    lir_operand_symbol_label *label = NEW(lir_operand_symbol_label);
    label->ident = ident;
    label->is_local = is_local;

    lir_operand *operand = NEW(lir_operand);
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

lir_operand *lir_copy_label_operand(lir_operand *label_operand) {
    lir_operand_symbol_label *label = label_operand->value;
    return lir_new_label_operand(label->ident, label->is_local);
}

lir_op_t *lir_op_bal(lir_operand *label) {
    return lir_op_new(LIR_OPCODE_BAL, NULL, NULL, lir_copy_label_operand(label));
}

lir_op_t *lir_op_move(lir_operand *dst, lir_operand *src) {
    return lir_op_new(LIR_OPCODE_MOVE, src, NULL, dst);
}

lir_op_t *lir_op_new(lir_opcode code, lir_operand *first, lir_operand *second, lir_operand *result) {
    // 变量 copy,避免寄存器分配时相互粘连
    if (first != NULL && first->type == LIR_OPERAND_VAR) {
        lir_operand_var *operand_var = first->value;
        first = LIR_NEW_OPERAND(LIR_OPERAND_VAR, DEEP_COPY(lir_operand_var, operand_var));
    }

    if (second != NULL && second->type == LIR_OPERAND_VAR) {
        lir_operand_var *operand_var = second->value;
        second = LIR_NEW_OPERAND(LIR_OPERAND_VAR, DEEP_COPY(lir_operand_var, operand_var));
    }

    if (result != NULL && result->type == LIR_OPERAND_VAR) {
        lir_operand_var *operand_var = result->value;

        result = LIR_NEW_OPERAND(LIR_OPERAND_VAR, DEEP_COPY(lir_operand_var, operand_var));
    }

    lir_op_t *op = NEW(lir_op_t);
    op->code = code;
    op->first = first;
    op->second = second;
    op->output = result;

#ifdef DEBUG_COMPILER_LIR
    debug_lir(lir_line, op);
#endif

    return op;
}

closure_t *lir_new_closure(ast_closure_t *ast) {
    closure_t *new = NEW(closure_t);
    new->name = ast->function->name;
    new->env_name = ast->env_name;
    new->parent = NULL;
    new->operations = NULL;
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

lir_operand *lir_new_phi_body(lir_operand_var *var, uint8_t count) {
    lir_operand *operand = NEW(lir_operand);

    slice_t *phi_body = slice_new();
    for (int i = 0; i < count; ++i) {
        slice_push(phi_body, LIR_NEW_VAR_OPERAND(var->ident));
    }

    operand->type = LIR_OPERAND_PHI_BODY;
    operand->value = phi_body;
    return operand;
}

lir_operand_var *lir_new_var_operand(closure_t *c, char *ident) {
    lir_operand_var *var = NEW(lir_operand_var);
    var->ident = ident;
    var->old = ident;
//    var->reg_index = 0;

    // 1. 读取符号信息
    lir_var_decl *local = table_get(c->var_decl_table, ident);
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
lir_var_decl *lir_new_var_decl(closure_t *c, char *ident, type_t type) {
    lir_var_decl *var = NEW(lir_var_decl);
    var->type = type;
    var->ident = ident;
    table_set(c->var_decl_table, ident, var);
    return var;
}

lir_operand *lir_new_empty_operand() {
    lir_operand *operand = NEW(lir_operand);
    operand->type = 0;
    operand->value = NULL;
    return operand;
}

type_base_t lir_operand_type_base(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_VAR) {
        lir_operand_var *var = operand->value;
        return var->type_base;
    }

    if (operand->type == LIR_OPERAND_ADDR) {
        lir_operand_addr *addr = operand->value;
        return addr->type_base;
    }

    if (operand->type == LIR_OPERAND_SYMBOL_VAR) {
        lir_operand_symbol_var *s = operand->value;
        return s->type;
    }

    if (operand->type == LIR_OPERAND_IMM) {
        lir_operand_immediate *imm = operand->value;
        return imm->type;
    }

    return TYPE_UNKNOWN;
}

uint8_t lir_operand_sizeof(lir_operand *operand) {
    return type_base_sizeof(lir_operand_type_base(operand));
}

/**
 * 读取 operand 中包含的所有 var， 并返回 lir_operand_var slice
 * @param operand
 * @return
 */
slice_t *lir_operand_vars(lir_operand *operand) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }
    slice_t *operands = lir_operand_nests(operand, FLAG(LIR_OPERAND_VAR));
    SLICE_FOR(operands) {
        lir_operand *o = SLICE_VALUE(operands);
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

bool lir_operand_equal(lir_operand *a, lir_operand *b) {
    if (a->type != b->type) {
        return false;
    }
    if (a->type == LIR_OPERAND_REG) {
        reg_t *reg_a = a->value;
        reg_t *reg_b = b->value;
        return reg_a->index == reg_b->index;
    }

    if (a->type == LIR_OPERAND_STACK) {
        lir_operand_stack *stack_a = a->value;
        lir_operand_stack *stack_b = b->value;
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
slice_t *lir_operand_nests(lir_operand *operand, uint64_t flag) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }
    if (flag & FLAG(LIR_OPERAND_VAR) && operand->type == LIR_OPERAND_VAR) {
        slice_push(result, operand);
    }
    if (flag & FLAG(LIR_OPERAND_REG) && operand->type == LIR_OPERAND_REG) {
        slice_push(result, operand);
    }

    if (operand->type == LIR_OPERAND_ACTUAL_PARAMS) {
        slice_t *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand *o = operands->take[i];
            assert(o->type != LIR_OPERAND_ACTUAL_PARAMS && "ACTUAL_PARAM nesting is not allowed");

            if (flag & FLAG(LIR_OPERAND_VAR) && o->type == LIR_OPERAND_VAR) {
                slice_push(result, o);
            }

            if (flag & FLAG(LIR_OPERAND_REG) && o->type == LIR_OPERAND_REG) {
                slice_push(result, o);
            }
        }
    }

    if (flag & FLAG(LIR_OPERAND_VAR) && operand->type == LIR_OPERAND_FORMAL_PARAMS) {
        slice_t *formal_params = operand->value;
        for (int i = 0; i < formal_params->count; ++i) {
            lir_operand_var *var = formal_params->take[i];
            slice_push(result, LIR_NEW_OPERAND(LIR_OPERAND_VAR, var));
        }
    }

    return result;
}

slice_t *lir_op_nest_operands(lir_op_t *op, uint64_t flag) {
    slice_t *result = lir_operand_nests(op->output, flag);
    slice_append(result, lir_operand_nests(op->first, flag));
    slice_append(result, lir_operand_nests(op->second, flag));

    return result;
}
