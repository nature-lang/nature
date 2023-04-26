#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "lir.h"

// STACK[12]
static char *lir_operand_stack_to_string(lir_stack_t *stack) {
    char *str = (char *) malloc(sizeof(char) * 100);
    sprintf(str, "STACK[%d|%d]", stack->slot, stack->size);
    return str;
}

// REG[rax]
static char *lir_operand_reg_to_string(reg_t *reg) {
    char *str = (char *) malloc(sizeof(char) * 100);
    sprintf(str, "REG[%s]", reg->name);
    return str;
}

static char *lir_operand_symbol_to_string(lir_symbol_var_t *ptr) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    int len = sprintf(buf, "SYMBOL[%s]", ptr->ident);
    return realloc(buf, len + 1);
}

string lir_operand_to_string(lir_operand_t *operand) {
    if (operand == NULL) {
        return "_";
    }

    switch (operand->assert_type) {
        case LIR_OPERAND_SYMBOL_LABEL: {
            return lir_label_to_string((lir_symbol_label_t *) operand->value);
        }
        case LIR_OPERAND_SYMBOL_VAR: { // 外部符号引用
            return lir_operand_symbol_to_string((lir_symbol_var_t *) operand->value);
        }
        case LIR_OPERAND_STACK: {
            return lir_operand_stack_to_string((lir_stack_t *) operand->value);
        }
        case LIR_OPERAND_REG: {
            return lir_operand_reg_to_string((reg_t *) operand->value);
        }
        case LIR_OPERAND_VAR: {
            return lir_var_to_string((lir_var_t *) operand->value);
        }
        case LIR_OPERAND_IMM: {
            return lir_imm_to_string((lir_imm_t *) operand->value);
        }
        case LIR_OPERAND_INDIRECT_ADDR: {
            return lir_addr_to_string((lir_indirect_addr_t *) operand->value);
        }
        case LIR_OPERAND_ACTUAL_PARAMS: {
            return lir_actual_param_to_string((slice_t *) operand->value);
        }
        case LIR_OPERAND_FORMAL_PARAMS: {
            return lir_formal_param_to_string((slice_t *) operand->value);
        }
        case LIR_OPERAND_PHI_BODY: {
            return lir_phi_body_to_string(operand->value);
        }
        default: {
            assertf(0, "unknown operand type: %d", operand->assert_type);
        }
    }

}

string lir_label_to_string(lir_symbol_label_t *label) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string scope = "G";
    if (label->is_local) {
        scope = "L";
    }

    sprintf(buf, "SYMBOL[%s|%s]", label->ident, scope);
    return buf;
}

/**
 * @param var
 * @return
 */
char *lir_var_to_string(lir_var_t *var) {
//    int stack_frame_offset = 0;
    char *type_string = "";
    int len;

//    stack_frame_offset = *var->local->stack_frame_offset;
    if (var->type.kind > 0) {
        type_string = type_kind_string[var->type.kind];
//        for (int i = 0; i < var->type.pointer; ++i) {
//            type_string = str_connect(type_string, "*");
//        }
    }

    string ident = var->ident;
    string indirect_addr = "";
//    if (var->indirect_addr) {
//        indirect_addr = "*";
//    }
    return dsprintf("VAR[%s|%s]", ident, type_string);
}

