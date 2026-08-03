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

ast_fndef_t *generate_receiver_wrapper(module_t *m, ast_fndef_t *origin_fndef);

static void interface_generate_receiver_wrappers(module_t *m);

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
 * 为值类型接收器生成包装函数
 * 当 self_kind == PARAM_SELF_T 时，interface 存储指针数据但方法期望值类型
 * wrapper 接收指针参数，解引用后调用原函数
 *
 * 原函数: fn dog.speak<T>(self) { ... }
 * 生成的 wrapper: fn dog.speak_receiver_wrapper<T>(*dog ptr) { return dog.speak<T>(*ptr) }
 */
ast_fndef_t *generate_receiver_wrapper(module_t *m, ast_fndef_t *origin_fndef) {
    type_t impl_type = origin_fndef->impl_type;

    // 创建 wrapper 函数定义
    ast_fndef_t *wrapper = ast_fndef_new(m, origin_fndef->line, origin_fndef->column);
    wrapper->module = origin_fndef->module;

    // 设置函数名称和符号名
    wrapper->fn_name = dsprintf("%s_receiver_wrapper", origin_fndef->fn_name);
    wrapper->symbol_name = dsprintf("%s_receiver_wrapper", origin_fndef->symbol_name);
    wrapper->fn_name_with_pkg = dsprintf("%s_receiver_wrapper", origin_fndef->fn_name_with_pkg);

    // 复制返回类型和其他属性
    wrapper->return_type = origin_fndef->return_type;
    wrapper->is_errable = origin_fndef->is_errable;
    wrapper->is_impl = true;
    wrapper->impl_type = impl_type;
    wrapper->self_kind = PARAM_SELF_REF_T; // wrapper 接收指针参数

    // 复制泛型参数
    wrapper->generics_params = origin_fndef->generics_params;
    wrapper->is_generics = origin_fndef->is_generics;
    wrapper->is_tpl = origin_fndef->is_tpl;

    // 创建参数列表 - 第一个参数是 ref<impl_type>
    wrapper->params = ct_list_new(sizeof(ast_var_decl_t));

    // 为 self 参数生成唯一标识符并注册到符号表
    char *self_unique_ident = var_unique_ident(m, FN_SELF_NAME);
    ast_var_decl_t *self_param = NEW(ast_var_decl_t);
    self_param->ident = self_unique_ident;
    self_param->type = type_refof(impl_type);
    symbol_table_set(self_unique_ident, SYMBOL_VAR, self_param, true);
    ct_list_push(wrapper->params, self_param);

    // 复制原函数的其他参数（跳过第一个 self 参数）
    for (int i = 1; i < origin_fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(origin_fndef->params, i);
        ct_list_push(wrapper->params, param);
    }

    // 构建函数体：return origin_fn(*self, other_args...)
    wrapper->body = slice_new();

    // 创建解引用表达式: *self (使用唯一标识符)
    ast_unary_expr_t *deref_expr = NEW(ast_unary_expr_t);
    deref_expr->op = AST_OP_IA; // indirect address (dereference)
    deref_expr->operand = (ast_expr_t){
        .assert_type = AST_EXPR_IDENT,
        .value = ast_new_ident(self_unique_ident),
        .line = origin_fndef->line,
        .column = origin_fndef->column,
    };

    // 创建 call 表达式
    ast_call_t *call = NEW(ast_call_t);
    call->left = (ast_expr_t){
        .assert_type = AST_EXPR_IDENT,
        .value = ast_new_ident(origin_fndef->symbol_name),
        .line = origin_fndef->line,
        .column = origin_fndef->column,
    };
    call->args = ct_list_new(sizeof(ast_expr_t));
    call->spread = false;
    call->inject_self_arg = true;

    // 转发泛型参数作为 generics_args
    if (origin_fndef->generics_params && origin_fndef->generics_params->length > 0) {
        call->generics_args = ct_list_new(sizeof(type_t));
        for (int i = 0; i < origin_fndef->generics_params->length; ++i) {
            ast_generics_param_t *gp = ct_list_value(origin_fndef->generics_params, i);
            type_t arg_type = type_ident_new(gp->ident, TYPE_IDENT_GENERICS_PARAM);
            ct_list_push(call->generics_args, &arg_type);
        }
    } else {
        call->generics_args = NULL;
    }

    // 第一个参数是解引用的 self
    ast_expr_t deref_arg = {
        .assert_type = AST_EXPR_UNARY,
        .value = deref_expr,
        .line = origin_fndef->line,
        .column = origin_fndef->column,
    };
    ct_list_push(call->args, &deref_arg);

    // 添加其他参数
    for (int i = 1; i < origin_fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(origin_fndef->params, i);
        ast_expr_t arg = {
            .assert_type = AST_EXPR_IDENT,
            .value = ast_new_ident(param->ident),
            .line = origin_fndef->line,
            .column = origin_fndef->column,
        };
        ct_list_push(call->args, &arg);
    }

    // 创建 return 语句
    ast_return_stmt_t *ret_stmt = NEW(ast_return_stmt_t);
    if (origin_fndef->return_type.kind != TYPE_VOID) {
        ret_stmt->expr = NEW(ast_expr_t);
        ret_stmt->expr->assert_type = AST_CALL;
        ret_stmt->expr->value = call;
        ret_stmt->expr->line = origin_fndef->line;
        ret_stmt->expr->column = origin_fndef->column;
    } else {
        // void 返回类型，只调用不 return
        ret_stmt->expr = NULL;
    }

    ast_stmt_t *stmt = NEW(ast_stmt_t);
    if (origin_fndef->return_type.kind != TYPE_VOID) {
        stmt->assert_type = AST_STMT_RETURN;
        stmt->value = ret_stmt;
    } else {
        // void 返回类型，改为表达式语句
        stmt->assert_type = AST_CALL;
        stmt->value = call;
    }
    stmt->line = origin_fndef->line;
    stmt->column = origin_fndef->column;
    slice_push(wrapper->body, stmt);

    return wrapper;
}

