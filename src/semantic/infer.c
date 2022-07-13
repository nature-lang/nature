#include <string.h>
#include "infer.h"
#include "src/lib/error.h"
#include "src/symbol.h"
#include "analysis.h"
#include "src/debug/debug.h"

void infer(ast_closure_decl *closure_decl) {
    infer_line = 0;
    infer_closure_decl(closure_decl);
}

ast_type infer_closure_decl(ast_closure_decl *closure_decl) {
    ast_function_decl *function_decl = closure_decl->function;

    // 类型还原
    function_decl->return_type = infer_type(function_decl->return_type);
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        function_decl->formal_params[i]->type = infer_type(function_decl->formal_params[i]->type);
    }

    infer_current_init(closure_decl);
    ast_type result = analysis_function_to_type(function_decl);
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
        case AST_CLOSURE_DECL: {
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
ast_type infer_expr(ast_expr *expr) {
    ast_type type;
    switch (expr->type) {
        case AST_EXPR_BINARY: {
            type = infer_binary((ast_binary_expr *) expr->expr);
            break;
        }
        case AST_EXPR_UNARY: {
            type = infer_unary((ast_unary_expr *) expr->expr);
            break;
        }
        case AST_EXPR_IDENT: {
            type = infer_ident((ast_ident *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_LIST: {
            type = infer_new_list((ast_new_list *) expr->expr);
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
        case AST_CLOSURE_DECL: {
            type = infer_closure_decl((ast_closure_decl *) expr->expr);
            break;
        }
        case AST_EXPR_LITERAL: {
            type = infer_literal((ast_literal *) expr->expr);
            break;
        }
        default: {
            error_exit("unknown expr");
            exit(0);
        }
    }

    expr->data_type = type;

    return type;
}

/**
 * @param expr
 * @return
 */
ast_type infer_binary(ast_binary_expr *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    ast_type left_type = infer_expr(&expr->left);
    ast_type right_type = infer_expr(&expr->right);

    if (left_type.category != TYPE_INT && left_type.category != TYPE_FLOAT) {
        error_printf(infer_line, "invalid operation: %s, expr type must be int or float, cannot '%s' type",
                     ast_expr_operator_to_string[expr->operator],
                     type_to_string[left_type.category]);
    }

    if (right_type.category != TYPE_INT && right_type.category != TYPE_FLOAT) {
        error_printf(infer_line, "invalid operation: %s,  expr type must be int or float, cannot '%s' type",
                     ast_expr_operator_to_string[expr->operator],
                     type_to_string[right_type.category]);
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
            return ast_new_simple_type(TYPE_BOOL);
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
ast_type infer_unary(ast_unary_expr *expr) {
    ast_type operand_type = infer_expr(&expr->operand);
    if (expr->operator == AST_EXPR_OPERATOR_NOT && operand_type.category != TYPE_BOOL) {
        error_exit("!expr, expr must be bool type");
    }

    if ((expr->operator == AST_EXPR_OPERATOR_MINUS) && operand_type.category != TYPE_INT
        && operand_type.category != TYPE_FLOAT) {
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
ast_type infer_ident(ast_ident *expr) {
    string unique_ident = expr->literal;
    analysis_local_ident *local_ident = table_get(symbol_ident_table, unique_ident);
    if (local_ident->belong != SYMBOL_TYPE_VAR) {
        error_message(infer_line, "ident type exception");
    }

    // 类型还原，并回写到 local_ident
    ast_var_decl *var_decl = local_ident->decl;
    var_decl->type = infer_type(var_decl->type);

    return var_decl->type;
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
ast_type infer_new_list(ast_new_list *new_list) {
    ast_type result = {
            .is_origin = true,
            .category = TYPE_LIST,
    };
    ast_list_decl *list_decl = malloc(sizeof(ast_list_decl));
    list_decl->type = ast_new_simple_type(TYPE_VAR);

    for (int i = 0; i < new_list->count; ++i) {
        ast_type item_type = infer_expr(&new_list->values[i]);
        if (list_decl->type.category == TYPE_VAR) {
            // 初始化赋值
            list_decl->type = item_type;
        } else {
            if (!infer_compare_type(item_type, list_decl->type)) {
                list_decl->type = ast_new_simple_type(TYPE_ANY);
                break;
            }
        }
    }

    result.value = list_decl;

    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param new_map
 * @return
 */
ast_type infer_new_map(ast_new_map *new_map) {
    ast_type result = {
            .is_origin = true,
            .category = TYPE_MAP,
    };
    ast_map_decl *map_decl = NEW(ast_map_decl);
    map_decl->key_type = ast_new_simple_type(TYPE_VAR);
    map_decl->value_type = ast_new_simple_type(TYPE_VAR);
    for (int i = 0; i < new_map->count; ++i) {
        ast_type key_type = infer_expr(&new_map->values[i].key);
        ast_type value_type = infer_expr(&new_map->values[i].value);

        // key
        if (map_decl->key_type.category == TYPE_VAR) {
            map_decl->key_type = key_type;
        } else {
            if (!infer_compare_type(key_type, map_decl->key_type)) {
                map_decl->key_type = ast_new_simple_type(TYPE_ANY);
                break;
            }
        }

        // value
        if (map_decl->value_type.category == TYPE_VAR) {
            map_decl->value_type = value_type;
        } else {
            if (!infer_compare_type(value_type, map_decl->value_type)) {
                map_decl->value_type = ast_new_simple_type(TYPE_ANY);
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
ast_type infer_new_struct(ast_new_struct *new_struct) {
    // 类型还原, struct ident 一定会被还原回 struct 原始结构
    // 如果本身已经是 struct 结构，那么期中的 struct property type 也会被还原成原始类型
    new_struct->type = infer_type(new_struct->type);

    if (new_struct->type.category != TYPE_STRUCT) {
        error_printf(infer_line, "ident not struct");
    }

    ast_struct_decl *struct_decl = (ast_struct_decl *) new_struct->type.value;
    for (int i = 0; i < new_struct->count; ++i) {
        ast_struct_property struct_property = new_struct->list[i];

        // struct_decl 已经是被还原过的类型了
        ast_type expect_type = infer_struct_property_type(struct_decl, struct_property.key);
        ast_type actual_type = infer_expr(&struct_property.value);

        // expect type type 并不允许为 var
        if (!infer_compare_type(actual_type, expect_type)) {
            error_printf(infer_line, "property '%s' expect '%s' type, cannot assign '%s' type",
                         struct_property.key,
                         type_to_string[expect_type.category],
                         type_to_string[actual_type.category]);
        }
    }

    return new_struct->type;
}

/**
 * @param expr
 * @return
 */
ast_type infer_access(ast_expr *expr) {
    ast_type result;
    ast_access *access = expr->expr;
    ast_type left_type = infer_expr(&access->left);
    ast_type key_type = infer_expr(&access->key);

    if (left_type.category == TYPE_MAP) {
        ast_access_map *access_map = malloc(sizeof(ast_access_map));
        ast_map_decl *map_decl = left_type.value;

        // 参数改写
        access_map->left = access->left;
        access_map->key = access->key;

        // access_map 冗余字段处理
        access_map->key_type = map_decl->key_type;
        access_map->value_type = map_decl->value_type;
        expr->type = AST_EXPR_ACCESS_MAP;
        expr->expr = access_map;


        // 返回值
        result = map_decl->value_type;
    } else if (left_type.category == TYPE_LIST) {
        if (key_type.category != TYPE_INT) {
            error_printf(infer_line,
                         "access list error, index expr type must by int, cannot '%s'",
                         type_to_string[key_type.category]);
        }

        ast_access_list *access_list = malloc(sizeof(ast_access_map));
        ast_list_decl *list_decl = left_type.value;

        // 参数改写
        access_list->left = access->left;
        access_list->index = access->key;
        access_list->type = list_decl->type;
        expr->type = AST_EXPR_ACCESS_LIST;
        expr->expr = access_list;

        result = list_decl->type;
    } else {
        error_printf(infer_line, "expr type must map or list, cannot '%s'", type_to_string[left_type.category]);
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
ast_type infer_select_property(ast_select_property *select_property) {
    ast_type left_type = infer_expr(&select_property->left);

    if (left_type.category != TYPE_STRUCT) {
        error_printf(infer_line, "expr not struct, cannot select property");
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
ast_type infer_call(ast_call *call) {
    ast_type result;

    ast_type left_type = infer_expr(&call->left);

    if (left_type.category != TYPE_FUNCTION) {
        error_printf(infer_line, "expression not function type(%s), cannot call", type_to_string[left_type.category]);
    }

    ast_function_type_decl *function_type_decl = left_type.value;

    if (function_type_decl->formal_param_count != call->actual_param_count) {
        error_printf(infer_line, "function param count not match");
        exit(0);
    }

    // call param check
    for (int i = 0; i < function_type_decl->formal_param_count; ++i) {
        ast_var_decl *formal_param = function_type_decl->formal_params[i];

        ast_type actual_param_type = infer_expr(&call->actual_params[i]);
        if (!infer_compare_type(formal_param->type, actual_param_type)) {
            error_printf(infer_line, "call param[%d] type error, expect '%s' type, actual '%s' type",
                         i,
                         type_to_string[formal_param->type.category],
                         type_to_string[actual_param_type.category]);
        }
    }

    result = function_type_decl->return_type;
    return result;
}

/**
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(ast_var_decl *var_decl) {
    ast_type type = var_decl->type;
    if (type.category == TYPE_VAR || type.category == TYPE_VOID || type.category == TYPE_NULL) {
        error_printf(infer_line, "variable declarations cannot use '%s'", type_to_string[type.category]);
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
    ast_type expr_type = infer_expr(&stmt->expr);

    // 类型推断(不需要再比较类型是否一致)
    if (stmt->var_decl->type.category == TYPE_VAR) {
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
    ast_type left_type = infer_expr(&stmt->left);
    ast_type right_type = infer_expr(&stmt->right);

    if (!infer_compare_type(left_type, right_type)) {
        error_type_not_match(infer_line);
    }
}

void infer_if(ast_if_stmt *stmt) {
    ast_type condition_type = infer_expr(&stmt->condition);
    if (condition_type.category != TYPE_BOOL) {
        error_exit("if stmt condition must bool");
    }

    infer_block(&stmt->consequent);
    infer_block(&stmt->alternate);
}

void infer_while(ast_while_stmt *stmt) {
    ast_type condition_type = infer_expr(&stmt->condition);
    if (condition_type.category != TYPE_BOOL) {
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
    ast_type iterate_type = infer_expr(&stmt->iterate);
    if (iterate_type.category != TYPE_MAP && iterate_type.category != TYPE_LIST) {
        error_printf(infer_line,
                     "for in iterate type must be map/list, actual:(%s)",
                     type_to_string[iterate_type.category]);
    }

    // 类型推断
    ast_var_decl *key_decl = stmt->gen_key;
    ast_var_decl *value_decl = stmt->gen_value;
    if (iterate_type.category == TYPE_MAP) {
        ast_map_decl *map_decl = iterate_type.value;
        key_decl->type = map_decl->key_type;
        value_decl->type = map_decl->value_type;
    } else {
        ast_list_decl *list_decl = iterate_type.value;
        key_decl->type = ast_new_simple_type(TYPE_INT);
        value_decl->type = list_decl->type;

    }

    infer_block(&stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure 里面？
 * @param stmt
 */
void infer_return(ast_return_stmt *stmt) {
    ast_type return_type = infer_expr(&stmt->expr);

    ast_type expect_type = infer_current->closure_decl->function->return_type;
    if (!infer_compare_type(expect_type, return_type)) {
        error_printf(infer_line, "return type(%s) error,expect '%s' type",
                     type_to_string[return_type.category],
                     type_to_string[expect_type.category]);
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
bool infer_compare_type(ast_type left, ast_type right) {
    if (!left.is_origin || !right.is_origin) {
        error_printf(infer_line, "type not origin, left: '%s', right: '%s'",
                     type_to_string[left.category],
                     type_to_string[right.category]);
        return false;
    }

    if (left.category == TYPE_VAR && right.category == TYPE_VAR) {
        error_printf(infer_line, "type cannot infer");
        return false;
    }

    if (left.category == TYPE_ANY) {
        return true;
    }

    if (left.category != right.category) {
        return false;
    }

    if (left.category == TYPE_MAP) {
        ast_map_decl *left_map_decl = left.value;
        ast_map_decl *right_map_decl = right.value;

        if (!infer_compare_type(left_map_decl->key_type, right_map_decl->key_type)) {
            return false;
        }

        if (!infer_compare_type(left_map_decl->value_type, right_map_decl->value_type)) {
            return false;
        }
    }

    if (left.category == TYPE_LIST) {
        ast_list_decl *left_list_decl = left.value;
        ast_list_decl *right_list_decl = right.value;
        if (!infer_compare_type(left_list_decl->type, right_list_decl->type)) {
            return false;
        }
    }

    if (left.category == TYPE_FUNCTION) {
        ast_function_type_decl *left_function = left.value;
        ast_function_type_decl *right_function = right.value;
        if (!infer_compare_type(left_function->return_type, right_function->return_type)) {
            return false;
        }

        if (left_function->formal_param_count != right_function->formal_param_count) {
            return false;
        }

        for (int i = 0; i < left_function->formal_param_count; ++i) {
            if (!infer_compare_type(
                    left_function->formal_params[i]->type,
                    right_function->formal_params[i]->type
            )) {
                return false;
            }
        }
    }

    if (left.category == TYPE_STRUCT) {
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
ast_type infer_type(ast_type type) {
    if (type.is_origin) {
        return type;
    }

    type.is_origin = true;
    if (type.category == TYPE_INT || type.category == TYPE_BOOL || type.category == TYPE_FLOAT
        || type.category == TYPE_STRING
        || type.category == TYPE_ANY
        || type.category == TYPE_VOID) {
        return type;
    }

    if (type.category == TYPE_STRUCT) {
        ast_struct_decl *struct_decl = type.value;
        infer_sort_struct_decl(struct_decl); // 按照 key 排序

        // 对 struct 的每个属性都进行还原
        for (int i = 0; i < struct_decl->count; ++i) {
            struct_decl->list[i].type = infer_type(struct_decl->list[i].type);
        }

        return type;
    }

    if (type.category == TYPE_DECL_IDENT) {
        return infer_type_decl_ident((ast_ident *) type.value);
    }

    if (type.category == TYPE_MAP) {
        ast_map_decl *map_decl = type.value;
        map_decl->key_type = infer_type(map_decl->key_type);
        map_decl->value_type = infer_type(map_decl->value_type);
        return type;
    }

    if (type.category == TYPE_LIST) {
        ast_list_decl *list_decl = type.value;
        list_decl->type = infer_type(list_decl->type);
        return type;
    }

    if (type.category == TYPE_FUNCTION) {
        ast_function_type_decl *fn_type_decl = type.value;
        fn_type_decl->return_type = infer_type(fn_type_decl->return_type);
        for (int i = 0; i < fn_type_decl->formal_param_count; ++i) {
            fn_type_decl->formal_params[i]->type = infer_type(fn_type_decl->formal_params[i]->type);
        }

        return type;
    }

    error_printf(infer_line, "cannot parser type %s", type_to_string[type.category]);
    exit(0);
}

ast_type infer_type_decl_ident(ast_ident *ident) {
    // 符号表找到相关类型
    analysis_local_ident *local_ident = table_get(symbol_ident_table, ident->literal);
    if (local_ident->belong != SYMBOL_TYPE_CUSTOM_TYPE) {
        error_printf(infer_line, "'%s' is not a type", local_ident->ident);
    }

    ast_type_decl_stmt *type_decl_stmt = local_ident->decl;

    // type_decl_stmt->ident 为自定义类型名称
    // type_decl_stmt->type 为引用的原始类型 int,my_int,struct....， 此时如果只有一次引用的话，实际上已经还原回去了
    ast_type origin_type = infer_type(type_decl_stmt->type);

    return origin_type;
}

/**
 * 返回对应的 type
 * @param struct_decl
 * @param ident
 * @return
 */
ast_type infer_struct_property_type(ast_struct_decl *struct_decl, char *ident) {
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

ast_type infer_literal(ast_literal *literal) {
    return ast_new_simple_type(literal->type);
}

/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param right
 * @return
 */
bool infer_var_type_can_confirm(ast_type right) {
    if (right.category == TYPE_VAR) {
        return false;
    }

    if (right.category == TYPE_LIST) {
        ast_list_decl *list_decl = right.value;
        if (list_decl->type.category == TYPE_VAR) {
            return false;
        }
    }

    if (right.category == TYPE_MAP) {
        ast_map_decl *map_decl = right.value;
        if (map_decl->key_type.category == TYPE_VAR) {
            return false;
        }
        if (map_decl->value_type.category == TYPE_VAR) {
            return false;
        }
    }

    return true;
}

