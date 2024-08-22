#include "ast.h"

#include "src/cross.h"
#include "types.h"
#include "utils/helper.h"

static slice_t *ast_body_copy(module_t *m, slice_t *body);

static ast_stmt_t *ast_stmt_copy(module_t *m, ast_stmt_t *temp);

static ast_call_t *ast_call_copy(module_t *m, ast_call_t *temp);

static ast_catch_t *ast_catch_copy(module_t *m, ast_catch_t *temp);

ast_ident *ast_new_ident(char *literal) {
    ast_ident *ident = NEW(ast_ident);
    ident->literal = strdup(literal);
    return ident;
}

static list_t *ct_list_type_copy(list_t *temp_list) {
    if (!temp_list) {
        return NULL;
    }
    list_t *list = ct_list_new(sizeof(type_t));
    for (int i = 0; i < temp_list->length; ++i) {
        type_t *temp = ct_list_value(temp_list, i);
        type_t type = type_copy(*temp);
        ct_list_push(list, &type);
    }

    return list;
}

static type_union_t *type_union_copy(type_union_t *temp) {
    type_union_t *union_ = COPY_NEW(type_union_t, temp);
    union_->elements = ct_list_type_copy(temp->elements);
    union_->any = temp->any;
    return union_;
}

static type_fn_t *type_fn_copy(type_fn_t *temp) {
    type_fn_t *fn = COPY_NEW(type_fn_t, temp);
    if (temp->name) {
        fn->name = strdup(temp->name);
    }

    fn->param_types = ct_list_type_copy(temp->param_types);
    fn->return_type = type_copy(temp->return_type);
    return fn;
}

static type_vec_t *type_vec_copy(type_vec_t *temp) {
    type_vec_t *list = COPY_NEW(type_vec_t, temp);
    list->element_type = type_copy(temp->element_type);
    return list;
}

static type_map_t *type_map_copy(type_map_t *temp) {
    type_map_t *map = COPY_NEW(type_map_t, temp);
    map->key_type = type_copy(map->key_type);
    map->value_type = type_copy(map->value_type);
    return map;
}

static type_tuple_t *type_tuple_copy(type_tuple_t *temp) {
    type_tuple_t *tuple = COPY_NEW(type_tuple_t, temp);
    tuple->elements = ct_list_type_copy(temp->elements);
    return tuple;
}

static type_set_t *type_set_copy(type_set_t *temp) {
    type_set_t *set = COPY_NEW(type_set_t, temp);
    set->element_type = type_copy(temp->element_type);
    return set;
}

static type_struct_t *type_struct_copy(type_struct_t *temp) {
    type_struct_t *struct_ = COPY_NEW(type_struct_t, temp);
    if (temp->ident) {
        struct_->ident = strdup(temp->ident);
    }
    struct_->properties = ct_list_new(sizeof(struct_property_t));
    for (int i = 0; i < temp->properties->length; ++i) {
        struct_property_t *temp_property = ct_list_value(temp->properties, i);
        struct_property_t *property = COPY_NEW(struct_property_t, temp_property);
        property->key = strdup(temp_property->key);
        property->type = type_copy(temp_property->type);

        ct_list_push(struct_->properties, property);
    }
    return struct_;
}

static type_alias_t *type_alias_copy(type_alias_t *temp) {
    type_alias_t *alias = COPY_NEW(type_alias_t, temp);
    alias->ident = strdup(temp->ident);
    alias->args = ct_list_type_copy(temp->args);
    return alias;
}

static type_gen_t *type_gen_copy(type_gen_t *temp) {
    type_gen_t *gen = COPY_NEW(type_gen_t, temp);
    gen->elements = ct_list_type_copy(temp->elements);
    return gen;
}

static type_ptr_t *type_pointer_copy(type_ptr_t *temp) {
    type_ptr_t *pointer = COPY_NEW(type_ptr_t, temp);
    pointer->value_type = type_copy(temp->value_type);
    return pointer;
}

static type_array_t *type_array_copy(type_array_t *temp) {
    type_array_t *array = COPY_NEW(type_array_t, temp);
    return array;
}

