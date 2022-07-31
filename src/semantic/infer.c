#include <string.h>
#include "infer.h"
#include "src/lib/error.h"
#include "src/symbol.h"
#include "analysis.h"
#include "src/debug/debug.h"
#include "src/lib/helper.h"

void infer(ast_closure_decl *closure_decl) {
    infer_line = 0;
    infer_closure_decl(closure_decl);
}

type_t infer_closure_decl(ast_closure_decl *closure_decl) {
    ast_new_fn *function_decl = closure_decl->function;

    // 类型还原
    function_decl->return_type = infer_type(function_decl->return_type);
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        function_decl->formal_params[i]->type = infer_type(function_decl->formal_params[i]->type);
    }

    // env 表达式也有类型，需要还原
    for (int i = 0; i < closure_decl->env_count; ++i) {
        infer_expr(&closure_decl->env[i]);
    }

    infer_current_init(closure_decl);
    type_t result = analysis_function_to_type(function_decl);
    result = infer_type(result);

    infer_block(&function_decl->body);

    infer_current = infer_current->parent;

    return result;
}

void infer_block(ast_block_stmt *block) {
    for (int i = 0; i < block->count; ++i) {
        infer_line = block->list[i].line;

#ifdef DEBUG_INFER
        debug_stmt("INFER", block->list[i]);
#endif

        // switch 结构导向优化
        infer_stmt(&block->list[i]);
    }
}

