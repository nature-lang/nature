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



/**
 * 比较前都已经还原为原始类型了
 * @param target
 * @param source
 * @return
 */
bool type_compare(type_t target, type_t source) {
    assertf(target.is_origin && source.is_origin, "code not origin, left: '%s', right: '%s'",
            type_to_string[target.base],
            type_to_string[source.base]);
    assertf(target.base != TYPE_UNKNOWN && source.base != TYPE_UNKNOWN, "type cnnot infer");

    if (target.base == TYPE_ANY || source.base == TYPE_ANY) {
        return true;
    }

    if (target.base != source.base) {
        return false;
    }

    if (target.base == TYPE_MAP) {
        ast_map_decl *left_map_decl = target.value;
        ast_map_decl *right_map_decl = source.value;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (target.base == TYPE_ARRAY) {
        ast_array_decl *left_list_decl = target.value;
        ast_array_decl *right_list_decl = source.value;
        if (right_list_decl->type.base == TYPE_UNKNOWN) {
            // 但是这样在 compiler_array 时将完全不知道将右值初始化多大空间的 capacity
            // 但是其可以完全继承左值, 左值进入到该方法之前已经经过了类型推断，这里肯定不是 var 了
            right_list_decl->type = left_list_decl->type;
            right_list_decl->count = left_list_decl->count;
            return true;
        }
        // 类型不相同
        if (!type_compare(left_list_decl->type, right_list_decl->type)) {
            return false;
        }

        if (left_list_decl->count == 0) {
            left_list_decl->count = right_list_decl->count;
        }

        if (left_list_decl->count < right_list_decl->count) {
            return false;
        }

        right_list_decl->count = left_list_decl->count;
        return true;
    }

    if (target.base == TYPE_FN) {
        type_fn_t *left_type_fn = target.value;
        type_fn_t *right_type_fn = source.value;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        if (left_type_fn->formal_param_count != right_type_fn->formal_param_count) {
            return false;
        }

        for (int i = 0; i < left_type_fn->formal_param_count; ++i) {
            if (!type_compare(
                    left_type_fn->formal_param_types[i],
                    right_type_fn->formal_param_types[i]
            )) {
                return false;
            }
        }
    }

    if (target.base == TYPE_STRUCT) {
        ast_struct_decl *left_struct_decl = target.value;
        ast_struct_decl *right_struct_decl = source.value;
        if (left_struct_decl->count != right_struct_decl->count) {
            return false;
        }

        for (int i = 0; i < left_struct_decl->count; ++i) {
            // key 比较
            if (strcmp(
                    left_struct_decl->list[i].key,
                    right_struct_decl->list[i].key) != 0) {
                return false;
            }

            // code 比较
            if (!type_compare(
                    left_struct_decl->list[i].type,
                    right_struct_decl->list[i].type
            )) {
                return false;
            }
        }
    }

    return true;
}