char *lir_imm_to_string(lir_imm_t *immediate) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    int len;
    switch (immediate->kind) {
        case TYPE_BOOL: {
            string bool_str = "true";
            if (immediate->bool_value == false) {
                bool_str = "false";
            }
            len = sprintf(buf, "IMM[%s|BOOL]", bool_str);
            break;
        }
        case TYPE_INT: {
            len = sprintf(buf, "IMM[%ld:INT]", immediate->uint_value);
            break;
        }
        case TYPE_INT8: {
            len = sprintf(buf, "IMM[%ld:INT8]", immediate->uint_value);
            break;
        }
        case TYPE_INT16: {
            len = sprintf(buf, "IMM[%ld:INT16]", immediate->uint_value);
            break;
        }
        case TYPE_INT32: {
            len = sprintf(buf, "IMM[%ld:INT32]", immediate->uint_value);
            break;
        }
        case TYPE_UINT: {
            len = sprintf(buf, "IMM[%ld:UINT]", immediate->uint_value);
            break;
        }
        case TYPE_UINT8: {
            len = sprintf(buf, "IMM[%ld:UINT8]", immediate->uint_value);
            break;
        }
        case TYPE_UINT16: {
            len = sprintf(buf, "IMM[%ld:UINT16]", immediate->uint_value);
            break;
        }
        case TYPE_UINT32: {
            len = sprintf(buf, "IMM[%ld:UINT32]", immediate->uint_value);
            break;
        }
        case TYPE_UINT64: {
            len = sprintf(buf, "IMM[%ld:UINT64]", immediate->uint_value);
            break;
        }
        case TYPE_FLOAT: {
            len = sprintf(buf, "IMM[%f:FLOAT]", immediate->float_value);
            break;
        }
        case TYPE_FLOAT32: {
            len = sprintf(buf, "IMM[%f:FLOAT32]", immediate->float_value);
            break;
        }
        case TYPE_FLOAT64: {
            len = sprintf(buf, "IMM[%f:FLOAT64]", immediate->float_value);
            break;
        }
        case TYPE_RAW_STRING: {
            len = sprintf(buf, "IMM[%s:RAW_STRING]", immediate->string_value);
            break;
        }
        default:
            return "UNKNOWN IMM";
    }

    return buf;
}

char *lir_addr_to_string(lir_indirect_addr_t *operand_addr) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string indirect_addr_str = "";
    string type_string = type_kind_string[operand_addr->type.kind];
    sprintf(buf, "%sI_ADDR[%s:%lu:%s]",
            indirect_addr_str,
            lir_operand_to_string(operand_addr->base),
            operand_addr->offset,
            type_string);
    return buf;
}

char *lir_formal_param_to_string(slice_t *formal_params) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT);
    for (int i = 0; i < formal_params->count; ++i) {
        string src = lir_var_to_string(formal_params->take[i]);
        strcat(params, src);
        free(src);
        if (i < formal_params->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "FORMALS(%s)", params);
    free(params);

    return buf;
}


char *lir_actual_param_to_string(slice_t *actual_params) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT);
    for (int i = 0; i < actual_params->count; ++i) {
        string src = lir_operand_to_string(actual_params->take[i]);
        strcat(params, src);
        free(src);

        if (i < actual_params->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "PARAMS(%s)", params);
    free(params);

    return buf;
}

char *lir_phi_body_to_string(slice_t *phi_body) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT * (phi_body->count + 1));
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT * phi_body->count);
    for (int i = 0; i < phi_body->count; ++i) {
        string src = lir_var_to_string(phi_body->take[i]);
        strcat(params, src);

        if (i < phi_body->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "BODY(%s)", params);
    free(params);

    return buf;
}

closure_t *lir_closure_new(ast_fndef_t *fndef) {
    closure_t *c = NEW(closure_t);
    c->name = fndef->name;
    c->operations = linked_new();
    c->text_count = 0;
    c->asm_operations = slice_new();
    c->asm_build_temps = slice_new();
    c->asm_symbols = slice_new();
    c->entry = NULL;
    c->globals = slice_new();
    c->blocks = slice_new(); // basic_block_t

    c->interval_table = table_new();

//    c->var_decl_table = table_new();
    c->stack_offset = 0;
    c->stack_vars = slice_new();
    c->loop_count = 0;
    c->loop_ends = slice_new();
    c->loop_headers = slice_new();

    c->interval_count = cross_alloc_reg_count() + 1;
    fndef->closure = c;
    return c;
}