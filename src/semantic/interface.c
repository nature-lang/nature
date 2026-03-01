#include "interface.h"

#include "src/ast.h"
#include "src/error.h"
#include "src/lir.h"
#include "src/symbol/symbol.h"

#define INTERFACE_ASSERTF(cond, fmt, ...)                                                             \
    {                                                                                                 \
        if (!(cond)) {                                                                                \
            dump_errorf(m, CT_STAGE_INFER, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
        }                                                                                             \
    }

static bool interface_equal(module_t *m, type_t *left, type_t *right) {
    int save_line = m->current_line;
    int save_column = m->current_column;

    if (!left || !right || !left->ident || !right->ident) {
        return false;
    }

    if (!str_equal(left->ident, right->ident)) {
        return false;
    }

    int left_args_len = left->args ? left->args->length : 0;
    int right_args_len = right->args ? right->args->length : 0;
    if (left_args_len != right_args_len) {
        return false;
    }

    for (int i = 0; i < left_args_len; ++i) {
        type_t *left_arg = ct_list_value(left->args, i);
        type_t *right_arg = ct_list_value(right->args, i);

        type_t reduced_left_arg = reduction_type(m, type_copy(m, *left_arg));
        type_t reduced_right_arg = reduction_type(m, type_copy(m, *right_arg));
        if (!type_compare(reduced_left_arg, reduced_right_arg)) {
            m->current_line = save_line;
            m->current_column = save_column;
            return false;
        }
    }

    m->current_line = save_line;
    m->current_column = save_column;
    return true;
}

static type_t interface_extract_fn_type(module_t *m, ast_fndef_t *fndef) {
    // method_table only stores instance impl methods
    assert(!fndef->is_static);

    type_fn_t *fn = NEW(type_fn_t);
    fn->fn_name = fndef->fn_name;
    fn->is_tpl = fndef->is_tpl;
    fn->is_errable = fndef->is_errable;
    fn->is_fx = fndef->is_fx;
    fn->param_types = ct_list_new(sizeof(type_t));
    fn->return_type = reduction_type(m, type_copy(m, fndef->return_type));

    assert(fndef->self_kind != PARAM_SELF_NULL);
    // skip self param
    int start_index = 1;
    for (int i = start_index; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);
        type_t param_type = reduction_type(m, type_copy(m, param->type));
        ct_list_push(fn->param_types, &param_type);
    }

    fn->is_rest = fndef->rest_param;

    type_t result = type_new(TYPE_FN, fn);
    result.status = REDUCTION_STATUS_DONE;
    return result;
}

/**
 * 递归查找 stmt 中是否包含 target_interface
 */
bool check_impl_interface_contains(module_t *m, ast_typedef_stmt_t *stmt, type_t *find_target_interface) {
    int save_line = m->current_line;
    int save_column = m->current_column;

    if (!stmt->impl_interfaces) {
        m->current_line = save_line;
        m->current_column = save_column;
        return false;
    }

    for (int i = 0; i < stmt->impl_interfaces->length; ++i) {
        type_t *impl_interface = ct_list_value(stmt->impl_interfaces, i);
        if (interface_equal(m, impl_interface, find_target_interface)) {
            m->current_line = save_line;
            m->current_column = save_column;
            return true;
        }

        symbol_t *s = symbol_table_get(impl_interface->ident);
        if (!s) {
            continue;
        }

        assert(s->type == SYMBOL_TYPE);
        ast_typedef_stmt_t *typedef_stmt = s->ast_value;
        if (typedef_stmt->impl_interfaces == NULL) {
            continue;
        }

        if (check_impl_interface_contains(m, typedef_stmt, find_target_interface)) {
            m->current_line = save_line;
            m->current_column = save_column;
            return true;
        }
    }

    m->current_line = save_line;
    m->current_column = save_column;
    return false;
}

