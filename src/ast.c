#include "ast.h"
#include "utils/helper.h"

string ast_expr_operator_to_string[100] = {
        [AST_EXPR_OPERATOR_ADD] = "+",
        [AST_EXPR_OPERATOR_SUB] = "-",
        [AST_EXPR_OPERATOR_MUL] = "*",
        [AST_EXPR_OPERATOR_DIV] = "/",

        [AST_EXPR_OPERATOR_LT] = "<",
        [AST_EXPR_OPERATOR_LTE] = "<=",
        [AST_EXPR_OPERATOR_GT] = ">", // >
        [AST_EXPR_OPERATOR_GTE] = ">=",  // >=
        [AST_EXPR_OPERATOR_EQ_EQ] = "==", // ==
        [AST_EXPR_OPERATOR_NOT_EQ] = "!=", // !=

        [AST_EXPR_OPERATOR_NOT] = "!", // unary !expr
        [AST_EXPR_OPERATOR_NEG] = "-", // unary -expr
};

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = malloc(sizeof(ast_ident));
    ident->literal = literal;
    return ident;
}

int ast_struct_decl_size(ast_struct_decl *struct_decl) {
    int size = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        ast_struct_property property = struct_decl->list[i];
        size += type_base_sizeof(property.type.base);
    }
    return size;
}

/**
 * 默认 struct_decl 已经排序过了
 * @param struct_decl
 * @param property
 * @return
 */
int ast_struct_offset(ast_struct_decl *struct_decl, char *property) {
    int offset = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        ast_struct_property item = struct_decl->list[i];
        if (str_equal(item.key, property)) {
            break;
        }
        offset += type_base_sizeof(item.type.base);
    }
    return offset;
}


type_t select_actual_param(ast_call *call, uint8_t index) {
    if (call->spread_param && index >= call->actual_param_count) {
        // last actual param type must array
        type_t last_param_type = call->actual_params[call->actual_param_count].type;
        assertf(last_param_type.base == TYPE_ARRAY, "spread_param must array");
        ast_array_decl *array_decl = last_param_type.value;
        return array_decl->type;
    }

    return call->actual_params[index].type;
}

type_t select_formal_param(type_fn_t *formal_fn, uint8_t index) {
    if (formal_fn->rest_param && index >= formal_fn->formal_param_count - 1) {
        type_t last_param_type = formal_fn->formal_param_types[formal_fn->formal_param_count - 1];
        assertf(last_param_type.base == TYPE_ARRAY, "rest param must array");
        ast_array_decl *array_decl = last_param_type.value;

        return array_decl->type;
    }

    assertf(index < formal_fn->formal_param_count, "select index out range");
    return formal_fn->formal_param_types[index];
}
