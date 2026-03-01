#include "generics.h"

#include <string.h>

#include "src/error.h"
#include "src/symbol/symbol.h"

#define GENERIC_ASSERTF(cond, fmt, ...)                                                               \
    {                                                                                                 \
        if (!(cond)) {                                                                                \
            dump_errorf(m, CT_STAGE_GENERIC, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
        }                                                                                             \
    }

// forward declarations
static void generics_body(module_t *m, slice_t *body);
static void generics_expr(module_t *m, ast_expr_t *expr);
static void generics_check_bool_operand(module_t *m, ast_expr_t *expr);

static inline void interface_alloc_push_kind(list_t *alloc_types, type_kind kind) {
    type_t t = type_kind_new(kind);
    ct_list_push(alloc_types, &t);
}

static inline void interface_alloc_push_numeric(list_t *alloc_types) {
    interface_alloc_push_kind(alloc_types, TYPE_INT);
    interface_alloc_push_kind(alloc_types, TYPE_UINT);
    interface_alloc_push_kind(alloc_types, TYPE_INT8);
    interface_alloc_push_kind(alloc_types, TYPE_UINT8);
    interface_alloc_push_kind(alloc_types, TYPE_INT16);
    interface_alloc_push_kind(alloc_types, TYPE_UINT16);
    interface_alloc_push_kind(alloc_types, TYPE_INT32);
    interface_alloc_push_kind(alloc_types, TYPE_UINT32);
    interface_alloc_push_kind(alloc_types, TYPE_INT64);
    interface_alloc_push_kind(alloc_types, TYPE_UINT64);
    interface_alloc_push_kind(alloc_types, TYPE_FLOAT);
    interface_alloc_push_kind(alloc_types, TYPE_FLOAT32);
    interface_alloc_push_kind(alloc_types, TYPE_FLOAT64);
}

void generics_interface_fill_builtin_alloc_types(type_t t) {
    if (t.kind != TYPE_INTERFACE || !t.interface || !t.ident) {
        return;
    }

    if (t.ident_kind != TYPE_IDENT_INTERFACE) {
        return;
    }

    if (t.interface->alloc_types || t.interface->deny_types) {
        return;
    }

    list_t *alloc_types = NULL;
    list_t *deny_types = NULL;
    if (str_equal(t.ident, NUMERIC_IDENT)) {
        alloc_types = ct_list_new(sizeof(type_t));
        interface_alloc_push_numeric(alloc_types);
    } else if (str_equal(t.ident, COMPARABLE_IDENT)) {
        alloc_types = ct_list_new(sizeof(type_t));
        interface_alloc_push_numeric(alloc_types);
        interface_alloc_push_kind(alloc_types, TYPE_STRING);
    } else if (str_equal(t.ident, ADDABLE_IDENT)) {
        alloc_types = ct_list_new(sizeof(type_t));
        interface_alloc_push_numeric(alloc_types);
        interface_alloc_push_kind(alloc_types, TYPE_STRING);
    } else if (str_equal(t.ident, EQUATABLE_IDENT)) {
        alloc_types = ct_list_new(sizeof(type_t));
        interface_alloc_push_numeric(alloc_types);
        interface_alloc_push_kind(alloc_types, TYPE_BOOL);
        interface_alloc_push_kind(alloc_types, TYPE_STRING);
        interface_alloc_push_kind(alloc_types, TYPE_ANYPTR);
    } else if (str_equal(t.ident, NONVOID_IDENT)) {
        deny_types = ct_list_new(sizeof(type_t));
        interface_alloc_push_kind(deny_types, TYPE_VOID);
    } else {
        return;
    }

    t.interface->alloc_types = alloc_types;
    t.interface->deny_types = deny_types;
}

/**
 * 查找泛型参数的约束声明
 */
static ast_generics_param_t *find_generics_param(list_t *generics_params, char *ident) {
    if (!generics_params) {
        return NULL;
    }
    for (int i = 0; i < generics_params->length; i++) {
        ast_generics_param_t *p = ct_list_value(generics_params, i);
        if (str_equal(p->ident, ident)) {
            return p;
        }
    }
    return NULL;
}

static bool constraint_contains_interface(module_t *m, type_t interface_type, char *interface_ident, table_t *visited) {
    if (interface_type.kind != TYPE_INTERFACE) {
        return false;
    }

    if (interface_type.ident && str_equal(interface_type.ident, interface_ident)) {
        return true;
    }

    if (!interface_type.ident) {
        return false;
    }

    if (table_exist(visited, interface_type.ident)) {
        return false;
    }
    table_set(visited, interface_type.ident, (void *) true);

    symbol_t *symbol = symbol_table_get(interface_type.ident);
    if (!symbol || symbol->type != SYMBOL_TYPE) {
        return false;
    }

    ast_typedef_stmt_t *typedef_stmt = symbol->ast_value;
    if (!typedef_stmt || !typedef_stmt->impl_interfaces) {
        return false;
    }

    for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
        type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
        type_t reduced = reduction_type(m, *impl_interface);
        if (constraint_contains_interface(m, reduced, interface_ident, visited)) {
            return true;
        }
    }

    return false;
}