type_t type_copy(type_t temp) {
    type_t type = temp;
    if (temp.origin_ident) {
        type.origin_ident = strdup(temp.origin_ident);
        type.origin_type_kind = temp.origin_type_kind;
    }
    if (temp.impl_ident) {
        type.impl_ident = strdup(temp.impl_ident);
    }
    if (temp.impl_args) {
        type.impl_args = ct_list_type_copy(temp.impl_args);
    }

    switch (temp.kind) {
        case TYPE_ALIAS: {
            type.alias = type_alias_copy(temp.alias);
            break;
        }
        case TYPE_VEC: {
            type.vec = type_vec_copy(temp.vec);
            break;
        }
        case TYPE_ARR: {
            type.array = type_array_copy(temp.array);
            break;
        }
        case TYPE_MAP: {
            type.map = type_map_copy(temp.map);
            break;
        }
        case TYPE_SET: {
            type.set = type_set_copy(temp.set);
            break;
        }
        case TYPE_TUPLE: {
            type.tuple = type_tuple_copy(temp.tuple);
            break;
        }
        case TYPE_STRUCT: {
            type.struct_ = type_struct_copy(temp.struct_);
            break;
        }
        case TYPE_FN: {
            type.fn = type_fn_copy(temp.fn);
            break;
        }
        case TYPE_UNION: {
            type.union_ = type_union_copy(temp.union_);
            break;
        }
        case TYPE_RAW_PTR:
        case TYPE_PTR: {
            type.ptr = type_pointer_copy(temp.ptr);
            break;
        }
        default:
            break;
    }
    return type;
}

static list_t *ast_list_expr_copy(module_t *m, list_t *temp) {
    list_t *elements = ct_list_new(sizeof(ast_expr_t));
    for (int i = 0; i < temp->length; ++i) {
        ast_expr_t *expr = ct_list_value(temp, i);
        ct_list_push(elements, ast_expr_copy(m, expr));
    }
    return elements;
}

static ast_ident *ast_ident_copy(module_t *m, ast_ident *temp) {
    ast_ident *ident = COPY_NEW(ast_ident, temp);
    ident->literal = strdup(temp->literal);
    return ident;
}

static ast_literal_t *ast_literal_copy(module_t *m, ast_literal_t *temp) {
    ast_literal_t *literal = COPY_NEW(ast_literal_t, temp);
    literal->value = strdup(temp->value);// 根据实际情况复制，这里假设 value 是字符串
    return literal;
}

static ast_env_access_t *ast_env_access_copy(module_t *m, ast_env_access_t *temp) {
    ast_env_access_t *access = COPY_NEW(ast_env_access_t, temp);
    access->unique_ident = strdup(temp->unique_ident);
    return access;
}

static ast_as_expr_t *ast_as_expr_copy(module_t *m, ast_as_expr_t *temp) {
    ast_as_expr_t *as_expr = COPY_NEW(ast_as_expr_t, temp);
    as_expr->src = *ast_expr_copy(m, &temp->src);
    as_expr->target_type = type_copy(temp->target_type);
    return as_expr;
}

static ast_new_expr_t *ast_new_expr_copy(module_t *m, ast_new_expr_t *temp) {
    ast_new_expr_t *new_expr = COPY_NEW(ast_new_expr_t, temp);
    new_expr->type = type_copy(temp->type);
    return new_expr;
}

static ast_macro_ula_expr_t *ast_ula_expr_copy(module_t *m, ast_macro_ula_expr_t *temp) {
    ast_macro_ula_expr_t *expr = COPY_NEW(ast_macro_ula_expr_t, temp);
    expr->src = *ast_expr_copy(m, &temp->src);
    return expr;
}

static ast_macro_default_expr_t *ast_default_expr_copy(module_t *m, ast_macro_default_expr_t *temp) {
    ast_macro_default_expr_t *expr = COPY_NEW(ast_macro_default_expr_t, temp);
    return expr;
}

static ast_macro_sizeof_expr_t *ast_sizeof_expr_copy(module_t *m, ast_macro_sizeof_expr_t *temp) {
    ast_macro_sizeof_expr_t *sizeof_expr = COPY_NEW(ast_macro_sizeof_expr_t, temp);
    sizeof_expr->target_type = type_copy(temp->target_type);
    return sizeof_expr;
}

