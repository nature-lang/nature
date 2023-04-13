#include "ast.h"
#include "utils/helper.h"

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = malloc(sizeof(ast_ident));
    ident->literal = literal;
    return ident;
}

/**
 * 外部已经进行过类型还原了，这里不需要再类型还原
 * @param call
 * @param index
 * @return
 */
typeuse_t select_actual_param(ast_call *call, uint8_t index) {
//    if (call->spread && index >= call->actual_params->length - 1) {
//        // last actual param type must array
//        ast_expr *last_param_expr = ct_list_value(call->actual_params, call->actual_params->length - 1);
//        typeuse_t last_param_type = last_param_expr->type;
//        assertf(last_param_type.kind == TYPE_LIST, "spread param must list");
//        type_list_t *list_decl = last_param_type.list;
//        return list_decl->element_type;
//    }

    ast_expr *last_param_expr = ct_list_value(call->actual_params, index);
    return last_param_expr->type;
}

typeuse_t select_formal_param(type_fn_t *formal_fn, uint8_t index) {
    if (formal_fn->rest && index >= formal_fn->formal_types->length - 1) {

        typeuse_t *last_param_type = ct_list_value(formal_fn->formal_types, formal_fn->formal_types->length - 1);
        assertf(last_param_type->kind == TYPE_LIST, "rest param must list");
        type_list_t *list_decl = last_param_type->list;

        return list_decl->element_type;
    }

    assertf(index < formal_fn->formal_types->length, "select index out range");
    typeuse_t *result = ct_list_value(formal_fn->formal_types, index);
    return *result;
}



/**
 * 比较前都已经还原为原始类型了
 * @param left
 * @param right
 * @return
 */
bool type_compare(typeuse_t left, typeuse_t right) {
    assertf(left.status == REDUCTION_STATUS_DONE && right.status == REDUCTION_STATUS_DONE,
            "type not origin, left: '%s', right: '%s'",
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
        type_map_t *left_map_decl = left.map;
        type_map_t *right_map_decl = right.map;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (left.kind == TYPE_SET) {
        type_set_t *left_decl = left.set;
        type_set_t *right_decl = right.set;

        if (!type_compare(left_decl->key_type, right_decl->key_type)) {
            return false;
        }
    }

    if (left.kind == TYPE_LIST) {
        type_list_t *left_list_decl = left.list;
        struct type_list_t *right_list_decl = right.list;
        if (right_list_decl->element_type.kind == TYPE_UNKNOWN) {
            // 但是这样在 compiler_array 时将完全不知道将右值初始化多大空间的 capacity
            // 但是其可以完全继承左值, 左值进入到该方法之前已经经过了类型推断，这里肯定不是 var 了
            right_list_decl->element_type = left_list_decl->element_type;
            return true;
        }
        return type_compare(left_list_decl->element_type, right_list_decl->element_type);
    }

    if (left.kind == TYPE_TUPLE) {
        type_tuple_t *left_tuple = left.tuple;
        type_tuple_t *right_tuple = right.tuple;

        if (left_tuple->elements->length != right_tuple->elements->length) {
            return false;
        }
        for (int i = 0; i < left_tuple->elements->length; ++i) {
            typeuse_t *left_item = ct_list_value(left_tuple->elements, i);
            typeuse_t *right_item = ct_list_value(right_tuple->elements, i);
            if (!type_compare(*left_item, *right_item)) {
                return false;
            }
        }
    }


    if (left.kind == TYPE_FN) {
        type_fn_t *left_type_fn = left.fn;
        type_fn_t *right_type_fn = right.fn;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        // TODO rest 支持
        if (left_type_fn->formal_types->length != right_type_fn->formal_types->length) {
            return false;
        }

        for (int i = 0; i < left_type_fn->formal_types->length; ++i) {
            typeuse_t *left_formal_type = ct_list_value(left_type_fn->formal_types, i);
            typeuse_t *right_formal_type = ct_list_value(right_type_fn->formal_types, i);
            if (!type_compare(*left_formal_type, *right_formal_type)) {
                return false;
            }
        }
    }

    if (left.kind == TYPE_STRUCT) {
        type_struct_t *left_struct = left.struct_;
        type_struct_t *right_struct = right.struct_;
        if (left_struct->properties->length != right_struct->properties->length) {
            return false;
        }

        for (int i = 0; i < left_struct->properties->length; ++i) {
            struct_property_t *left_property = ct_list_value(left_struct->properties, i);
            struct_property_t *right_property = ct_list_value(right_struct->properties, i);

            // key 比较
            if (!str_equal(left_property->key, right_property->key)) {
                return false;
            }

            // type 比较
            if (!type_compare(left_property->type, right_property->type)) {
                return false;
            }
        }
    }

    return true;
}