static bool constraint_contains_deny_type(module_t *m, type_t interface_type, type_t target_type, table_t *visited) {
    if (interface_type.kind != TYPE_INTERFACE) {
        return false;
    }

    if (interface_type.interface && interface_type.interface->deny_types &&
        interface_type.interface->deny_types->length > 0) {
        for (int i = 0; i < interface_type.interface->deny_types->length; ++i) {
            type_t *deny_type = ct_list_value(interface_type.interface->deny_types, i);
            type_t reduced_deny = reduction_type(m, *deny_type);
            if (type_compare(reduced_deny, target_type)) {
                return true;
            }
        }
    }

    if (!interface_type.ident) {
        return false;
    }

    if (table_exist(visited, interface_type.ident)) {
        return false;
    }
    table_set(visited, interface_type.ident, (void *) true);

    symbol_t *symbol = symbol_table_get(interface_type.ident);
    if (!symbol || symbol->type != SYMBOL_TYPE) {
        return false;
    }

    ast_typedef_stmt_t *typedef_stmt = symbol->ast_value;
    if (!typedef_stmt || !typedef_stmt->impl_interfaces) {
        return false;
    }

    for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
        type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
        type_t reduced = reduction_type(m, *impl_interface);
        if (constraint_contains_deny_type(m, reduced, target_type, visited)) {
            return true;
        }
    }

    return false;
}

bool generics_constraint_deny_type(module_t *m, type_t interface_type, type_t target_type) {
    if (interface_type.kind != TYPE_INTERFACE) {
        return false;
    }

    type_t target = target_type;
    if (target.kind == TYPE_REF || target.kind == TYPE_PTR) {
        target = target.ptr->value_type;
    }
    target = reduction_type(m, target);

    table_t *visited = table_new();
    return constraint_contains_deny_type(m, interface_type, target, visited);
}

static bool constraint_has_interface(module_t *m, ast_generics_param_t *param, char *interface_ident) {
    if (!param || param->constraints.elements->length == 0) {
        return false;
    }

    for (int i = 0; i < param->constraints.elements->length; i++) {
        type_t *constraint = ct_list_value(param->constraints.elements, i);
        type_t reduced = reduction_type(m, *constraint);
        table_t *visited = table_new();
        if (constraint_contains_interface(m, reduced, interface_ident, visited)) {
            return true;
        }
    }

    return false;
}

/**
 * 检查约束接口中是否声明了指定的方法名
 */
static bool constraint_has_method(module_t *m, ast_generics_param_t *param, char *method_name) {
    if (!param || param->constraints.elements->length == 0) {
        return false;
    }

    for (int i = 0; i < param->constraints.elements->length; i++) {
        type_t *constraint = ct_list_value(param->constraints.elements, i);
        type_t reduced = reduction_type(m, *constraint);
        if (reduced.kind != TYPE_INTERFACE) {
            continue;
        }

        type_interface_t *interface_type = reduced.interface;
        for (int j = 0; j < interface_type->elements->length; j++) {
            type_t *element = ct_list_value(interface_type->elements, j);
            if (element->kind == TYPE_FN && element->fn && element->fn->fn_name &&
                str_equal(element->fn->fn_name, method_name)) {
                return true;
            }
        }
    }

    return false;
}

static ast_generics_param_t *expr_generics_param(module_t *m, ast_expr_t *expr) {
    if (!m->current_fn || !m->current_fn->generics_params || !expr || expr->assert_type != AST_EXPR_IDENT) {
        return NULL;
    }

    ast_ident *ident = expr->value;
    symbol_t *s = symbol_table_get(ident->literal);
    if (!s || !s->is_local || s->type != SYMBOL_VAR) {
        return NULL;
    }

    ast_var_decl_t *var_decl = s->ast_value;
    if (!var_decl || var_decl->type.kind != TYPE_IDENT ||
        var_decl->type.ident_kind != TYPE_IDENT_GENERICS_PARAM) {
        return NULL;
    }

    return find_generics_param(m->current_fn->generics_params, var_decl->type.ident);
}

