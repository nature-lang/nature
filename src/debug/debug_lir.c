#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "debug_lir.h"
#include "src/symbol.h"
#include "src/lib/helper.h"
#include "src/semantic/analysis.h"

static char *lir_operand_symbol_to_string(lir_operand_symbol_var *ptr) {
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
        case LIR_OPERAND_TYPE_SYMBOL_LABEL: {
            return lir_operand_label_to_string((lir_operand_symbol_label *) operand->value);
        }
        case LIR_OPERAND_TYPE_SYMBOL_VAR: {
            return lir_operand_symbol_to_string((lir_operand_symbol_var *) operand->value);
        }
        case LIR_OPERAND_TYPE_VAR: {
            return lir_operand_var_to_string((lir_operand_var *) operand->value);
        }
        case LIR_OPERAND_TYPE_IMM: {
            return lir_operand_imm_to_string((lir_operand_immediate *) operand->value);
        }
        case LIR_OPERAND_TYPE_ADDR: {
            return lir_operand_addr_to_string((lir_operand_addr *) operand->value);
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

string lir_operand_label_to_string(lir_operand_symbol_label *label) {
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
    string indirect_addr = "";
    if (var->indirect_addr) {
        indirect_addr = "*";
    }
    len = sprintf(buf, "%sVAR[%s|%s]", indirect_addr, ident, type_string);

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
        case TYPE_INT8: {
            len = sprintf(buf, "IMM[%ld:INT8]", immediate->int_value);
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

char *lir_operand_addr_to_string(lir_operand_addr *operand_addr) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    string indirect_addr_str = "";
    if (operand_addr->indirect_addr) {
        indirect_addr_str = "*";
    }
    string type_string = type_to_string[operand_addr->infer_size_type];
    sprintf(buf, "%sADDR[%s:%zu:%s]",
            indirect_addr_str,
            lir_operand_to_string(operand_addr->base), operand_addr->offset, type_string);
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

