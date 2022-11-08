#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "lir.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"
#include "utils/error.h"

static slice_t *extract_operands(lir_operand_t *operand, uint64_t flag) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }

    if (flag & FLAG(operand->type)) {
        slice_push(result, operand);
    }

    if (operand->type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        if (FLAG(addr->base->type) & flag) {
            slice_push(result, addr->base);
        }
        return result;
    }

    if (operand->type == LIR_OPERAND_ACTUAL_PARAMS) {
        slice_t *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand_t *o = operands->take[i];
            assert(o->type != LIR_OPERAND_ACTUAL_PARAMS && "ACTUAL_PARAM nesting is not allowed");

            if (FLAG(o->type) & flag) {
                slice_push(result, o);
            }
        }
        return result;
    }

    if (flag & FLAG(LIR_OPERAND_VAR) && operand->type == LIR_OPERAND_FORMAL_PARAMS) {
        slice_t *formal_params = operand->value;
        for (int i = 0; i < formal_params->count; ++i) { // 这里都是 def flag
            lir_var_t *var = formal_params->take[i];
            slice_push(result, LIR_NEW_OPERAND(LIR_OPERAND_VAR, var));
        }
    }

    // TODO phi body handle, phi body are var or imm

    return result;
}


static void set_operand_flag(lir_operand_t *operand, bool is_output) {
    if (!operand) {
        return;
    }

    if (operand->type == LIR_OPERAND_VAR) {
        // 仅 output 且 indirect_addr = false 才配置 def
        lir_var_t *var = operand->value;
        if (is_output) {
            var->flag |= FLAG(VR_FLAG_DEF);
        } else {
            var->flag |= FLAG(VR_FLAG_USE);
        }

        return;
    }

    if (operand->type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        if (addr->base->type == LIR_OPERAND_VAR) {
            lir_var_t *var = addr->base->value;
            var->flag |= FLAG(VR_FLAG_USE);
            var->flag |= FLAG(VR_FLAG_INDIRECT_ADDR_BASE);
        }
        return;
    }


    if (operand->type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        if (is_output) {
            reg->flag |= FLAG(VR_FLAG_DEF);
        } else {
            reg->flag |= FLAG(VR_FLAG_USE);
        }
        return;
    }

    if (operand->type == LIR_OPERAND_FORMAL_PARAMS) {
        slice_t *formal_params = operand->value;
        for (int i = 0; i < formal_params->count; ++i) { // 这里都是 def flag
            lir_var_t *var = formal_params->take[i];
            var->flag |= FLAG(VR_FLAG_DEF);
        }
        return;
    }

    // 剩下的都是 use 直接提取出来即可
    slice_t *operands = extract_operands(operand, FLAG(LIR_OPERAND_VAR) | FLAG(LIR_OPERAND_REG));
    for (int i = 0; i < operands->count; ++i) {
        lir_operand_t *o = operands->take[i];
        set_operand_flag(o, false); // 符合嵌入的全部定义成 USE
    }
}

static slice_t *op_extract_operands(lir_op_t *op, uint64_t operand_flag) {
    slice_t *result = extract_operands(op->output, operand_flag);
    slice_append(result, extract_operands(op->first, operand_flag));
    slice_append(result, extract_operands(op->second, operand_flag));

    return result;
}

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
        new_var->type = var->type;
        new_var->type_base = var->type_base;
        new_var->flag = 0; // 即使是同一个 var 在不同的位置承担的 flag 也是不同的
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
    } else if (operand->type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        return operand;
    }

    error_exit("[set_indirect_addr] operand_type != LIR_OPERAND_VAR or LIR_OPERAND_ADDR, actual %d",
               operand->type);
    return NULL;
}

lir_operand_t *lir_new_addr_operand(lir_operand_t *base, int offset, type_base_t type_base) {
    lir_indirect_addr_t *addr_operand = malloc(sizeof(lir_indirect_addr_t));
    addr_operand->base = base;
    addr_operand->offset = offset;
    addr_operand->type_base = type_base;

    lir_operand_t *operand = NEW(lir_operand_t);
    operand->type = LIR_OPERAND_INDIRECT_ADDR;
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
lir_operand_t *lir_temp_var_operand(closure_t *c, type_t type) {
    string unique_ident = analysis_unique_ident(c->module, TEMP_IDENT);

    symbol_table_set_var(unique_ident, type);

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
        var->flag |= FLAG(VR_FLAG_FIRST);
    }

    if (op->second && op->second->type == LIR_OPERAND_VAR) {
        lir_var_t *var = op->second->value;
        var->flag |= FLAG(VR_FLAG_SECOND);
    }

    if (op->output && op->output->type == LIR_OPERAND_VAR) {
        lir_var_t *var = op->output->value;
        var->flag |= FLAG(VR_FLAG_OUTPUT);
    }

    set_operand_flag(op->first, false);
    set_operand_flag(op->second, false);
    set_operand_flag(op->output, true);

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

//    new->var_decl_table = table_new();
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
    basic_block->live = slice_new();
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

    ast_var_decl *global_var = symbol_table_get_var(ident);
    var->type = global_var->type;
    var->type_base = global_var->type.base;
    var->flag |= type_base_trans_alloc(global_var->type.base);

    return var;
}

lir_operand_t *lir_new_target_operand() {
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

    if (operand->type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
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

/**
 * 已经经过了 ssa 的处理，才 first op 需要排除 label 和 phi
 * @param block
 */
void lir_set_quick_op(basic_block_t *block) {
    list_node *current = list_first(block->operations)->succ;
    while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
        current = current->succ;
    }
    assert(current);
    // current code not opcode phi
    block->first_op = current;
    block->last_op = list_last(block->operations);
}

slice_t *lir_op_operands(lir_op_t *op, flag_t operand_flag, flag_t vr_flag, bool extract_value) {
    slice_t *temps = op_extract_operands(op, operand_flag);
    slice_t *results = slice_new();
    for (int i = 0; i < temps->count; ++i) {
        lir_operand_t *operand = temps->take[i];
        assertf(FLAG(operand->type) & operand_flag, "operand type is not and operand flag");

        // 只有 var 或者 reg 现需要进行 vr 校验
        if (operand->type == LIR_OPERAND_VAR) {
            lir_var_t *var = operand->value;
            // def or use
            if (!(var->flag & vr_flag)) {
                continue;
            }
        } else if (operand->type == LIR_OPERAND_REG) {
            reg_t *reg = operand->value;
            if (!(reg->flag & vr_flag)) {
                continue;
            }
        }

        if (extract_value) {
            slice_push(results, operand->value);
        } else {
            slice_push(results, operand);
        }
    }

    return results;
}


/**
 * @param op
 * @param vr_flag  use or def
 * @return
 */
slice_t *lir_var_operands(lir_op_t *op, flag_t vr_flag) {
    return lir_op_operands(op, FLAG(LIR_OPERAND_VAR), vr_flag, true);
}