/**
 * nature 目前没有 bool-like 约束，因此泛型参数不能直接作为 bool 值使用。
 * 该检查用于 if/for 条件、!、&&、|| 等需要 bool 的上下文。
 */
static void generics_check_bool_operand(module_t *m, ast_expr_t *expr) {
    if (!expr) {
        return;
    }

    SET_LINE_COLUMN(expr);

    ast_generics_param_t *param = expr_generics_param(m, expr);
    if (param) {
        GENERIC_ASSERTF(false, "generic param '%s' cannot be used as bool value", param->ident);
        return;
    }

    switch (expr->assert_type) {
        case AST_EXPR_UNARY: {
            ast_unary_expr_t *unary = expr->value;
            if (unary->op == AST_OP_NOT) {
                generics_check_bool_operand(m, &unary->operand);
            }
            break;
        }
        case AST_EXPR_BINARY: {
            ast_binary_expr_t *binary = expr->value;
            if (binary->op == AST_OP_AND_AND || binary->op == AST_OP_OR_OR) {
                generics_check_bool_operand(m, &binary->left);
                generics_check_bool_operand(m, &binary->right);
            }
            break;
        }
        case AST_EXPR_TERNARY: {
            ast_ternary_expr_t *ternary = expr->value;
            generics_check_bool_operand(m, &ternary->condition);
            break;
        }
        default:
            break;
    }
}

static void generics_check_operator_operand(module_t *m,
                                            ast_expr_t *operand,
                                            ast_expr_op_t op,
                                            char *expect_interface) {
    if (!expect_interface) {
        return;
    }

    ast_generics_param_t *param = expr_generics_param(m, operand);
    if (!param) {
        return;
    }

    bool found = constraint_has_interface(m, param, expect_interface);
    GENERIC_ASSERTF(found,
                    "generic param '%s' cannot use operator '%s' without '%s' constraint",
                    param->ident, ast_expr_op_str[op], expect_interface);
}

static void generics_unary(module_t *m, ast_unary_expr_t *unary) {
    generics_expr(m, &unary->operand);

    if (unary->op == AST_OP_NEG || unary->op == AST_OP_BNOT) {
        generics_check_operator_operand(m, &unary->operand, unary->op, NUMERIC_IDENT);
        return;
    }

    if (unary->op == AST_OP_NOT) {
        generics_check_bool_operand(m, &unary->operand);
    }
}

/**
 * 检查二元运算符是否满足泛型约束
 * 如果操作数是泛型参数类型的变量，则该泛型参数必须有约束
 */
static void generics_binary(module_t *m, ast_binary_expr_t *binary) {
    generics_expr(m, &binary->left);
    generics_expr(m, &binary->right);

    char *expect_interface = NULL;
    switch (binary->op) {
        case AST_OP_ADD:
            expect_interface = ADDABLE_IDENT;
            break;
        case AST_OP_SUB:
        case AST_OP_MUL:
        case AST_OP_DIV:
        case AST_OP_REM:
        case AST_OP_AND:
        case AST_OP_OR:
        case AST_OP_XOR:
        case AST_OP_LSHIFT:
        case AST_OP_RSHIFT:
            expect_interface = NUMERIC_IDENT;
            break;
        case AST_OP_LT:
        case AST_OP_LE:
        case AST_OP_GT:
        case AST_OP_GE:
            expect_interface = COMPARABLE_IDENT;
            break;
        case AST_OP_EE:
        case AST_OP_NE:
            expect_interface = EQUATABLE_IDENT;
            break;
        default:
            break;
    }

    if (!expect_interface) {
        if (binary->op == AST_OP_AND_AND || binary->op == AST_OP_OR_OR) {
            generics_check_bool_operand(m, &binary->left);
            generics_check_bool_operand(m, &binary->right);
        }
        return;
    }

    generics_check_operator_operand(m, &binary->left, binary->op, expect_interface);
    generics_check_operator_operand(m, &binary->right, binary->op, expect_interface);
}

/**
 * 检查 select call 调用是否满足泛型约束
 * 处理两种情况:
 *   Case 1: a.hello() — a 是泛型参数类型的变量
 *   Case 2: T.new()   — T 是泛型类型参数作为类型直接调用静态方法
 */
