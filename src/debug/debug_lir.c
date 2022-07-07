#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "debug_lir.h"
#include "src/symbol.h"
#include "src/semantic/analysis.h"

string lir_operand_to_string(lir_operand *operand) {
    if (operand == NULL) {
        return "_";
    }

    switch (operand->type) {
        case LIR_OPERAND_TYPE_LABEL: {
            return lir_operand_label_to_string((lir_operand_label *) operand->value);
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

string lir_operand_label_to_string(lir_operand_label *label) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    sprintf(buf, "LABEL[%s]", label->ident);
    return buf;
}

/**
 * @param var
 * @return
 */
char *lir_operand_var_to_string(lir_operand_var *var) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    ast_var_decl *var_decl = SYMBOL_GET_VAR_DECL(var->old);

    sprintf(buf, "VAR[%s:%s]", type_to_string[var_decl->type.category], var->ident);
    return buf;
}

char *lir_operand_imm_to_string(lir_operand_immediate *immediate) {
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT);
    switch (immediate->type) {
        case TYPE_BOOL: {
            string bool_str = "true";
            if (immediate->bool_value == false) {
                bool_str = "false";
            }
            sprintf(buf, "IMM[BOOL:%s]", bool_str);
            break;
        }
        case TYPE_INT: {
            sprintf(buf, "IMM[INT:%ld]", immediate->int_value);
            break;
        }
        case TYPE_FLOAT: {
            sprintf(buf, "IMM[FLOAT:%f]", immediate->float_value);
            break;
        }
        case TYPE_STRING: {
            sprintf(buf, "IMM[STRING:%s]", immediate->string_value);
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
    string buf = malloc(sizeof(char) * DEBUG_STR_COUNT * (actual_param->count + 1));
    string params = malloc(sizeof(char) * DEBUG_STR_COUNT * actual_param->count);
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

