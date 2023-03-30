#include "ast.h"
#include "utils/helper.h"

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = malloc(sizeof(ast_ident));
    ident->literal = literal;
    return ident;
}

typedecl_t select_actual_param(ast_call *call, uint8_t index) {
    if (call->spread_param && index >= call->actual_params->length - 1) {
        // last actual param type must array
        ast_expr *last_param_expr = ct_list_value(call->actual_params, call->actual_params->length - 1);
        typedecl_t last_param_type = last_param_expr->type;
        assertf(last_param_type.kind == TYPE_LIST, "spread param must list");
        typedecl_list_t *list_decl = last_param_type.list_decl;
        return list_decl->element_type;
    }

    ast_expr *last_param_expr = ct_list_value(call->actual_params, index);

    return last_param_expr->type;
}

typedecl_t select_formal_param(typedecl_fn_t *formal_fn, uint8_t index) {
    if (formal_fn->rest_param && index >= formal_fn->formal_types->length - 1) {

        typedecl_t *last_param_type = ct_list_value(formal_fn->formal_types, formal_fn->formal_types->length - 1);
        assertf(last_param_type->kind == TYPE_LIST, "rest param must list");
        typedecl_list_t *list_decl = last_param_type->list_decl;

        return list_decl->element_type;
    }

    assertf(index < formal_fn->formal_types->length, "select index out range");
    typedecl_t *result = ct_list_value(formal_fn->formal_types, index);
    return *result;
}



/**
 * 比较前都已经还原为原始类型了
 * @param left
 * @param right
 * @return
 */
bool type_compare(typedecl_t left, typedecl_t right) {
    assertf(left.is_origin && right.is_origin, "type not origin, left: '%s', right: '%s'",
            type_kind_string[left.kind],
            type_kind_string[right.kind]);

    assertf(left.kind != TYPE_UNKNOWN && right.kind != TYPE_UNKNOWN, "type cannot infer");

    if (left.kind == TYPE_ANY || right.kind == TYPE_ANY) {
        return true;
    }

    if (left.kind != right.kind) {
        return false;
    }

    if (left.kind == TYPE_MAP) {
        typedecl_map_t *left_map_decl = left.map_decl;
        typedecl_map_t *right_map_decl = right.map_decl;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (left.kind == TYPE_SET) {
        typedecl_set_t *left_decl = left.set_decl;
        typedecl_set_t *right_decl = right.set_decl;

        if (!type_compare(left_decl->key_type, right_decl->key_type)) {
            return false;
        }
    }

    if (left.kind == TYPE_LIST) {
        typedecl_list_t *left_list_decl = left.list_decl;
        struct typedecl_list_t *right_list_decl = right.list_decl;
        if (right_list_decl->element_type.kind == TYPE_UNKNOWN) {
            // 但是这样在 compiler_array 时将完全不知道将右值初始化多大空间的 capacity
            // 但是其可以完全继承左值, 左值进入到该方法之前已经经过了类型推断，这里肯定不是 var 了
            right_list_decl->element_type = left_list_decl->element_type;
            return true;
        }
        return type_compare(left_list_decl->element_type, right_list_decl->element_type);
    }

    if (left.kind == TYPE_TUPLE) {
        typedecl_tuple_t *left_tuple = left.tuple_decl;
        typedecl_tuple_t *right_tuple = right.tuple_decl;

        if (left_tuple->elements->length != right_tuple->elements->length) {
            return false;
        }
        for (int i = 0; i < left_tuple->elements->length; ++i) {
            typedecl_t *left_item = ct_list_value(left_tuple->elements, i);
            typedecl_t *right_item = ct_list_value(right_tuple->elements, i);
            if (!type_compare(*left_item, *right_item)) {
                return false;
            }
        }
    }


    if (left.kind == TYPE_FN) {
        typedecl_fn_t *left_type_fn = left.fn_decl;
        typedecl_fn_t *right_type_fn = right.fn_decl;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        if (left_type_fn->formal_types->length != right_type_fn->formal_types->length) {
            return false;
        }

        for (int i = 0; i < left_type_fn->formal_types->length; ++i) {
            typedecl_t *left_formal_type = ct_list_value(left_type_fn->formal_types, i);
            typedecl_t *right_formal_type = ct_list_value(right_type_fn->formal_types, i);
            if (!type_compare(*left_formal_type, *right_formal_type)) {
                return false;
            }
        }
    }

    if (left.kind == TYPE_STRUCT) {
        typedecl_struct_t *left_struct_decl = left.struct_decl;
        typedecl_struct_t *right_struct_decl = right.struct_decl;
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

            // type 比较
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

bool can_assign(ast_type_e t) {
    if (t == AST_EXPR_IDENT ||
        t == AST_EXPR_ACCESS ||
        t == AST_EXPR_STRUCT_ACCESS) {
        return true;
    }
    return false;
}