static void generics_call(module_t *m, ast_call_t *call) {
    // 递归检查 call 参数中的表达式
    if (call->args) {
        for (int i = 0; i < call->args->length; i++) {
            ast_expr_t *arg = ct_list_value(call->args, i);
            generics_expr(m, arg);
        }
    }

    if (call->left.assert_type != AST_EXPR_SELECT) {
        generics_expr(m, &call->left);
        return;
    }

    ast_expr_select_t *select = call->left.value;
    char *generic_param_ident = NULL;
    list_t *generics_params = m->current_fn->generics_params;

    if (select->left.assert_type != AST_EXPR_IDENT || !generics_params) {
        // 递归检查 select->left
        generics_expr(m, &select->left);
        return;
    }

    ast_ident *ident = select->left.value;
    symbol_t *s = symbol_table_get(ident->literal);

    // Case 1: instance method call — a.hello() where a: T
    if (s && s->is_local) {
        ast_var_decl_t *var_decl = s->ast_value;
        if (var_decl && var_decl->type.kind == TYPE_IDENT &&
            var_decl->type.ident_kind == TYPE_IDENT_GENERICS_PARAM) {
            generic_param_ident = var_decl->type.ident;
        }
    }

    // Case 2: static call — T.new()
    if (!generic_param_ident && s && s->type == SYMBOL_TYPE) {
        ast_generics_param_t *p = find_generics_param(generics_params, ident->literal);
        if (p) {
            generic_param_ident = p->ident;
        }
    }

    if (!generic_param_ident) {
        generics_expr(m, &select->left);
        return;
    }

    ast_generics_param_t *param = find_generics_param(generics_params, generic_param_ident);
    if (!param) {
        return;
    }

    // 无约束则不允许 select call
    if (param->constraints.elements->length == 0) {
        GENERIC_ASSERTF(false, "generic param '%s' has no constraint declaring fn '%s'",
                        generic_param_ident, select->key);
        return;
    }

    // 检查约束接口中是否声明了该方法
    bool found = constraint_has_method(m, param, select->key);
    GENERIC_ASSERTF(found, "generic param '%s' has no constraint declaring fn '%s'",
                    generic_param_ident, select->key);
}

/**
 * 递归检查表达式
 */
static void generics_expr(module_t *m, ast_expr_t *expr) {
    if (!expr) {
        return;
    }

    SET_LINE_COLUMN(expr);

    switch (expr->assert_type) {
        case AST_CALL: {
            generics_call(m, expr->value);
            break;
        }
        case AST_EXPR_BINARY: {
            generics_binary(m, expr->value);
            break;
        }
        case AST_EXPR_UNARY: {
            generics_unary(m, expr->value);
            break;
        }
        case AST_EXPR_TERNARY: {
            ast_ternary_expr_t *ternary = expr->value;
            generics_check_bool_operand(m, &ternary->condition);
            generics_expr(m, &ternary->condition);
            generics_expr(m, &ternary->consequent);
            generics_expr(m, &ternary->alternate);
            break;
        }
        case AST_EXPR_AS: {
            ast_as_expr_t *as_expr = expr->value;
            generics_expr(m, &as_expr->src);
            break;
        }
        case AST_EXPR_IS: {
            ast_is_expr_t *is_expr = expr->value;
            if (is_expr->src) {
                generics_expr(m, is_expr->src);
            }
            break;
        }
        case AST_CATCH: {
            ast_catch_t *catch_expr = expr->value;
            generics_expr(m, &catch_expr->try_expr);
            generics_body(m, catch_expr->catch_body);
            break;
        }
        case AST_MATCH: {
            ast_match_t *match = expr->value;
            if (match->subject) {
                generics_expr(m, match->subject);
            }
            for (int i = 0; i < match->cases->count; i++) {
                ast_match_case_t *match_case = match->cases->take[i];
                generics_body(m, match_case->handle_body);
            }
            break;
        }
        case AST_EXPR_SELECT: {
            ast_expr_select_t *select_expr = expr->value;
            generics_expr(m, &select_expr->left);
            break;
        }
        case AST_EXPR_ACCESS: {
            ast_access_t *access = expr->value;
            generics_expr(m, &access->left);
            generics_expr(m, &access->key);
            break;
        }
        default:
            break;
    }
}

/**
 * 检查语句
 */