static ast_macro_reflect_hash_expr_t *ast_reflect_hash_expr_copy(module_t *m, ast_macro_reflect_hash_expr_t *temp) {
    ast_macro_reflect_hash_expr_t *expr = COPY_NEW(ast_macro_reflect_hash_expr_t, temp);
    expr->target_type = type_copy(temp->target_type);
    return expr;
}

static ast_macro_type_eq_expr_t *ast_type_eq_expr_copy(ast_macro_type_eq_expr_t *temp) {
    ast_macro_type_eq_expr_t *expr = COPY_NEW(ast_macro_type_eq_expr_t, temp);
    expr->left_type = type_copy(temp->left_type);
    expr->right_type = type_copy(temp->right_type);
    return expr;
}

static ast_is_expr_t *ast_is_expr_copy(module_t *m, ast_is_expr_t *temp) {
    ast_is_expr_t *is_expr = COPY_NEW(ast_is_expr_t, temp);
    is_expr->src = *ast_expr_copy(m, &temp->src);
    is_expr->target_type = type_copy(temp->target_type);
    return is_expr;
}

static ast_unary_expr_t *ast_unary_copy(module_t *m, ast_unary_expr_t *temp) {
    ast_unary_expr_t *unary = COPY_NEW(ast_unary_expr_t, temp);
    unary->operand = *ast_expr_copy(m, &temp->operand);
    return unary;
}

static ast_binary_expr_t *ast_binary_copy(module_t *m, ast_binary_expr_t *temp) {
    ast_binary_expr_t *binary = COPY_NEW(ast_binary_expr_t, temp);
    binary->left = *ast_expr_copy(m, &temp->left);
    binary->right = *ast_expr_copy(m, &temp->right);
    return binary;
}

static ast_map_access_t *ast_map_access_copy(module_t *m, ast_map_access_t *temp) {
    ast_map_access_t *access = COPY_NEW(ast_map_access_t, temp);
    access->left = *ast_expr_copy(m, &temp->left);
    access->key = *ast_expr_copy(m, &temp->key);
    return access;
}

static ast_vec_access_t *ast_list_access_copy(module_t *m, ast_vec_access_t *temp) {
    ast_vec_access_t *access = COPY_NEW(ast_vec_access_t, temp);
    access->left = *ast_expr_copy(m, &temp->left);
    access->index = *ast_expr_copy(m, &temp->index);
    return access;
}

static ast_tuple_access_t *ast_tuple_access_copy(module_t *m, ast_tuple_access_t *temp) {
    ast_tuple_access_t *access = COPY_NEW(ast_tuple_access_t, temp);
    access->left = *ast_expr_copy(m, &temp->left);
    return access;
}

static ast_struct_select_t *ast_struct_select_copy(module_t *m, ast_struct_select_t *temp) {
    ast_struct_select_t *select = COPY_NEW(ast_struct_select_t, temp);
    select->instance = *ast_expr_copy(m, &temp->instance);
    select->key = strdup(temp->key);
    return select;
}

static ast_vec_new_t *ast_list_new_copy(module_t *m, ast_vec_new_t *temp) {
    ast_vec_new_t *vec_new = COPY_NEW(ast_vec_new_t, temp);
    if (temp->elements) {
        vec_new->elements = ast_list_expr_copy(m, temp->elements);
    }

    if (temp->len) {
        vec_new->len = ast_expr_copy(m, temp->len);
    }

    if (temp->cap) {
        vec_new->cap = ast_expr_copy(m, temp->cap);
    }

    return vec_new;
}

static ast_map_new_t *ast_map_new_copy(module_t *m, ast_map_new_t *temp) {
    ast_map_new_t *map_new = COPY_NEW(ast_map_new_t, temp);
    list_t *elements = ct_list_new(sizeof(ast_map_element_t));
    for (int i = 0; i < temp->elements->length; ++i) {
        ast_map_element_t *temp_map_element = ct_list_value(temp->elements, i);

        ast_map_element_t *map_element = NEW(ast_map_element_t);
        map_element->key = *ast_expr_copy(m, &temp_map_element->key);
        map_element->value = *ast_expr_copy(m, &temp_map_element->value);
        ct_list_push(elements, map_element);
    }

    map_new->elements = elements;
    return map_new;
}

