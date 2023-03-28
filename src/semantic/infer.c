#include <string.h>
#include "infer.h"
#include "utils/error.h"
#include "src/symbol/symbol.h"
#include "analysis.h"
#include "src/debug/debug.h"
#include "utils/helper.h"

static void set_expr_target(ast_expr *expr, typedecl_t target_type) {
    expr->target_type = target_type;
}

void infer(ast_closure_t *closure_decl) {
    infer_line = 0;
    infer_closure_decl(closure_decl);
}

typedecl_t infer_closure_decl(ast_closure_t *closure_decl) {
    ast_fn_decl *function_decl = closure_decl->fn;

    // 类型还原
    function_decl->return_type = infer_type(function_decl->return_type);
    for (int i = 0; i < function_decl->param_count; ++i) {
        function_decl->formal_params[i]->type = infer_type(function_decl->formal_params[i]->type);
    }

    // env 表达式也有类型，需要还原
    for (int i = 0; i < closure_decl->env_list->count; ++i) {
        infer_expr(closure_decl->env_list->take[i]);
    }

    infer_current_init(closure_decl);
    typedecl_t result = analysis_fn_to_type(function_decl);
    result = infer_type(result);

    infer_block(function_decl->body);

    infer_current = infer_current->parent;

    return result;
}

void infer_block(slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
#ifdef DEBUG_INFER
        debug_stmt("INFER", block->list[i]);
#endif

        // switch 结构导向优化
        infer_stmt(block->take[i]);
    }
}

void infer_stmt(ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            infer_var_decl((ast_var_decl *) stmt->value);
            break;
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            infer_var_decl_assign((ast_var_assign_stmt *) stmt->value);
            break;
        }
        case AST_STMT_ASSIGN: {
            infer_assign((ast_assign_stmt *) stmt->value);
            break;
        }
        case AST_CLOSURE_NEW: {
            infer_closure_decl((ast_closure_t *) stmt->value);
            break;
        }
        case AST_CALL: {
            infer_call((ast_call *) stmt->value);
            break;
        }
        case AST_STMT_IF: {
            infer_if((ast_if_stmt *) stmt->value);
            break;
        }
        case AST_STMT_WHILE: {
            infer_while((ast_for_cond_stmt *) stmt->value);
            break;
        }
        case AST_STMT_FOR_ITERATOR: {
            infer_for_in((ast_for_iterator_stmt *) stmt->value);
            break;
        }
        case AST_STMT_RETURN: {
            infer_return((ast_return_stmt *) stmt->value);
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
typedecl_t infer_expr(ast_expr *expr) {
    typedecl_t type;
    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            type = infer_binary((ast_binary_expr *) expr->value);
            break;
        }
        case AST_EXPR_UNARY: {
            type = infer_unary((ast_unary_expr *) expr->value);
            break;
        }
        case AST_EXPR_IDENT: {
            type = infer_ident(((ast_ident *) expr->value)->literal);
            break;
        }
        case AST_EXPR_LIST_NEW: {
            type = infer_new_list((ast_list_new *) expr->value);
            break;
        }
        case AST_EXPR_MAP_NEW: {
            type = infer_new_map((ast_map_new *) expr->value);
            break;
        }
        case AST_EXPR_STRUCT_NEW: {
            type = infer_new_struct((ast_struct_new_t *) expr->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            // 需要做类型改写，所以传递整个表达式
            type = infer_access(expr);
            break;
        }
        case AST_EXPR_STRUCT_ACCESS: {
            type = infer_struct_access((ast_struct_access *) expr->value);
            break;
        }
        case AST_CALL: {
            type = infer_call((ast_call *) expr->value);
            break;
        }
        case AST_CLOSURE_NEW: {
            type = infer_closure_decl((ast_closure_t *) expr->value);
            break;
        }
        case AST_EXPR_LITERAL: {
            type = infer_literal((ast_literal *) expr->value);
            break;
        }
        case AST_EXPR_ENV_VALUE: {
            type = infer_access_env((ast_env_value *) expr->value);
            break;
        }
        default: {
            assertf(false, "unknown right %v", expr->assert_type);
        }
    }

    expr->type = type;
    return expr->type;
}

/**
 * @param expr
 * @return
 */