static void generics_stmt(module_t *m, ast_stmt_t *stmt) {
    SET_LINE_COLUMN(stmt);

    switch (stmt->assert_type) {
        case AST_STMT_EXPR_FAKE: {
            ast_expr_fake_stmt_t *fake = stmt->value;
            generics_expr(m, &fake->expr);
            break;
        }
        case AST_STMT_VARDEF: {
            ast_vardef_stmt_t *vardef = stmt->value;
            if (vardef->right) {
                generics_expr(m, vardef->right);
            }
            break;
        }
        case AST_STMT_ASSIGN: {
            ast_assign_stmt_t *assign = stmt->value;
            generics_expr(m, &assign->left);
            generics_expr(m, &assign->right);
            break;
        }
        case AST_CALL: {
            generics_call(m, stmt->value);
            break;
        }
        case AST_STMT_IF: {
            ast_if_stmt_t *if_stmt = stmt->value;
            generics_check_bool_operand(m, &if_stmt->condition);
            generics_expr(m, &if_stmt->condition);
            generics_body(m, if_stmt->consequent);
            generics_body(m, if_stmt->alternate);
            break;
        }
        case AST_STMT_FOR_COND: {
            ast_for_cond_stmt_t *for_cond = stmt->value;
            generics_check_bool_operand(m, &for_cond->condition);
            generics_expr(m, &for_cond->condition);
            generics_body(m, for_cond->body);
            break;
        }
        case AST_STMT_FOR_ITERATOR: {
            ast_for_iterator_stmt_t *for_iter = stmt->value;
            generics_expr(m, &for_iter->iterate);
            generics_body(m, for_iter->body);
            break;
        }
        case AST_STMT_FOR_TRADITION: {
            ast_for_tradition_stmt_t *for_trad = stmt->value;
            generics_stmt(m, for_trad->init);
            generics_check_bool_operand(m, &for_trad->cond);
            generics_expr(m, &for_trad->cond);
            generics_stmt(m, for_trad->update);
            generics_body(m, for_trad->body);
            break;
        }
        case AST_STMT_RETURN: {
            ast_return_stmt_t *ret = stmt->value;
            if (ret->expr) {
                generics_expr(m, ret->expr);
            }
            break;
        }
        case AST_STMT_RET: {
            ast_ret_stmt_t *ret = stmt->value;
            generics_expr(m, &ret->expr);
            break;
        }
        case AST_STMT_THROW: {
            ast_throw_stmt_t *throw_stmt = stmt->value;
            generics_expr(m, &throw_stmt->error);
            break;
        }
        case AST_STMT_DEFER: {
            ast_defer_stmt_t *defer_stmt = stmt->value;
            generics_body(m, defer_stmt->body);
            break;
        }
        case AST_CATCH: {
            ast_catch_t *catch_stmt = stmt->value;
            generics_expr(m, &catch_stmt->try_expr);
            generics_body(m, catch_stmt->catch_body);
            break;
        }
        case AST_STMT_TRY_CATCH: {
            ast_try_catch_stmt_t *try_catch = stmt->value;
            generics_body(m, try_catch->try_body);
            generics_body(m, try_catch->catch_body);
            break;
        }
        default:
            break;
    }
}

/**
 * 遍历 body 中的所有语句进行检查
 */
static void generics_body(module_t *m, slice_t *body) {
    if (!body) {
        return;
    }
    for (int i = 0; i < body->count; i++) {
        generics_stmt(m, body->take[i]);
    }
}

/**
 * 对泛型模板函数进行约束校验
 * 遍历函数体中的所有表达式，检查泛型参数上的调用是否满足约束
 */
void generics_fn(module_t *m, ast_fndef_t *fndef) {
    if (!fndef || !fndef->is_generics || !fndef->generics_params || !fndef->body) {
        return;
    }

    ast_fndef_t *saved_fn = m->current_fn;
    int saved_line = m->current_line;
    int saved_column = m->current_column;
    ct_stack_t *saved_stack = m->infer_type_args_stack;
    m->infer_type_args_stack = NULL;

    m->current_fn = fndef;
    m->current_line = fndef->line;
    m->current_column = fndef->column;

    generics_body(m, fndef->body);

    m->current_fn = saved_fn;
    m->current_line = saved_line;
    m->current_column = saved_column;
    m->infer_type_args_stack = saved_stack;
}

void generics(module_t *m) {
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        assert(!fndef->is_local);
        generics_fn(fndef->module, fndef);
    }
}