void infer_stmt(ast_stmt *stmt) {
    switch (stmt->type) {
        case AST_VAR_DECL: {
            infer_var_decl((ast_var_decl *) stmt->stmt);
            break;
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            infer_var_decl_assign((ast_var_decl_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_ASSIGN: {
            infer_assign((ast_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_NEW_CLOSURE: {
            infer_closure_decl((ast_closure_decl *) stmt->stmt);
            break;
        }
        case AST_CALL: {
            infer_call((ast_call *) stmt->stmt);
            break;
        }
        case AST_STMT_IF: {
            infer_if((ast_if_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_WHILE: {
            infer_while((ast_while_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_FOR_IN: {
            infer_for_in((ast_for_in_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_RETURN: {
            infer_return((ast_return_stmt *) stmt->stmt);
            break;
        }
        default:
            return;
    }
}

/**
 * 表达式推断核心逻辑
 * @param expr
 * @return
 */
type_t infer_expr(ast_expr *expr) {
    type_t type;
    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            type = infer_binary((ast_binary_expr *) expr->expr);
            break;
        }
        case AST_EXPR_UNARY: {
            type = infer_unary((ast_unary_expr *) expr->expr);
            break;
        }
        case AST_EXPR_IDENT: {
            type = infer_ident(((ast_ident *) expr->expr)->literal);
            break;
        }
        case AST_EXPR_NEW_ARRAY: {
            type = infer_new_array((ast_new_list *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_MAP: {
            type = infer_new_map((ast_new_map *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_STRUCT: {
            type = infer_new_struct((ast_new_struct *) expr->expr);
            break;
        }
        case AST_EXPR_ACCESS: {
            // 需要做类型改写，所以传递整个表达式
            type = infer_access(expr);
            break;
        }
        case AST_EXPR_SELECT_PROPERTY: {
            type = infer_select_property((ast_select_property *) expr->expr);
            break;
        }
        case AST_CALL: {
            type = infer_call((ast_call *) expr->expr);
            break;
        }
        case AST_NEW_CLOSURE: {
            type = infer_closure_decl((ast_closure_decl *) expr->expr);
            break;
        }
        case AST_EXPR_LITERAL: {
            type = infer_literal((ast_literal *) expr->expr);
            break;
        }
        case AST_EXPR_ACCESS_ENV: {
            type = infer_access_env((ast_access_env *) expr->expr);
            break;
        }
        default: {
            error_exit("[infer_expr]unknown expr %v", expr->assert_type);
            exit(0);
        }
    }

    expr->type = type;

    return type;
}

/**
 * @param expr
 * @return
 */
type_t infer_binary(ast_binary_expr *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    type_t left_type = infer_expr(&expr->left);
    type_t right_type = infer_expr(&expr->right);

    if (left_type.base != TYPE_INT && left_type.base != TYPE_FLOAT) {
        error_printf(infer_line, "invalid operation: %s, expr type must be int or float, cannot '%s' type",
                     ast_expr_operator_to_string[expr->operator],
                     type_to_string[left_type.base]);
    }

    if (right_type.base != TYPE_INT && right_type.base != TYPE_FLOAT) {
        error_printf(infer_line, "invalid operation: %s,  expr type must be int or float, cannot '%s' type",
                     ast_expr_operator_to_string[expr->operator],
                     type_to_string[right_type.base]);
    }

    switch (expr->operator) {
        case AST_EXPR_OPERATOR_ADD:
        case AST_EXPR_OPERATOR_SUB:
        case AST_EXPR_OPERATOR_MUL:
        case AST_EXPR_OPERATOR_DIV: {
            return left_type;
        }
        case AST_EXPR_OPERATOR_LT:
        case AST_EXPR_OPERATOR_LTE:
        case AST_EXPR_OPERATOR_GT:
        case AST_EXPR_OPERATOR_GTE:
        case AST_EXPR_OPERATOR_EQ_EQ:
        case AST_EXPR_OPERATOR_NOT_EQ: {
            return type_new_base(TYPE_BOOL);
        }
        default: {
            error_exit("unknown operator type");
            exit(0);
        }
    }
}

/**
 * unary
 * @param expr
 * @return
 */
type_t infer_unary(ast_unary_expr *expr) {
    type_t operand_type = infer_expr(&expr->operand);
    if (expr->operator == AST_EXPR_OPERATOR_NOT && operand_type.base != TYPE_BOOL) {
        error_exit("!expr, expr must be bool type");
    }

    if ((expr->operator == AST_EXPR_OPERATOR_NEG) && operand_type.base != TYPE_INT
        && operand_type.base != TYPE_FLOAT) {
        error_exit("!expr, expr must be int or float");
    }

    return operand_type;
}

/**
 * func main() {
 *  a = 1
 *  fmt.Println(a)
 *}
 * var a int
 *
 * 参考 golang，声明是可能在使用之后的
 * @param expr
 * @return
 */
type_t infer_ident(string unique_ident) {
    symbol_t *symbol = symbol_table_get(unique_ident);
    if (symbol->type == SYMBOL_TYPE_VAR) {
        // 类型还原，并回写到 local_ident
        ast_var_decl *var_decl = symbol->decl;
        var_decl->type = infer_type(var_decl->type);
        return var_decl->type;
    }

    if (symbol->type == SYMBOL_TYPE_FN) {
        ast_new_fn *new_fn = symbol->decl;
        return infer_type(analysis_function_to_type(new_fn));
    }


    error_exit("ident type exception");
    exit(0);
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
type_t infer_new_array(ast_new_list *new_list) {
    type_t result = {
            .is_origin = true,
            .base = TYPE_ARRAY,
    };
    ast_array_decl *decl = malloc(sizeof(ast_array_decl));
    decl->ast_type = type_new_base(TYPE_UNKNOWN); // unknown 可以适配任何类型
    decl->count = new_list->count;

    for (int i = 0; i < new_list->count; ++i) {
        type_t item_type = infer_expr(&new_list->values[i]);
        if (decl->ast_type.base == TYPE_UNKNOWN) {
            // 数组已经添加了初始化值，可以添加一种有值进行类型推导了
            decl->ast_type = item_type;
        } else {
            if (!infer_compare_type(item_type, decl->ast_type)) {
                // 出现了多种类型，无法推导出具体的类型，可以暂定为 any, 并退出右值类型推导
                decl->ast_type = type_new_base(TYPE_ANY);
                break;
            }
        }
    }
    result.value = decl;
    new_list->ast_type = result;

    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param new_map
 * @return
 */
type_t infer_new_map(ast_new_map *new_map) {
    type_t result = {
            .is_origin = true,
            .base = TYPE_MAP,
    };
    ast_map_decl *map_decl = NEW(ast_map_decl);
    map_decl->key_type = type_new_base(TYPE_UNKNOWN);
    map_decl->value_type = type_new_base(TYPE_UNKNOWN);
    for (int i = 0; i < new_map->count; ++i) {
        type_t key_type = infer_expr(&new_map->values[i].key);
        type_t value_type = infer_expr(&new_map->values[i].value);

        // key
        if (map_decl->key_type.base == TYPE_UNKNOWN) {
            map_decl->key_type = key_type;
        } else {
            if (!infer_compare_type(key_type, map_decl->key_type)) {
                map_decl->key_type = type_new_base(TYPE_ANY);
                break;
            }
        }

        // value
        if (map_decl->value_type.base == TYPE_UNKNOWN) {
            map_decl->value_type = value_type;
        } else {
            if (!infer_compare_type(value_type, map_decl->value_type)) {
                map_decl->value_type = type_new_base(TYPE_ANY);
                break;
            }
        }
    }

    // 冗余
    new_map->key_type = map_decl->key_type;
    new_map->value_type = map_decl->value_type;

    result.value = map_decl;

    return result;
}

/**
 * person {
 *  age = 1
 * }
 *
 * struct {
 *   int age
 * } {
 *  age = 1
 * }
 * @param new_struct
 * @return
 */
type_t infer_new_struct(ast_new_struct *new_struct) {
    // 类型还原, struct ident 一定会被还原回 struct 原始结构
    // 如果本身已经是 struct 结构，那么期中的 struct property type 也会被还原成原始类型
    new_struct->type = infer_type(new_struct->type);

    if (new_struct->type.base != TYPE_STRUCT) {
        error_printf(infer_line, "ident not struct");
    }

    ast_struct_decl *struct_decl = (ast_struct_decl *) new_struct->type.value;
    for (int i = 0; i < new_struct->count; ++i) {
        ast_struct_property struct_property = new_struct->list[i];

        // struct_decl 已经是被还原过的类型了
        type_t expect_type = infer_struct_property_type(struct_decl, struct_property.key);
        type_t actual_type = infer_expr(&struct_property.value);

        // expect type type 并不允许为 var
        if (!infer_compare_type(actual_type, expect_type)) {
            error_printf(infer_line, "property '%s' expect '%s' type, cannot assign '%s' type",
                         struct_property.key,
                         type_to_string[expect_type.base],
                         type_to_string[actual_type.base]);
        }
    }

    return new_struct->type;
}

/**
 * @param expr
 * @return
 */
type_t infer_access(ast_expr *expr) {
    type_t result;
    ast_access *access = expr->expr;
    type_t left_type = infer_expr(&access->left);
    type_t key_type = infer_expr(&access->key);

    if (left_type.base == TYPE_MAP) {
        ast_access_map *access_map = malloc(sizeof(ast_access_map));
        ast_map_decl *map_decl = left_type.value;

        // 参数改写
        access_map->left = access->left;
        access_map->key = access->key;

        // access_map 冗余字段处理
        access_map->key_type = map_decl->key_type;
        access_map->value_type = map_decl->value_type;
        expr->assert_type = AST_EXPR_ACCESS_MAP;
        expr->expr = access_map;


        // 返回值
        result = map_decl->value_type;
    } else if (left_type.base == TYPE_ARRAY) {
        if (key_type.base != TYPE_INT) {
            error_printf(infer_line,
                         "access list error, index expr type must by int, cannot '%s'",
                         type_to_string[key_type.base]);
        }

        ast_access_list *access_list = malloc(sizeof(ast_access_map));
        ast_array_decl *list_decl = left_type.value;

        // 参数改写
        access_list->left = access->left;
        access_list->index = access->key;
        access_list->type = list_decl->ast_type;
        expr->assert_type = AST_EXPR_ACCESS_LIST;
        expr->expr = access_list;

        result = list_decl->ast_type;
    } else {
        error_printf(infer_line, "expr type must map or list, cannot '%s'", type_to_string[left_type.base]);
        exit(0);
    };

    return result;
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * @param select_property
 * @return
 */
type_t infer_select_property(ast_select_property *select_property) {
    type_t left_type = infer_expr(&select_property->left);
    if (left_type.base != TYPE_STRUCT) {
        error_printf(infer_line, "[infer_select_property]expr not struct, cannot select property");
        exit(0);
    }

    ast_struct_decl *struct_decl = left_type.value;

    // 冗余结构到 select 表达式，便于计算 size
    select_property->struct_decl = struct_decl;

    for (int i = 0; i < struct_decl->count; ++i) {
        if (strcmp(struct_decl->list[i].key, select_property->property) == 0) {
            select_property->struct_property = &struct_decl->list[i];

            return struct_decl->list[i].type;
        }
    }

    error_printf(infer_line, "cannot get struct property '%s'", select_property->property);
    exit(0);
}

/**
 * @param call
 * @return
 */
type_t infer_call(ast_call *call) {
    type_t result;

    // 实参推导
    type_t actual_types[call->actual_param_count];
    for (int i = 0; i < call->actual_param_count; ++i) {
        actual_types[i] = infer_expr(&call->actual_params[i]);
    }

    if (call->left.assert_type == AST_EXPR_IDENT) {
        ast_ident *ident = call->left.expr;
        if (is_print_symbol(ident->literal)) {
            return type_new_base(TYPE_FN);
        }
    }

    // 左值符号推导(TODO 外部符号暂时偷懒没有进行具体的推导)
    type_t left_type = infer_expr(&call->left);
    if (left_type.base != TYPE_FN) {
        error_printf(infer_line, "[infer_call]expression not function type(%s), cannot call",
                     type_to_string[left_type.base]);
    }

    type_fn_t *type_fn = left_type.value;
    if (type_fn->formal_param_count != call->actual_param_count) {
        error_printf(infer_line, "[infer_call] function param count not match");
        exit(0);
    }

    // call param check
    for (int i = 0; i < type_fn->formal_param_count; ++i) {
        type_t t = type_fn->formal_param_types[i];
        type_t actual_param_type = actual_types[i];
        if (!infer_compare_type(t, actual_param_type)) {
            error_printf(infer_line, "[infer_call] call param[%d] type error, expect '%s' type, actual '%s' type",
                         i,
                         type_to_string[t.base],
                         type_to_string[actual_param_type.base]);
        }
    }

    result = type_fn->return_type;
    return result;
}

/**
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(ast_var_decl *var_decl) {
    type_t type = var_decl->type;
    if (type.base == TYPE_UNKNOWN || type.base == TYPE_VOID || type.base == TYPE_NULL) {
        error_printf(infer_line, "variable declarations cannot use '%s'", type_to_string[type.base]);
    }
}

/**
 * 仅使用了 var 关键字的地方才需要进行类型推断，好像就这里需要！
 * var a = 1
 * var b = 2.0
 * var c = true
 * var d = void (int a, int b) {}
 * var e = [1, 2, 3] // ?
 * var f = {"a": 1, "b": 2} // ?
 * var h = call();
 */
void infer_var_decl_assign(ast_var_decl_assign_stmt *stmt) {
    type_t expr_type = infer_expr(&stmt->expr);

    // 类型推断(不需要再比较类型是否一致)
    if (stmt->var_decl->type.base == TYPE_UNKNOWN) {
        if (!infer_var_type_can_confirm(expr_type)) {
            error_printf(infer_line, "type inference error, right expr type is not clear");
            return;
        }
        stmt->var_decl->type = expr_type;
        return;
    }

    // 左值类型还原()
    stmt->var_decl->type = infer_type(stmt->var_decl->type);

    // 判断类型是否一致 compare
    if (!infer_compare_type(stmt->var_decl->type, expr_type)) {
        error_type_not_match(infer_line);
    }
}

/**
 * @param stmt
 */
void infer_assign(ast_assign_stmt *stmt) {
    type_t left_type = infer_expr(&stmt->left);
    type_t right_type = infer_expr(&stmt->right);

    if (!infer_compare_type(left_type, right_type)) {
        error_type_not_match(infer_line);
    }
}

void infer_if(ast_if_stmt *stmt) {
    type_t condition_type = infer_expr(&stmt->condition);
    if (condition_type.base != TYPE_BOOL) {
        error_exit("if stmt condition must bool");
    }

    infer_block(&stmt->consequent);
    infer_block(&stmt->alternate);
}

void infer_while(ast_while_stmt *stmt) {
    type_t condition_type = infer_expr(&stmt->condition);
    if (condition_type.base != TYPE_BOOL) {
        error_exit("while stmt condition must bool");
    }
    infer_block(&stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
void infer_for_in(ast_for_in_stmt *stmt) {
    // 经过 infer_expr 的类型一定是已经被还原过的
    type_t iterate_type = infer_expr(&stmt->iterate);
    if (iterate_type.base != TYPE_MAP && iterate_type.base != TYPE_ARRAY) {
        error_printf(infer_line,
                     "for in iterate type must be map/list, actual:(%s)",
                     type_to_string[iterate_type.base]);
    }

    // 类型推断
    ast_var_decl *key_decl = stmt->gen_key;
    ast_var_decl *value_decl = stmt->gen_value;
    if (iterate_type.base == TYPE_MAP) {
        ast_map_decl *map_decl = iterate_type.value;
        key_decl->type = map_decl->key_type;
        value_decl->type = map_decl->value_type;
    } else {
        ast_array_decl *list_decl = iterate_type.value;
        key_decl->type = type_new_base(TYPE_INT);
        value_decl->type = list_decl->ast_type;

    }

    infer_block(&stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure 里面？
 * @param stmt
 */
void infer_return(ast_return_stmt *stmt) {
    type_t return_type = type_new_base(TYPE_VOID);
    if (stmt->expr != NULL) {
        return_type = infer_expr(stmt->expr);
    }

    type_t expect_type = infer_current->closure_decl->function->return_type;
    if (!infer_compare_type(expect_type, return_type)) {
        error_printf(infer_line, "return type(%s) error,expect '%s' type",
                     type_to_string[return_type.base],
                     type_to_string[expect_type.base]);
    }
}

infer_closure *infer_current_init(ast_closure_decl *closure_decl) {
    infer_closure *new = malloc(sizeof(infer_closure));
    new->closure_decl = closure_decl;
    new->parent = infer_current;

    infer_current = new;
    return infer_current;
}

/**
 * 比较前都已经还原为原始类型了
 * @param left
 * @param right
 * @return
 */
bool infer_compare_type(type_t left, type_t right) {
    if (!left.is_origin || !right.is_origin) {
        error_printf(infer_line, "type not origin, left: '%s', right: '%s'",
                     type_to_string[left.base],
                     type_to_string[right.base]);
        return false;
    }

    if (left.base == TYPE_BUILTIN_ANY || right.base == TYPE_BUILTIN_ANY) {
        return true;
    }

    if (left.base == TYPE_UNKNOWN && right.base == TYPE_UNKNOWN) {
        error_printf(infer_line, "type cannot infer");
        return false;
    }

    if (left.base == TYPE_ANY) {
        return true;
    }

    if (left.base != right.base) {
        return false;
    }

    if (left.base == TYPE_MAP) {
        ast_map_decl *left_map_decl = left.value;
        ast_map_decl *right_map_decl = right.value;

        if (!infer_compare_type(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!infer_compare_type(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (left.base == TYPE_ARRAY) {
        ast_array_decl *left_list_decl = left.value;
        ast_array_decl *right_list_decl = right.value;
        if (right_list_decl->ast_type.base == TYPE_UNKNOWN) {
            // 但是这样在 compiler_array 时将完全不知道将右值初始化多大空间的 capacity
            // 但是其可以完全继承左值, 左值进入到该方法之前已经经过了类型推断，这里肯定不是 var 了
            right_list_decl->ast_type = left_list_decl->ast_type;
            right_list_decl->count = left_list_decl->count;
            return true;
        }
        // 类型不相同
        if (!infer_compare_type(left_list_decl->ast_type, right_list_decl->ast_type)) {
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

    if (left.base == TYPE_FN) {
        type_fn_t *left_type_fn = left.value;
        type_fn_t *right_type_fn = right.value;
        if (!infer_compare_type(left_type_fn->return_type, right_type_fn->return_type)) {
            return false;
        }

        if (left_type_fn->formal_param_count != right_type_fn->formal_param_count) {
            return false;
        }

        for (int i = 0; i < left_type_fn->formal_param_count; ++i) {
            if (!infer_compare_type(
                    left_type_fn->formal_param_types[i],
                    right_type_fn->formal_param_types[i]
            )) {
                return false;
            }
        }
    }

    if (left.base == TYPE_STRUCT) {
        ast_struct_decl *left_struct_decl = left.value;
        ast_struct_decl *right_struct_decl = right.value;
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

            // type 比较
            if (!infer_compare_type(
                    left_struct_decl->list[i].type,
                    right_struct_decl->list[i].type
            )) {
                return false;
            }
        }
    }

    return true;
}

/**
 * struct 允许顺序不通，但是 key 和 type 需要相同，在还原时需要根据 key 进行排序
 * @param type
 * @return
 */
type_t infer_type(type_t type) {
    if (type.is_origin) {
        return type;
    }

    type.is_origin = true;
    if (type.base == TYPE_INT || type.base == TYPE_BOOL || type.base == TYPE_FLOAT
        || type.base == TYPE_STRING
        || type.base == TYPE_ANY
        || type.base == TYPE_VOID) {
        return type;
    }

    if (type.base == TYPE_STRUCT) {
        ast_struct_decl *struct_decl = type.value;
        infer_sort_struct_decl(struct_decl); // 按照 key 排序

        // 对 struct 的每个属性都进行还原
        for (int i = 0; i < struct_decl->count; ++i) {
            struct_decl->list[i].type = infer_type(struct_decl->list[i].type);
        }

        return type;
    }

    // type foo = int, 'foo' is type_dec_ident
    if (type.base == TYPE_DECL_IDENT) {
        return infer_type_decl_ident((ast_ident *) type.value);
    }

    if (type.base == TYPE_MAP) {
        ast_map_decl *map_decl = type.value;
        map_decl->key_type = infer_type(map_decl->key_type);
        map_decl->value_type = infer_type(map_decl->value_type);
        return type;
    }

    if (type.base == TYPE_ARRAY) {
        ast_array_decl *list_decl = type.value;
        list_decl->ast_type = infer_type(list_decl->ast_type);
        return type;
    }

    if (type.base == TYPE_FN) {
        type_fn_t *type_fn = type.value;
        type_fn->return_type = infer_type(type_fn->return_type);
        for (int i = 0; i < type_fn->formal_param_count; ++i) {
            type_fn->formal_param_types[i] = infer_type(type_fn->formal_param_types[i]);
        }

        return type;
    }

    error_printf(infer_line, "cannot parser type %s", type_to_string[type.base]);
    exit(0);
}

type_t infer_type_decl_ident(ast_ident *ident) {
    // 符号表找到相关类型
    symbol_t *symbol = symbol_table_get(ident->literal);
    if (symbol->type != SYMBOL_TYPE_CUSTOM) {
        error_printf(infer_line, "'%s' is not a type", symbol->ident);
    }

    ast_type_decl_stmt *type_decl_stmt = symbol->decl;

    // type_decl_stmt->ident 为自定义类型名称
    // type_decl_stmt->type 为引用的原始类型 int,my_int,struct....， 此时如果只有一次引用的话，实际上已经还原回去了
    type_t origin_type = infer_type(type_decl_stmt->type);

    return origin_type;
}

/**
 * 返回对应的 type
 * @param struct_decl
 * @param ident
 * @return
 */
type_t infer_struct_property_type(ast_struct_decl *struct_decl, char *ident) {
    for (int i = 0; i < struct_decl->count; ++i) {
        if (strcmp(struct_decl->list[i].key, ident) == 0) {
            return struct_decl->list[i].type;
        }
    }

    error_printf(infer_line, "not found struct property '%s'", ident);
    exit(0);
}

/**
 * 对 struct list 按照 key 进行排序,选择排序
 * @param struct_decl
 */
void infer_sort_struct_decl(ast_struct_decl *struct_decl) {
    for (int i = 0; i < struct_decl->count; ++i) {
        for (int j = i + 1; j < struct_decl->count; ++j) {
            if (strcmp(struct_decl->list[i].key, struct_decl->list[j].key) > 0) {
                // 交换
                ast_struct_property temp = struct_decl->list[i];
                struct_decl->list[i] = struct_decl->list[j];
                struct_decl->list[j] = temp;
            }
        }
    }
}

type_t infer_literal(ast_literal *literal) {
    return type_new_base(literal->type);
}

/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param right
 * @return
 */
bool infer_var_type_can_confirm(type_t right) {
    if (right.base == TYPE_UNKNOWN) {
        return false;
    }

    // var a = []  这样就完全不知道是个啥。。。
    if (right.base == TYPE_ARRAY) {
        ast_array_decl *list_decl = right.value;
        if (list_decl->ast_type.base == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (right.base == TYPE_MAP) {
        ast_map_decl *map_decl = right.value;
        if (map_decl->key_type.base == TYPE_UNKNOWN) {
            return false;
        }
        if (map_decl->value_type.base == TYPE_UNKNOWN) {
            return false;
        }
    }

    return true;
}

type_t infer_access_env(ast_access_env *expr) {
    type_t t = infer_ident(expr->unique_ident);
    return t;
}