static void interface_combination_typedef(module_t *m, ast_typedef_stmt_t *typedef_stmt) {
    if (!typedef_stmt->is_interface || !typedef_stmt->impl_interfaces || typedef_stmt->impl_interfaces->length == 0) {
        return;
    }

    m->current_line = typedef_stmt->type_expr.line;
    m->current_column = typedef_stmt->type_expr.column;

    typedef_stmt->type_expr = reduction_type(m, typedef_stmt->type_expr);
    INTERFACE_ASSERTF(typedef_stmt->type_expr.kind == TYPE_INTERFACE, "interface '%s' type invalid",
                      typedef_stmt->ident);

    type_interface_t *origin_interface = typedef_stmt->type_expr.interface;
    struct sc_map_sv exists = {0};
    sc_map_init_sv(&exists, 0, 0); // value is type_t*

    for (int i = 0; i < origin_interface->elements->length; ++i) {
        type_t *method = ct_list_value(origin_interface->elements, i);
        INTERFACE_ASSERTF(method->kind == TYPE_FN, "interface '%s' contains non-fn method", typedef_stmt->ident);
        INTERFACE_ASSERTF(method->fn && method->fn->fn_name, "interface '%s' method invalid", typedef_stmt->ident);
        sc_map_put_sv(&exists, method->fn->fn_name, method);
    }

    // merge composed interfaces
    for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
        type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
        m->current_line = impl_interface->line;
        m->current_column = impl_interface->column;

        *impl_interface = reduction_type(m, *impl_interface);
        INTERFACE_ASSERTF(impl_interface->kind == TYPE_INTERFACE, "interface '%s' impl target '%s' is not interface",
                          typedef_stmt->ident, impl_interface->ident);

        for (int j = 0; j < impl_interface->interface->elements->length; ++j) {
            type_t *method = ct_list_value(impl_interface->interface->elements, j);
            type_t *exist_method = sc_map_get_sv(&exists, method->fn->fn_name);
            if (exist_method) {
                INTERFACE_ASSERTF(type_compare(*exist_method, *method), "duplicate method '%s'", method->fn->fn_name);
                continue;
            }

            sc_map_put_sv(&exists, method->fn->fn_name, method);
            ct_list_push(origin_interface->elements, method);
        }

        if (impl_interface->interface->alloc_types && impl_interface->interface->alloc_types->length > 0) {
            if (!origin_interface->alloc_types) {
                origin_interface->alloc_types = ct_list_new(sizeof(type_t));
            }

            for (int j = 0; j < impl_interface->interface->alloc_types->length; ++j) {
                type_t *alloc_type = ct_list_value(impl_interface->interface->alloc_types, j);
                bool exists_type = false;
                for (int k = 0; k < origin_interface->alloc_types->length; ++k) {
                    type_t *origin_alloc = ct_list_value(origin_interface->alloc_types, k);
                    if (type_compare(*origin_alloc, *alloc_type)) {
                        exists_type = true;
                        break;
                    }
                }

                if (!exists_type) {
                    ct_list_push(origin_interface->alloc_types, alloc_type);
                }
            }
        }

        if (impl_interface->interface->deny_types && impl_interface->interface->deny_types->length > 0) {
            if (!origin_interface->deny_types) {
                origin_interface->deny_types = ct_list_new(sizeof(type_t));
            }

            for (int j = 0; j < impl_interface->interface->deny_types->length; ++j) {
                type_t *deny_type = ct_list_value(impl_interface->interface->deny_types, j);
                bool exists_type = false;
                for (int k = 0; k < origin_interface->deny_types->length; ++k) {
                    type_t *origin_deny = ct_list_value(origin_interface->deny_types, k);
                    if (type_compare(*origin_deny, *deny_type)) {
                        exists_type = true;
                        break;
                    }
                }

                if (!exists_type) {
                    ct_list_push(origin_interface->deny_types, deny_type);
                }
            }
        }
    }
}

static void interface_check_typedef_impl(module_t *m, ast_typedef_stmt_t *typedef_stmt, type_t *impl_interface) {
    if (impl_interface->ident_kind != TYPE_IDENT_INTERFACE) {
        return;
    }

    m->current_line = impl_interface->line;
    m->current_column = impl_interface->column;

    *impl_interface = reduction_type(m, *impl_interface);
    INTERFACE_ASSERTF(impl_interface->kind == TYPE_INTERFACE, "type '%s' impl target '%s' is not interface",
                      typedef_stmt->ident, impl_interface->ident);

    type_interface_t *interface_type = impl_interface->interface;
    if (!interface_type || interface_type->elements->length == 0) {
        return;
    }

    for (int i = 0; i < interface_type->elements->length; ++i) {
        type_t *expect_type = ct_list_value(interface_type->elements, i);
        INTERFACE_ASSERTF(expect_type->kind == TYPE_FN, "interface '%s' contains non-fn constraint",
                          impl_interface->ident);

        type_fn_t *interface_fn_type = expect_type->fn;
        INTERFACE_ASSERTF(interface_fn_type && interface_fn_type->fn_name, "interface '%s' fn invalid",
                          impl_interface->ident);

        char *fn_ident = str_connect_by(typedef_stmt->ident, interface_fn_type->fn_name, IMPL_CONNECT_IDENT);
        ast_fndef_t *impl_method = sc_map_get_sv(&typedef_stmt->method_table, fn_ident);
        INTERFACE_ASSERTF(impl_method, "type '%s' not impl fn '%s' for interface '%s'",
                          typedef_stmt->ident, interface_fn_type->fn_name, impl_interface->ident);

        m->current_line = impl_method->line;
        m->current_column = impl_method->column;

        type_t actual_type = interface_extract_fn_type(m, impl_method);
        INTERFACE_ASSERTF(type_compare(*expect_type, actual_type),
                          "the fn '%s' of type '%s' mismatch interface '%s'",
                          impl_method->fn_name, typedef_stmt->ident, impl_interface->ident);
    }
}

void interface(module_t *m) {
    // reduction 过程中可能触发 impl check（内部会写入 temp_worklist）
    if (m->temp_worklist == NULL) {
        m->temp_worklist = linked_new();
    }

    for (int i = 0; i < m->ast_typedefs->count; ++i) {
        ast_typedef_stmt_t *typedef_stmt = m->ast_typedefs->take[i];
        if (!typedef_stmt->impl_interfaces || typedef_stmt->impl_interfaces->length == 0) {
            continue;
        }

        if (typedef_stmt->is_interface) {
            interface_combination_typedef(m, typedef_stmt);
            continue;
        }

        for (int j = 0; j < typedef_stmt->impl_interfaces->length; ++j) {
            type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, j);
            interface_check_typedef_impl(m, typedef_stmt, impl_interface);
        }
    }
}
