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

int ast_struct_decl_size(typedecl_struct_t *struct_decl) {
    int size = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        typedecl_struct_property_t property = struct_decl->properties[i];
        size += type_kind_sizeof(property.type.kind);
    }
    return size;
}

/**
 * 默认 struct_decl 已经排序过了
 * @param struct_decl
 * @param property
 * @return
 */
int ast_struct_offset(typedecl_struct_t *struct_decl, char *property) {
    int offset = 0;
    for (int i = 0; i < struct_decl->count; ++i) {
        typedecl_struct_property_t item = struct_decl->properties[i];
        if (str_equal(item.key, property)) {
            break;
        }
        offset += type_kind_sizeof(item.type.kind);
    }
    return offset;
}


typedecl_t select_actual_param(ast_call *call, uint8_t index) {
    if (call->spread_param && index >= call->actual_param_count) {
        // last actual param type must array
        typedecl_t last_param_type = call->actual_params[call->actual_param_count].type;
        assertf(last_param_type.kind == TYPE_LIST, "spread param must list");
        typedecl_list_t *list_decl = last_param_type.list_decl;
        return list_decl->element_type;
    }

    return call->actual_params[index].type;
}

typedecl_t select_formal_param(typedecl_fn_t *formal_fn, uint8_t index) {
    if (formal_fn->rest_param && index >= formal_fn->formals_count - 1) {
        typedecl_t last_param_type = formal_fn->formals_types[formal_fn->formals_count - 1];
        assertf(last_param_type.kind == TYPE_LIST, "rest param must list");
        typedecl_list_t *list_decl = last_param_type.list_decl;

        return list_decl->element_type;
    }

    assertf(index < formal_fn->formals_count, "select index out range");
    return formal_fn->formals_types[index];
}



/**
 * 比较前都已经还原为原始类型了
 * @param target
 * @param source
 * @return
 */
bool type_compare(typedecl_t target, typedecl_t source) {
    assertf(target.is_origin && source.is_origin, "code not origin, left: '%s', right: '%s'",
            type_to_string[target.kind],
            type_to_string[source.kind]);
    assertf(target.kind != TYPE_UNKNOWN && source.kind != TYPE_UNKNOWN, "type cnnot infer");

    if (target.kind == TYPE_ANY || source.kind == TYPE_ANY) {
        return true;
    }

    if (target.kind != source.kind) {
        return false;
    }

    if (target.kind == TYPE_MAP) {
        typedecl_map_t *left_map_decl = target.map_decl;
        typedecl_map_t *right_map_decl = source.map_decl;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (target.kind == TYPE_LIST) {
        typedecl_list_t *left_list_decl = target.list_decl;
        struct typedecl_list_t *right_list_decl = source.list_decl;
        if (right_list_decl->element_type.kind == TYPE_UNKNOWN) {
            // 但是这样在 compiler_array 时将完全不知道将右值初始化多大空间的 capacity
            // 但是其可以完全继承左值, 左值进入到该方法之前已经经过了类型推断，这里肯定不是 var 了
            right_list_decl->element_type = left_list_decl->element_type;
            return true;
        }
        // 类型不相同
        if (!type_compare(left_list_decl->element_type, right_list_decl->element_type)) {
            return false;
        }

        return true;
    }

    if (target.kind == TYPE_FN) {
        typedecl_fn_t *left_type_fn = target.fn_decl;
        typedecl_fn_t *right_type_fn = source.fn_decl;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        if (left_type_fn->formals_count != right_type_fn->formals_count) {
            return false;
        }

        for (int i = 0; i < left_type_fn->formals_count; ++i) {
            if (!type_compare(
                    left_type_fn->formals_types[i],
                    right_type_fn->formals_types[i]
            )) {
                return false;
            }
        }
    }

    if (target.kind == TYPE_STRUCT) {
        typedecl_struct_t *left_struct_decl = target.struct_decl;
        typedecl_struct_t *right_struct_decl = source.struct_decl;
        if (left_struct_decl->count != right_struct_decl->count) {
            return false;
        }

        for (int i = 0; i < left_struct_decl->count; ++i) {
            // key 比较
            if (strcmp(
                    left_struct_decl->properties[i].key,
                    right_struct_decl->properties[i].key) != 0) {
                return false;
            }

            // code 比较
            if (!type_compare(
                    left_struct_decl->properties[i].type,
                    right_struct_decl->properties[i].type
            )) {
                return false;
            }
        }
    }

    return true;
}