static ast_set_new_t *ast_set_new_copy(module_t *m, ast_set_new_t *temp) {
    ast_set_new_t *set_new = COPY_NEW(ast_set_new_t, temp);
    list_t *elements = ct_list_new(sizeof(ast_expr_t));
    for (int i = 0; i < temp->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(temp->elements, i);
        ct_list_push(elements, ast_expr_copy(m, expr));
    }

    set_new->elements = elements;
    return set_new;
}

static ast_tuple_new_t *ast_tuple_new_copy(module_t *m, ast_tuple_new_t *temp) {
    ast_tuple_new_t *tuple_new = COPY_NEW(ast_tuple_new_t, temp);
    tuple_new->elements = ast_list_expr_copy(m, temp->elements);
    return tuple_new;
}

static ast_struct_new_t *ast_struct_new_copy(module_t *m, ast_struct_new_t *temp) {
    ast_struct_new_t *struct_new = COPY_NEW(ast_struct_new_t, temp);
    struct_new->type = type_copy(temp->type);

    list_t *properties = ct_list_new(sizeof(struct_property_t));
    for (int i = 0; i < temp->properties->length; ++i) {
        struct_property_t *temp_property = ct_list_value(temp->properties, i);

        struct_property_t *property = NEW(struct_property_t);
        property->type = type_copy(temp_property->type);
        property->key = strdup(temp_property->key);
        property->right = ast_expr_copy(m, temp_property->right);
        ct_list_push(properties, property);
    }
    struct_new->properties = properties;
    return struct_new;
}

static ast_access_t *ast_access_copy(module_t *m, ast_access_t *temp) {
    ast_access_t *access = COPY_NEW(ast_access_t, temp);
    access->left = *ast_expr_copy(m, &temp->left);
    access->key = *ast_expr_copy(m, &temp->key);
    return access;
}

static ast_select_t *ast_select_copy(module_t *m, ast_select_t *temp) {
    ast_select_t *select = COPY_NEW(ast_select_t, temp);
    select->left = *ast_expr_copy(m, &temp->left);
    select->key = strdup(temp->key);
    return select;
}

static ast_tuple_destr_t *ast_tuple_destr_copy(module_t *m, ast_tuple_destr_t *temp) {
    ast_tuple_destr_t *tuple_destr = COPY_NEW(ast_tuple_destr_t, temp);
    tuple_destr->elements = ast_list_expr_copy(m, temp->elements);
    return tuple_destr;
}

static ast_macro_co_async_t *ast_co_async_copy(module_t *m, ast_macro_co_async_t *temp) {
    ast_macro_co_async_t *expr = COPY_NEW(ast_macro_co_async_t, temp);
    expr->closure_fn = ast_fndef_copy(m, temp->closure_fn);
    expr->closure_fn_void = ast_fndef_copy(m, temp->closure_fn_void);
    expr->origin_call = ast_call_copy(m, temp->origin_call);
    if (expr->flag_expr) {
        expr->flag_expr = ast_expr_copy(m, expr->flag_expr);
    }
    return expr;
}

