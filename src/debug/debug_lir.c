#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "debug_lir.h"
#include "src/symbol.h"
#include "src/lib/helper.h"
#include "src/semantic/analysis.h"

static char *lir_operand_symbol_to_string(lir_operand_symbol *ptr) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    int len = sprintf(buf, "SYMBOL[%s]", ptr->ident);
    realloc(buf, len);

    return buf;
}

string lir_operand_to_string(lir_operand *operand) {
    if (operand == NULL) {
        return "_";
    }

    switch (operand->type) {
        case LIR_OPERAND_TYPE_LABEL_SYMBOL: {
            return lir_operand_label_to_string((lir_operand_label_symbol *) operand->value);
        }
        case LIR_OPERAND_TYPE_SYMBOL: {
            return lir_operand_symbol_to_string((lir_operand_symbol *) operand->value);
        }
        case LIR_OPERAND_TYPE_VAR: {
            return lir_operand_var_to_string((lir_operand_var *) operand->value);
        }
        case LIR_OPERAND_TYPE_IMMEDIATE: {
            return lir_operand_imm_to_string((lir_operand_immediate *) operand->value);
        }
        case LIR_OPERAND_TYPE_MEMORY: {
            return lir_operand_memory_to_string((lir_operand_memory *) operand->value);
        }
        case LIR_OPERAND_TYPE_ACTUAL_PARAM: {
            return lir_operand_actual_param_to_string((lir_operand_actual_param *) operand->value);
        }
        case LIR_OPERAND_TYPE_PHI_BODY: {
            return lir_operand_phi_body_to_string((lir_operand_phi_body *) operand->value);
        }
        default: {
            return "UNKNOWN";
        }
    }

}

string lir_operand_label_to_string(lir_operand_label_symbol *label) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string scope = "G";
    if (label->is_local) {
        scope = "L";
    }

    sprintf(buf, "LABEL[%s|%s]", label->ident, scope);
    return buf;
}

/**
 * @param var
 * @return
 */
char *lir_operand_var_to_string(lir_operand_var *var) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
//    int stack_frame_offset = 0;
    char *type_string = "";
    int len;

//    stack_frame_offset = *var->local->stack_frame_offset;
    type_string = type_to_string[var->decl->ast_type.base];
    for (int i = 0; i < var->decl->ast_type.point; ++i) {
        type_string = str_connect(type_string, "*");
    }

    string ident = var->ident;
    if (var->indirect_addr) {
        ident = str_connect("*", ident);
    }
    len = sprintf(buf, "VAR[%s|%s]", ident, type_string);

    realloc(buf, len);
    return buf;
}

char *lir_operand_imm_to_string(lir_operand_immediate *immediate) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    int len;
    switch (immediate->type) {
        case TYPE_BOOL: {
            string bool_str = "true";
            if (immediate->bool_value == false) {
                bool_str = "false";
            }
            len = sprintf(buf, "IMM[%s|BOOL]", bool_str);
            break;
        }
        case TYPE_INT: {
            len = sprintf(buf, "IMM[%ld:INT]", immediate->int_value);
            break;
        }
        case TYPE_INT64: {
            len = sprintf(buf, "IMM[%ld:INT64]", immediate->int_value);
            break;
        }
        case TYPE_FLOAT: {
            len = sprintf(buf, "IMM[%f:FLOAT]", immediate->float_value);
            break;
        }
        case TYPE_STRING_RAW: {
            len = sprintf(buf, "IMM[%s:STRING_RAW]", immediate->string_value);
            break;
        }
        default:
            return "UNKNOWN IMM";
    }

    return buf;
}

char *lir_operand_memory_to_string(lir_operand_memory *operand_memory) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    sprintf(buf, "MEM[%s:%zu,%zu]",
            lir_operand_to_string(operand_memory->base),
            operand_memory->offset,
            operand_memory->length);
    return buf;
}

char *lir_operand_actual_param_to_string(lir_operand_actual_param *actual_param) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT);
    for (int i = 0; i < actual_param->count; ++i) {
        string src = lir_operand_to_string(actual_param->list[i]);
        strcat(params, src);
        free(src);

        if (i < actual_param->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "PARAMS(%s)", params);
    free(params);

    return buf;
}

char *lir_operand_phi_body_to_string(lir_operand_phi_body *phi_body) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT * (phi_body->count + 1));
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT * phi_body->count);
    for (int i = 0; i < phi_body->count; ++i) {
        string src = lir_operand_var_to_string(phi_body->list[i]);
        strcat(params, src);

        if (i < phi_body->count - 1) {
            strcat(params, ",");
        }
    }

    sprintf(buf, "BODY(%s)", params);
    free(params);

    return buf;
}