typedecl_t infer_binary(ast_binary_expr *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    typedecl_t left_type = infer_expr(&expr->left);
    typedecl_t right_type = infer_expr(&expr->right);

    assertf(left_type.kind == TYPE_INT || left_type.kind == TYPE_FLOAT,
            "invalid operation: %s, right type must be int or float, cannot '%s' type",
            ast_expr_op_str[expr->operator],
            type_kind_string[right_type.kind]);
    assertf(right_type.kind == left_type.kind, "binary operations type not consistent， left: %s, right: %s",
            type_kind_string[right_type.kind], type_kind_string[right_type.kind]);

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
            return type_base_new(TYPE_BOOL);
        }
        default: {
            error_exit("unknown operator code");
            exit(0);
        }
    }
}

/**
 * unary
 * @param expr
 * @return
 */
typedecl_t infer_unary(ast_unary_expr *expr) {
    typedecl_t operand_type = infer_expr(&expr->operand);
    if (expr->operator == AST_EXPR_OPERATOR_NOT && operand_type.kind != TYPE_BOOL) {
        error_exit("!right, right must be bool type");
    }

    if ((expr->operator == AST_EXPR_OPERATOR_NEG) && operand_type.kind != TYPE_INT
        && operand_type.kind != TYPE_FLOAT) {
        error_exit("!right, right must be int or float");
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
typedecl_t infer_ident(string unique_ident) {
    symbol_t *symbol = symbol_table_get(unique_ident);
    assertf(symbol, "ident %s not found", unique_ident);

    if (symbol->type == SYMBOL_TYPE_VAR) {
        ast_var_decl *var_decl = symbol->ast_value;
        var_decl->type = infer_type(var_decl->type); // 类型还原
        return var_decl->type;
    }

    // 比如 print 和 println 都已经注册在了符号表中
    if (symbol->type == SYMBOL_TYPE_FN) {
        ast_fn_decl *new_fn = symbol->ast_value;
        return infer_type(analysis_fn_to_type(new_fn));
    }

    assertf(false, "symbol type not expect");
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
typedecl_t infer_new_list(ast_list_new *new_list) {
    typedecl_t result = {
            .is_origin = true,
            .kind = TYPE_LIST,
    };
    typedecl_list_t *decl = NEW(typedecl_list_t);
    decl->element_type = type_base_new(TYPE_UNKNOWN); // unknown 可以适配任何类型
//    decl->count = new_list->count; // TODO 暂时不支持定义数量, 如果有数量那定义的应该是数组了

    for (int i = 0; i < new_list->count; ++i) {
        typedecl_t item_type = infer_expr(&new_list->values[i]);
        if (decl->element_type.kind == TYPE_UNKNOWN) {
            // 数组已经添加了初始化值，可以添加一种有值进行类型推导了
            decl->element_type = item_type;
        } else {
            if (!type_compare(item_type, decl->element_type)) {
                // 出现了多种类型，无法推导出具体的类型，可以暂定为 any, 并退出右值类型推导
                decl->element_type = type_base_new(TYPE_ANY);
                break;
            }
        }
    }
    result.list_decl = decl;
    new_list->type = result;

    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param new_map
 * @return
 */
typedecl_t infer_new_map(ast_map_new *new_map) {
    typedecl_t result = {
            .is_origin = true,
            .kind = TYPE_MAP,
    };
    typedecl_map_t *map_decl = NEW(typedecl_map_t);
    map_decl->key_type = type_base_new(TYPE_UNKNOWN);
    map_decl->value_type = type_base_new(TYPE_UNKNOWN);
    for (int i = 0; i < new_map->count; ++i) {
        typedecl_t key_type = infer_expr(&new_map->values[i].key);
        typedecl_t value_type = infer_expr(&new_map->values[i].value);

        // key
        if (map_decl->key_type.kind == TYPE_UNKNOWN) {
            map_decl->key_type = key_type;
        } else {
            if (!type_compare(key_type, map_decl->key_type)) {
                map_decl->key_type = type_base_new(TYPE_ANY);
                break;
            }
        }

        // value
        if (map_decl->value_type.kind == TYPE_UNKNOWN) {
            map_decl->value_type = value_type;
        } else {
            if (!type_compare(value_type, map_decl->value_type)) {
                map_decl->value_type = type_base_new(TYPE_ANY);
                break;
            }
        }
    }

    // 冗余
//    new_map->key_type = map_decl->key_type;
//    new_map->value_type = map_decl->value_type;

    result.map_decl = map_decl;

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
typedecl_t infer_new_struct(ast_struct_new_t *new_struct) {
    // 类型还原, struct ident 一定会被还原回 struct 原始结构
    // 如果本身已经是 struct 结构，那么期中的 struct key code 也会被还原成原始类型
    new_struct->type = infer_type(new_struct->type);

    if (new_struct->type.kind != TYPE_STRUCT) {
        error_printf(infer_line, "ident not struct");
    }

    typedecl_struct_t *struct_decl = new_struct->type.struct_decl;
    // new_struct->count 小于等于 struct_decl->count, 允许 new_struct 期间不进行赋值
    for (int i = 0; i < new_struct->count; ++i) {
        ast_struct_property *struct_property = &new_struct->properties[i];

        // struct_decl 已经是被还原过的类型了
        typedecl_t expect_type = infer_struct_property_type(struct_decl, struct_property->key);
        typedecl_t actual_type = infer_expr(&struct_property->value);

        // expect code 并不允许为 var
        assertf(type_compare(actual_type, expect_type),
                "line: %d, key '%s' expect '%s' type, cannot assign '%s' type",
                infer_line,
                struct_property->key,
                type_kind_string[expect_type.kind],
                type_kind_string[actual_type.kind]);
    }

    return new_struct->type;
}

/**
 * @param expr
 * @return
 */
typedecl_t infer_access(ast_expr *expr) {
    typedecl_t result;
    ast_access *access = expr->value;
    typedecl_t left_type = infer_expr(&access->left);
    typedecl_t key_type = infer_expr(&access->key);

    if (left_type.kind == TYPE_MAP) {
        ast_map_access *access_map = malloc(sizeof(ast_map_access));
        typedecl_map_t *map_decl = left_type.map_decl;

        // 参数改写
        access_map->left = access->left;
        access_map->key = access->key;

        // access_map 冗余字段处理
        access_map->key_type = map_decl->key_type;
        access_map->value_type = map_decl->value_type;
        expr->assert_type = AST_EXPR_MAP_ACCESS;
        expr->value = access_map;


        // 返回值
        result = map_decl->value_type;
    } else if (left_type.kind == TYPE_LIST) {
        assertf(key_type.kind == TYPE_INT, "access list failed, index right type must by int");

        ast_list_access_t *access_list = NEW(ast_list_access_t);
        typedecl_list_t *list_decl = left_type.list_decl;

        // 参数改写
        access_list->left = access->left;
        access_list->index = access->key;
        access_list->type = list_decl->element_type;
        expr->assert_type = AST_EXPR_LIST_ACCESS;
        expr->value = access_list;

        result = list_decl->element_type;
    } else {
        assertf(false, "line: %d, right type must map or list, cannot '%s'", infer_line,
                type_kind_string[left_type.kind]);
    };

    return result;
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * @param struct_access
 * @return
 */
typedecl_t infer_struct_access(ast_struct_access *struct_access) {
    infer_expr(&struct_access->left);
    typedecl_t left_type = struct_access->left.type;

    assertf(left_type.kind == TYPE_STRUCT, "right not struct, cannot access key");

    typedecl_struct_t *struct_decl = left_type.struct_decl;
    for (int i = 0; i < struct_decl->count; ++i) {
        if (str_equal(struct_decl->properties[i].key, struct_access->key)) {
            struct_decl->properties[i].type = infer_type(struct_decl->properties[i].type);
            struct_access->property = struct_decl->properties[i];
            return struct_decl->properties[i].type;
        }
    }

    assertf(false, "cannot get struct key '%s'", struct_access->key);
    exit(0);
}

/**
 * @param call
 * @return
 */
typedecl_t infer_call(ast_call *call) {
    typedecl_t result;


    // 左值符号推导
    typedecl_t left_type = infer_expr(&call->left);
    assertf(left_type.kind == TYPE_FN, "left right not fn, cannot call");

    typedecl_fn_t *type_fn = left_type.fn_decl;

    // 实参类型推导与类型还原
    for (int i = 0; i < call->param_count; ++i) {
        infer_expr(&call->actual_params[i]);  // right 类型还原，其中也包括 spread param
    }

    // 参数对比，由于存在 spread 和 rest 运算，所以不能直接根据参数数量左 assert
    uint8_t count = max(type_fn->formals_count, call->param_count);

    for (int i = 0; i < count; ++i) {
        // first param from actual
        typedecl_t actual = select_actual_param(call, i);
        // first param from formal
        typedecl_t formal = select_formal_param(type_fn, i);
        assertf(type_compare(formal, actual), "call param[%d] type error, expect '%s' type, actual '%s' type", i,
                type_kind_string[formal.kind], type_kind_string[actual.kind]);

        // 如果 i < actual_param_count,则 actual_param 需要配置 target type
        bool is_spread = call->spread_param && i == call->param_count - 1;
        if (i < call->param_count && !is_spread) {
            set_expr_target(&call->actual_params[i], formal);
        }
    }

    return type_fn->return_type;
}

/**
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(ast_var_decl *var_decl) {
    var_decl->type = infer_type(var_decl->type);
    typedecl_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL) {
        error_printf(infer_line, "variable declarations cannot use '%s'", type_kind_string[type.kind]);
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
void infer_var_decl_assign(ast_var_assign_stmt *stmt) {
    typedecl_t expr_type = infer_expr(&stmt->right);

    // 类型推断(不需要再比较类型是否一致)
    if (stmt->var_decl->type.kind == TYPE_UNKNOWN) {
        assertf(infer_var_type_can_confirm(expr_type), "type inference error, right right code is not confirm");
        stmt->var_decl->type = expr_type; // right type is origin type
        return;
    }

    stmt->var_decl->type = infer_type(stmt->var_decl->type);

    // 判断类型是否一致 compare
    assertf(type_compare(stmt->var_decl->type, expr_type),
            "line: %d, cannot assigned variables, because code inconsistency", infer_line);
    set_expr_target(&stmt->right, stmt->var_decl->type);
}

/**
 * @param stmt
 */
void infer_assign(ast_assign_stmt *stmt) {
    typedecl_t target_type = infer_expr(&stmt->left);
    typedecl_t source_type = infer_expr(&stmt->right);

    assertf(type_compare(target_type, source_type), "line: %d, type inconsistency", infer_line);
    set_expr_target(&stmt->right, stmt->left.type);
}

void infer_if(ast_if_stmt *stmt) {
    typedecl_t condition_type = infer_expr(&stmt->condition);
    assertf(type_compare(type_base_new(TYPE_BOOL), condition_type),
            "line: %d, if condition must bool type", infer_line);
    set_expr_target(&stmt->condition, type_base_new(TYPE_BOOL));

    infer_block(stmt->consequent);
    infer_block(stmt->alternate);
}

void infer_while(ast_for_cond_stmt *stmt) {
    typedecl_t condition_type = infer_expr(&stmt->condition);
    if (condition_type.kind != TYPE_BOOL) {
        error_exit("while stmt condition must bool");
    }
    infer_block(stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
void infer_for_in(ast_for_iterator_stmt *stmt) {
    // 经过 infer_expr 的类型一定是已经被还原过的
    typedecl_t iterate_type = infer_expr(&stmt->iterate);

    assertf(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_LIST,
            "for in iterate code must be map/list, actual=%s", type_kind_string[iterate_type.kind]);

    // 类型推断
    ast_var_decl *key_decl = stmt->key;
    ast_var_decl *value_decl = stmt->value;
    if (iterate_type.kind == TYPE_MAP) {
        typedecl_map_t *map_decl = iterate_type.map_decl;
        key_decl->type = map_decl->key_type;
        value_decl->type = map_decl->value_type;
    } else {
        typedecl_list_t *list_decl = iterate_type.list_decl;
        key_decl->type = type_base_new(TYPE_INT);
        value_decl->type = list_decl->element_type;

    }

    infer_block(stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
void infer_return(ast_return_stmt *stmt) {
    typedecl_t return_type = type_base_new(TYPE_VOID);
    if (stmt->expr != NULL) {
        return_type = infer_expr(stmt->expr);
    }

    typedecl_t expect_type = infer_current->closure_decl->fn->return_type;
    assertf(type_compare(expect_type, return_type), "line: %d, return code(%s) error,expect '%s' code", infer_line,
            type_kind_string[return_type.kind],
            type_kind_string[expect_type.kind]);
}

infer_closure *infer_current_init(ast_closure_t *closure_decl) {
    infer_closure *new = malloc(sizeof(infer_closure));
    new->closure_decl = closure_decl;
    new->parent = infer_current;

    infer_current = new;
    return infer_current;
}

/**
 * struct 允许顺序不通，但是 key 和 code 需要相同，在还原时需要根据 key 进行排序
 * 所有的类型数据都会经过该 fn 进行类型还原, 这里可以堆所有的 fn 进行 reflect 的计算以及注册
 * @param type
 * @return
 */
typedecl_t infer_type(typedecl_t type) {
    if (type.is_origin) {
        goto TYPE_ORIGIN;
    }

    type.is_origin = true;
    if (type.kind == TYPE_INT || type.kind == TYPE_BOOL || type.kind == TYPE_FLOAT
        || type.kind == TYPE_STRING
        || type.kind == TYPE_ANY
        || type.kind == TYPE_VOID) {
        // 简单类型不需要再还原了
        goto TYPE_ORIGIN;
    }

    if (type.kind == TYPE_STRUCT) {
        typedecl_struct_t *struct_decl = type.struct_decl;
//        infer_sort_struct_decl(struct_decl); // 按照 key 排序

        // 对 struct 的每个属性都进行还原
        for (int i = 0; i < struct_decl->count; ++i) {
            struct_decl->properties[i].type = infer_type(struct_decl->properties[i].type);
        }

        goto TYPE_ORIGIN;
    }

    // code foo = int, 'foo' is type_dec_ident
    if (type.kind == TYPE_IDENT) {
        type = infer_type_def(type.ident_decl);
        goto TYPE_ORIGIN;
    }

    if (type.kind == TYPE_MAP) {
        typedecl_map_t *map_decl = type.map_decl;
        map_decl->key_type = infer_type(map_decl->key_type);
        map_decl->value_type = infer_type(map_decl->value_type);
        goto TYPE_ORIGIN;
    }

    if (type.kind == TYPE_LIST) {
        typedecl_list_t *list_decl = type.list_decl;
        list_decl->element_type = infer_type(list_decl->element_type);
        goto TYPE_ORIGIN;
    }

    if (type.kind == TYPE_FN) {
        typedecl_fn_t *type_decl_fn = type.fn_decl;
        type_decl_fn->return_type = infer_type(type_decl_fn->return_type);
        for (int i = 0; i < type_decl_fn->formals_count; ++i) {
            type_decl_fn->formals_types[i] = infer_type(type_decl_fn->formals_types[i]);
        }
        goto TYPE_ORIGIN;
    }

    assertf(false, "cannot parser type %s", type_kind_string[type.kind]);
    TYPE_ORIGIN:
    // - 增加 is_heap 标识
    type.in_heap = type_default_in_heap(type);

    ct_reflect_type(type);

    return type;
}

typedecl_t infer_type_def(typedecl_ident_t *def) {
    // 符号表找到相关类型
    symbol_t *symbol = symbol_table_get(def->literal);
    if (symbol->type != SYMBOL_TYPE_DECL) {
        error_printf(infer_line, "'%s' is not a code", symbol->ident);
    }

    ast_typedef_stmt *type_decl_stmt = symbol->ast_value;

    // type_decl_stmt->ident 为自定义类型名称
    // type_decl_stmt->code 为引用的原始类型 int,my_int,struct....， 此时如果只有一次引用的话，实际上已经还原回去了
    typedecl_t origin_type = infer_type(type_decl_stmt->type);

    return origin_type;
}

/**
 * 返回对应的 code
 * @param struct_decl
 * @param ident
 * @return
 */
typedecl_t infer_struct_property_type(typedecl_struct_t *struct_decl, char *ident) {
    for (int i = 0; i < struct_decl->count; ++i) {
        if (strcmp(struct_decl->properties[i].key, ident) == 0) {
            return struct_decl->properties[i].type;
        }
    }

    error_printf(infer_line, "not found struct key '%s'", ident);
    exit(0);
}

/**
 * ~~对 struct list 按照 key 进行排序,选择排序~~
 * struct 暂时不支持调换顺序
 * @param struct_decl
 */
//void infer_sort_struct_decl(typedecl_struct_t *struct_decl) {
//    for (int i = 0; i < struct_decl->count; ++i) {
//        for (int j = i + 1; j < struct_decl->count; ++j) {
//            if (strcmp(struct_decl->properties[i].key, struct_decl->properties[j].key) > 0) {
//                // 交换
//                struct_property_t temp = struct_decl->properties[i];
//                struct_decl->properties[i] = struct_decl->properties[j];
//                struct_decl->properties[j] = temp;
//            }
//        }
//    }
//}

typedecl_t infer_literal(ast_literal *literal) {
    return type_base_new(literal->kind);
}

/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param right
 * @return
 */
bool infer_var_type_can_confirm(typedecl_t right) {
    if (right.kind == TYPE_UNKNOWN) {
        return false;
    }

    // var a = []  这样就完全不知道是个啥。。。
    if (right.kind == TYPE_LIST) {
        typedecl_list_t *list_decl = right.list_decl;
        if (list_decl->element_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (right.kind == TYPE_MAP) {
        typedecl_map_t *map_decl = right.map_decl;
        if (map_decl->key_type.kind == TYPE_UNKNOWN) {
            return false;
        }
        if (map_decl->value_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    return true;
}

typedecl_t infer_access_env(ast_env_value *expr) {
    typedecl_t t = infer_ident(expr->unique_ident);
    return t;
}

