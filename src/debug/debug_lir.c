#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "debug_lir.h"

// STACK[12]
static char *lir_operand_stack_to_string(lir_stack_t *stack) {
    char *str = (char *) mallocz(100);
    sprintf(str, "STACK[%ld|%ld]", stack->slot, stack->size);
    return str;
}

// REG[rax]
static char *lir_operand_reg_to_string(reg_t *reg) {
    char *str = (char *) mallocz(100);
    sprintf(str, "REG[%s]", reg->name);
    return str;
}

static char *lir_operand_symbol_to_string(lir_symbol_var_t *ptr) {
    string buf = mallocz(DEBUG_STR_COUNT);
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
            return lir_arg_to_string((slice_t *) operand->value);
        }
        case LIR_OPERAND_CLOSURE_VARS:
        case LIR_OPERAND_VARS:
        case LIR_OPERAND_FORMAL_PARAMS:
        case LIR_OPERAND_PHI_BODY: {
            return lir_vars_to_string(operand->value);
        }
        case LIR_OPERAND_REGS: {
            return lir_regs_to_string(operand->value);
        }
        default: {
            assertf(0, "unknown operand type: %d", operand->assert_type);
        }
    }

    exit(1);
}

string lir_label_to_string(lir_symbol_label_t *label) {
    string buf = mallocz(DEBUG_STR_COUNT);
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
    char *type_string = "";

    if (var->type.kind > 0) {
        type_string = type_kind_str[var->type.kind];
    }

    string ident = var->ident;
    return dsprintf("VAR[%s|%s]", ident, type_string);
}

char *lir_imm_to_string(lir_imm_t *immediate) {
    string buf = mallocz(DEBUG_STR_COUNT);
    int len;
    if (immediate->kind == TYPE_BOOL) {
        string bool_str = "true";
        if (immediate->bool_value == false) {
            bool_str = "false";
        }
        len = sprintf(buf, "IMM[%s|BOOL]", bool_str);
    } else if (is_integer(immediate->kind)) {
        len = sprintf(buf, "IMM[%ld:INT]", immediate->uint_value);
    } else if (is_float(immediate->kind)) {
        len = sprintf(buf, "IMM[%f:FLOAT]", immediate->f64_value);
    } else if (immediate->kind == TYPE_RAW_STRING) {
        len = sprintf(buf, "IMM[%s:RAW_STRING]", immediate->string_value);
    } else {
        assertf(0, "unknown immediate type: %d", immediate->kind);
    }

    return buf;
}

char *lir_addr_to_string(lir_indirect_addr_t *operand_addr) {
    string buf = mallocz(DEBUG_STR_COUNT);
    string indirect_addr_str = "";
    string type_string = type_kind_str[operand_addr->type.kind];
    sprintf(buf, "%sI_ADDR[%s:%lu:%s]",
            indirect_addr_str,
            lir_operand_to_string(operand_addr->base),
            operand_addr->offset,
            type_string);
    return buf;
}

char *lir_formal_to_string(slice_t *formals) {
    string buf = mallocz(DEBUG_STR_COUNT);
    string params = mallocz(DEBUG_STR_COUNT);
    for (int i = 0; i < formals->count; ++i) {
        string src = lir_var_to_string(formals->take[i]);
        strcat(params, src);
        free(src);
        if (i < formals->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "FORMALS(%s)", params);
    free(params);

    return buf;
}


char *lir_arg_to_string(slice_t *args) {
    string buf = mallocz(DEBUG_STR_COUNT);
    string params = mallocz(DEBUG_STR_COUNT);
    for (int i = 0; i < args->count; ++i) {
        string src = lir_operand_to_string(args->take[i]);
        strcat(params, src);
        free(src);

        if (i < args->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "PARAMS(%s)", params);
    free(params);

    return buf;
}

char *lir_vars_to_string(slice_t *vars) {
    if (vars->count == 0) {
        return "VARS()";
    }

    string buf = mallocz(DEBUG_STR_COUNT * (vars->count + 1));
    string params = mallocz(DEBUG_STR_COUNT * vars->count);
    for (int i = 0; i < vars->count; ++i) {
        string src = lir_var_to_string(vars->take[i]);
        strcat(params, src);

        if (i < vars->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "VARS(%s)", params);
    free(params);

    return buf;
}

char *lir_regs_to_string(slice_t *regs) {
    if (regs->count == 0) {
        return "REGS()";
    }

    string buf = mallocz(DEBUG_STR_COUNT * (regs->count + 1));
    string params = mallocz(DEBUG_STR_COUNT * regs->count);
    for (int i = 0; i < regs->count; ++i) {
        string src = lir_operand_reg_to_string(regs->take[i]);
        strcat(params, src);

        if (i < regs->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "REGS(%s)", params);
    free(params);

    return buf;
}