ast_expr_t *ast_expr_copy(module_t *m, ast_expr_t *temp) {
    if (temp == NULL) {
        return NULL;
    }

    ast_expr_t *expr = COPY_NEW(ast_expr_t, temp);
    switch (temp->assert_type) {
        case AST_EXPR_LITERAL: {
            expr->value = ast_literal_copy(m, temp->value);
            break;
        }
        case AST_EXPR_IDENT: {
            expr->value = ast_ident_copy(m, temp->value);
            break;
        }
        case AST_EXPR_ENV_ACCESS: {
            expr->value = ast_env_access_copy(m, temp->value);
            break;
        }
        case AST_EXPR_BINARY: {
            expr->value = ast_binary_copy(m, temp->value);
            break;
        }
        case AST_EXPR_UNARY: {
            expr->value = ast_unary_copy(m, temp->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            expr->value = ast_access_copy(m, temp->value);
            break;
        }
        case AST_EXPR_VEC_NEW: {
            expr->value = ast_list_new_copy(m, temp->value);
            break;
        }
        case AST_EXPR_VEC_ACCESS: {
            expr->value = ast_list_access_copy(m, temp->value);
            break;
        }
        case AST_EXPR_EMPTY_CURLY_NEW: {
            expr->value = temp->value;
            break;
        }
        case AST_EXPR_MAP_NEW: {
            expr->value = ast_map_new_copy(m, temp->value);
            break;
        }
        case AST_EXPR_MAP_ACCESS: {
            expr->value = ast_map_access_copy(m, temp->value);
            break;
        }
        case AST_EXPR_STRUCT_NEW: {
            expr->value = ast_struct_new_copy(m, temp->value);
            break;
        }
        case AST_EXPR_STRUCT_SELECT: {
            expr->value = ast_struct_select_copy(m, temp->value);
            break;
        }
        case AST_EXPR_TUPLE_NEW: {
            expr->value = ast_tuple_new_copy(m, temp->value);
            break;
        }
        case AST_EXPR_TUPLE_DESTR: {
            expr->value = ast_tuple_destr_copy(m, temp->value);
            break;
        }
        case AST_EXPR_TUPLE_ACCESS: {
            expr->value = ast_tuple_access_copy(m, temp->value);
            break;
        }
        case AST_EXPR_SET_NEW: {
            expr->value = ast_set_new_copy(m, temp->value);
            break;
        }
        case AST_CALL: {
            expr->value = ast_call_copy(m, temp->value);
            break;
        }
        case AST_MACRO_CO_ASYNC: {
            expr->value = ast_co_async_copy(m, temp->value);
            break;
        }
        case AST_FNDEF: {
            expr->value = ast_fndef_copy(m, temp->value);
            break;
        }
        case AST_EXPR_AS: {
            expr->value = ast_as_expr_copy(m, temp->value);
            break;
        }
        case AST_EXPR_NEW: {
            expr->value = ast_new_expr_copy(m, temp->value);
            break;
        }
        case AST_EXPR_IS: {
            expr->value = ast_is_expr_copy(m, temp->value);
            break;
        }
        case AST_CATCH: {
            expr->value = ast_catch_copy(m, temp->value);
            break;
        }
        case AST_MACRO_EXPR_SIZEOF: {
            expr->value = ast_sizeof_expr_copy(m, temp->value);
            break;
        }
        case AST_MACRO_EXPR_ULA: {
            expr->value = ast_ula_expr_copy(m, temp->value);
        }
        case AST_MACRO_EXPR_DEFAULT: {
            expr->value = ast_default_expr_copy(m, temp->value);
        }
        case AST_MACRO_EXPR_REFLECT_HASH: {
            expr->value = ast_reflect_hash_expr_copy(m, temp->value);
            break;
        }
        case AST_MACRO_EXPR_TYPE_EQ: {
            expr->value = ast_type_eq_expr_copy(temp->value);
            break;
        }
        case AST_EXPR_SELECT: {
            expr->value = ast_select_copy(m, temp->value);
            break;
        }
        default:
            assertf(false, "ast_expr_copy unknown expr");
    }

    return expr;
}

static ast_expr_fake_stmt_t *ast_expr_fake_copy(module_t *m, ast_expr_fake_stmt_t *temp) {
    ast_expr_fake_stmt_t *stmt = COPY_NEW(ast_expr_fake_stmt_t, temp);
    stmt->expr = *ast_expr_copy(m, &temp->expr);
    return stmt;
}

static ast_var_decl_t *ast_var_decl_copy(module_t *m, ast_var_decl_t *temp) {
    ast_var_decl_t *var_decl = COPY_NEW(ast_var_decl_t, temp);
    var_decl->type = type_copy(temp->type);
    var_decl->ident = strdup(temp->ident);
    return var_decl;
}

static ast_vardef_stmt_t *ast_vardef_copy(module_t *m, ast_vardef_stmt_t *temp) {
    ast_vardef_stmt_t *vardef = COPY_NEW(ast_vardef_stmt_t, temp);
    vardef->var_decl = *ast_var_decl_copy(m, &temp->var_decl);
    vardef->right = *ast_expr_copy(m, &temp->right);
    return vardef;
}

static ast_catch_t *ast_catch_copy(module_t *m, ast_catch_t *temp) {
    ast_catch_t *catch = COPY_NEW(ast_catch_t, temp);
    catch->try_expr = *ast_expr_copy(m, &temp->try_expr);
    catch->catch_err = *ast_var_decl_copy(m, &temp->catch_err);
    catch->catch_body = ast_body_copy(m, temp->catch_body);// 需要实现这个函数
    return catch;
}

static ast_var_tuple_def_stmt_t *ast_var_tuple_def_copy(module_t *m, ast_var_tuple_def_stmt_t *temp) {
    ast_var_tuple_def_stmt_t *stmt = COPY_NEW(ast_var_tuple_def_stmt_t, temp);
    stmt->tuple_destr = ast_tuple_destr_copy(m, temp->tuple_destr);// 需要实现这个函数
    stmt->right = *ast_expr_copy(m, &temp->right);
    return stmt;
}

static ast_assign_stmt_t *ast_assign_copy(module_t *m, ast_assign_stmt_t *temp) {
    ast_assign_stmt_t *stmt = COPY_NEW(ast_assign_stmt_t, temp);
    stmt->left = *ast_expr_copy(m, &temp->left);
    stmt->right = *ast_expr_copy(m, &temp->right);
    return stmt;
}

static ast_if_stmt_t *ast_if_copy(module_t *m, ast_if_stmt_t *temp) {
    ast_if_stmt_t *stmt = COPY_NEW(ast_if_stmt_t, temp);
    stmt->condition = *ast_expr_copy(m, &temp->condition);
    stmt->consequent = ast_body_copy(m, temp->consequent);// 需要实现这个函数
    stmt->alternate = ast_body_copy(m, temp->alternate);  // 需要实现这个函数
    return stmt;
}

static ast_for_cond_stmt_t *ast_for_cond_copy(module_t *m, ast_for_cond_stmt_t *temp) {
    ast_for_cond_stmt_t *stmt = COPY_NEW(ast_for_cond_stmt_t, temp);
    stmt->condition = *ast_expr_copy(m, &temp->condition);
    stmt->body = ast_body_copy(m, temp->body);// 需要实现这个函数
    return stmt;
}

static ast_for_iterator_stmt_t *ast_for_iterator_copy(module_t *m, ast_for_iterator_stmt_t *temp) {
    ast_for_iterator_stmt_t *stmt = COPY_NEW(ast_for_iterator_stmt_t, temp);
    stmt->iterate = *ast_expr_copy(m, &temp->iterate);
    stmt->first = *ast_var_decl_copy(m, &temp->first);
    stmt->second = temp->second ? ast_var_decl_copy(m, temp->second) : NULL;
    stmt->body = ast_body_copy(m, temp->body);// 需要实现这个函数
    return stmt;
}

static ast_for_tradition_stmt_t *ast_tradition_copy(module_t *m, ast_for_tradition_stmt_t *temp) {
    ast_for_tradition_stmt_t *stmt = COPY_NEW(ast_for_tradition_stmt_t, temp);
    stmt->init = ast_stmt_copy(m, temp->init);
    stmt->cond = *ast_expr_copy(m, &temp->cond);
    stmt->update = ast_stmt_copy(m, temp->update);
    stmt->body = ast_body_copy(m, temp->body);// 需要实现这个函数
    return stmt;
}

static ast_throw_stmt_t *ast_throw_copy(module_t *m, ast_throw_stmt_t *temp) {
    ast_throw_stmt_t *stmt = COPY_NEW(ast_throw_stmt_t, temp);
    stmt->error = *ast_expr_copy(m, &temp->error);
    return stmt;
}

static ast_return_stmt_t *ast_return_copy(module_t *m, ast_return_stmt_t *temp) {
    ast_return_stmt_t *stmt = COPY_NEW(ast_return_stmt_t, temp);
    stmt->expr = ast_expr_copy(m, temp->expr);
    return stmt;
}

static ast_continue_t *ast_continue_copy(module_t *m, ast_continue_t *temp) {
    ast_continue_t *stmt = COPY_NEW(ast_continue_t, temp);
    stmt->expr = ast_expr_copy(m, temp->expr);
    return stmt;
}

static slice_t *ast_body_copy(module_t *m, slice_t *temp) {
    slice_t *body = slice_new();
    for (int i = 0; i < temp->count; ++i) {
        slice_push(body, ast_stmt_copy(m, temp->take[i]));
    }
    return body;
}

static ast_call_t *ast_call_copy(module_t *m, ast_call_t *temp) {
    ast_call_t *call = COPY_NEW(ast_call_t, temp);
    call->return_type = type_copy(temp->return_type);
    call->left = *ast_expr_copy(m, &temp->left);
    call->args = ast_list_expr_copy(m, temp->args);
    call->spread = temp->spread;
    return call;
}

static ast_stmt_t *ast_stmt_copy(module_t *m, ast_stmt_t *temp) {
    ast_stmt_t *stmt = COPY_NEW(ast_stmt_t, temp);
    switch (temp->assert_type) {
        case AST_STMT_EXPR_FAKE: {
            stmt->value = ast_expr_fake_copy(m, temp->value);
            break;
        }
        case AST_VAR_DECL: {
            stmt->value = ast_var_decl_copy(m, temp->value);
            break;
        }
        case AST_STMT_VARDEF: {
            stmt->value = ast_vardef_copy(m, temp->value);
            break;
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            stmt->value = ast_var_tuple_def_copy(m, temp->value);
            break;
        }
        case AST_STMT_ASSIGN: {
            stmt->value = ast_assign_copy(m, temp->value);
            break;
        }
        case AST_STMT_IF: {
            stmt->value = ast_if_copy(m, temp->value);
            break;
        }
        case AST_STMT_FOR_COND: {
            stmt->value = ast_for_cond_copy(m, temp->value);
            break;
        }
        case AST_STMT_FOR_ITERATOR: {
            stmt->value = ast_for_iterator_copy(m, temp->value);
            break;
        }
        case AST_STMT_FOR_TRADITION: {
            stmt->value = ast_tradition_copy(m, temp->value);
            break;
        }
        case AST_FNDEF: {
            stmt->value = ast_fndef_copy(m, temp->value);
            break;
        }
        case AST_STMT_THROW: {
            stmt->value = ast_throw_copy(m, temp->value);
            break;
        }
        case AST_STMT_RETURN: {
            stmt->value = ast_return_copy(m, temp->value);
            break;
        }
        case AST_CALL: {
            stmt->value = ast_call_copy(m, temp->value);
            break;
        }
        case AST_STMT_CONTINUE: {
            stmt->value = ast_continue_copy(m, temp->value);
            break;
        }
        case AST_CATCH: {
            stmt->value = ast_catch_copy(m, temp->value);
            break;
        }
        default:
            assertf(false, "[ast_stmt_copy] unknown stmt");
    }

    return stmt;
}

list_t *ast_fn_formals_copy(module_t *m, list_t *temp_formals) {
    list_t *formals = ct_list_new(sizeof(ast_var_decl_t));

    for (int i = 0; i < temp_formals->length; ++i) {
        ast_var_decl_t *temp = ct_list_value(temp_formals, i);
        ast_var_decl_t *var_decl = ast_var_decl_copy(m, temp);
        ct_list_push(formals, var_decl);
    }

    return formals;
}

/**
 * 深度 copy
 * @return
 */
ast_fndef_t *ast_fndef_copy(module_t *m, ast_fndef_t *temp) {
    ast_fndef_t *fndef = COPY_NEW(ast_fndef_t, temp);
    fndef->symbol_name = temp->symbol_name;
    fndef->linkid = temp->linkid;
    fndef->closure_name = temp->closure_name;
    fndef->return_type = type_copy(temp->return_type);
    fndef->params = ast_fn_formals_copy(m, temp->params);
    fndef->type = type_copy(temp->type);
    fndef->capture_exprs = temp->capture_exprs;
    fndef->fn_name = temp->fn_name;
    fndef->rel_path = temp->rel_path;
    fndef->column = temp->column;
    fndef->line = temp->line;
    if (temp->body) {
        fndef->body = ast_body_copy(m, temp->body);
    }
    fndef->is_generics = false;
    fndef->global_parent = NULL;
    if (!fndef->is_local) {
        m->analyzer_global = fndef;
        fndef->local_children = slice_new();
    } else {
        assert(m->analyzer_global);
        slice_push(m->analyzer_global->local_children, fndef);
        fndef->global_parent = m->analyzer_global;
    }

    return fndef;
}