static void interface_generate_receiver_wrappers(module_t *m) {
    int fndef_count = m->ast_fndefs->count;
    for (int i = 0; i < fndef_count; ++i) {
        ast_fndef_t *ast_fn = m->ast_fndefs->take[i];
        if (!ast_fn->is_impl || ast_fn->is_static || ast_fn->receiver_wrapper_ident) {
            continue;
        }

        if (ast_fn->self_kind != PARAM_SELF_T || ast_fn->impl_type.ident_kind != TYPE_IDENT_DEF) {
            continue;
        }

        symbol_t *type_symbol = symbol_table_get(ast_fn->impl_type.ident);
        INTERFACE_ASSERTF(type_symbol && type_symbol->type == SYMBOL_TYPE, "type '%s' undeclared",
                          ast_fn->impl_type.ident);
        if (!type_symbol || type_symbol->type != SYMBOL_TYPE) {
            continue;
        }

        type_t impl_type = reduction_type(m, type_copy(m, ast_fn->impl_type));
        if (!(ast_fn->self_kind == PARAM_SELF_T && impl_type.storage_kind != STORAGE_KIND_PTR)) {
            continue;
        }

        m->current_line = ast_fn->line;
        m->current_column = ast_fn->column;
        ast_fndef_t *wrapper_fn = generate_receiver_wrapper(m, ast_fn);

        symbol_t *wrapper_symbol = symbol_table_set(wrapper_fn->symbol_name, SYMBOL_FN, wrapper_fn, false);
        INTERFACE_ASSERTF(wrapper_symbol, "ident '%s' redeclared", wrapper_fn->symbol_name);
        ast_fn->receiver_wrapper_ident = wrapper_fn->symbol_name;

        if (!wrapper_symbol) {
            continue;
        }

        slice_push(wrapper_fn->module->ast_fndefs, wrapper_fn);
    }
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

    interface_generate_receiver_wrappers(m);

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
