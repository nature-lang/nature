#include <string.h>
#include "infer.h"
#include "src/error.h"
#include "analyzer.h"
#include "src/debug/debug.h"

static void literal_integer_casting(module_t *m, ast_expr_t *expr, type_t target_type) {
    INFER_ASSERTF(expr->assert_type == AST_EXPR_LITERAL, "integer casting only support literal");

    ast_literal_t *literal = expr->value;

    INFER_ASSERTF(is_integer(literal->kind), "type inconsistency");

    int64_t i = atoll(literal->value);

    INFER_ASSERTF(integer_range_check(cross_kind_trans(target_type.kind), i), "integer out of range");

    literal->kind = target_type.kind;
    expr->type = target_type;
}

static void literal_float_casting(module_t *m, ast_expr_t *expr, type_t target_type) {
    INFER_ASSERTF(expr->assert_type == AST_EXPR_LITERAL, "float casting only support literal");

    ast_literal_t *literal = expr->value;

    INFER_ASSERTF(is_number(literal->kind), "type inconsistency");


    type_kind target_kind = cross_kind_trans(target_type.kind);
    double f = atof(literal->value);

    INFER_ASSERTF(float_range_check(cross_kind_trans(target_type.kind), f), "float out of range");

    literal->kind = target_type.kind;
    expr->type = target_type;
}

static uint32_t fn_params_hash(type_fn_t *fn) {
    char *str = itoa(TYPE_FN);
    for (int i = 0; i < fn->formal_types->length; ++i) {
        type_t *t = ct_list_value(fn->formal_types, i);
        rtype_t r = ct_reflect_type(*t);
        str = str_connect(str, itoa(r.hash));
    }

    return hash_string(str);
}

/**
 * 当函数名称中存在多个函数时，将会通过简单的无类型转换进行函数匹配 (允许 single -> union type)
 * @param actual_params
 * @param s
 * @return
 */
static ast_fndef_t *fn_match(module_t *m, list_t *actual_params, symbol_t *s) {
    assert(s->type == SYMBOL_FN);
    assert(s->fndefs);
    assert(s->fndefs->count > 0);
    if (s->fndefs->count == 1) {
        return s->ast_value;
    }

    slice_t *fndefs = s->fndefs;

    // fndefs 进行类型还原
    ast_fndef_t *origin = m->infer_current;
    for (int i = 0; i < fndefs->count; ++i) {
        ast_fndef_t *fndef = fndefs->take[i];
        m->infer_current = fndef;
        infer_fn_decl(m, fndef);
    }
    m->infer_current = origin;

    // 基于参数数量进行一次过滤
    slice_t *temps = slice_new();
    for (int i = 0; i < fndefs->count; ++i) {
        ast_fndef_t *fndef = fndefs->take[i];
        type_t type_fn = fndef->type;
        if (type_fn.fn->rest) {
            // 实参的数量大于等于 type_fn.params.length - 1
            if (actual_params->length < (type_fn.fn->formal_types->length - 1)) {
                continue;
            }
        } else {
            //数量必须相等
            if (actual_params->length != type_fn.fn->formal_types->length) {
                continue;
            }
        }

        slice_push(temps, fndef);
    }

    fndefs = temps;

    // 开始匹配
    for (int i = 0; i < actual_params->length; ++i) {
        ast_expr_t *expr = ct_list_value(actual_params, i);
        infer_right_expr(m, expr, type_basic_new(TYPE_UNKNOWN));
        type_t actual_type = expr->type;

        // ast_fndef
        temps = slice_new();
        for (int j = 0; j < fndefs->count; ++j) {
            ast_fndef_t *fndef = fndefs->take[j];
            type_t type_fn = fndef->type;

            type_t *formal_type = select_formal_param(type_fn.fn, i);
            if (!formal_type) {
                continue;
            }

            INFER_ASSERTF(formal_type->kind != TYPE_GEN, "generic type not specialization");

            // 特殊情况处理
            if (type_fn.fn->rest &&
                actual_params->length == type_fn.fn->formal_types->length &&
                (i == type_fn.fn->formal_types->length - 1)) {

                // rest 情况
                if (type_compare(*formal_type, actual_type)) {
                    slice_push(temps, fndef);
                }

                // actual 是一个数组的替代情况
                if (actual_type.kind == TYPE_LIST &&
                    type_compare(*formal_type, actual_type.list->element_type)) {
                    slice_push(temps, fndef);
                }


                continue;
            }

            if (formal_type->kind == TYPE_UNION && actual_type.kind != TYPE_UNION) {
                if (union_type_contains(*formal_type, actual_type)) {
                    slice_push(temps, fndef);
                }
                continue;
            }

            if (!type_compare(*formal_type, actual_type)) {
                continue;
            }
            slice_push(temps, fndef);
        }

        fndefs = temps;
    }

    INFER_ASSERTF(fndefs->count > 0, "cannot match fn=%s", s->ident);
    INFER_ASSERTF(fndefs->count < 2, "fn=%s match more than one fndef", s->ident);

    return fndefs->take[0];
}

/**
 * 所有的 fndef 都将从这里进入，一旦 reduction 完成，就能基于入参快速计算出唯一标识
 * @param m
 * @param fndef
 * @param t
 */
static void rewrite_fndef(module_t *m, ast_fndef_t *fndef) {
    assert(fndef->type.status == REDUCTION_STATUS_DONE);
    if (fndef->params_hash) {
        return;
    }

    symbol_t *s = symbol_table_get(fndef->symbol_name);
    assert(s);
    assert(s->fndefs && s->fndefs->count > 0);

    // 函数名称全局唯一，没必要进行重写
    if (s->fndefs->count == 1) {
        return;
    }

    if (!fndef->is_local) {
        fndef->params_hash = itoa(fn_params_hash(fndef->type.fn));
    } else {
        // local fn 直接适用 parent 的 hash 即可, 这么做也是为了兼容 generic 的情况
        // 否则 local fn 根据不会存在同名的情况, 另外 local fn 的调用作用域仅仅在当前函数内
        assert(fndef->global_parent);
        assert(strlen(fndef->global_parent->params_hash) > 0);

        fndef->params_hash = fndef->global_parent->params_hash;
        if (fndef->closure_name) {
            fndef->closure_name = str_connect_by(fndef->closure_name, fndef->params_hash, ".");

            ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
            var_decl->ident = fndef->closure_name;
            var_decl->type = fndef->type;
            symbol_table_set(fndef->closure_name, SYMBOL_VAR, var_decl, true);
        }
    }

    fndef->symbol_name = str_connect_by(fndef->symbol_name, fndef->params_hash, ".");
    symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, fndef->is_local);
}

static char *rewrite_ident_def(module_t *m, char *old) {
    assert(m->infer_current);
    if (!m->infer_current->params_hash) {
        return old;
    }

    symbol_t *s = symbol_table_get(old);
    assert(s->is_local);
    assert(s->type != SYMBOL_FN);

    char *ident = str_connect_by(old, m->infer_current->params_hash, ".");

    s->ident = ident;
    symbol_table_set(ident, s->type, s->ast_value, s->is_local);

    return ident;
}

static char *rewrite_ident_use(module_t *m, char *old) {
    assert(m->infer_current);
    if (!m->infer_current->params_hash) {
        return old;
    }

    return str_connect_by(old, m->infer_current->params_hash, ".");
}

static void rewrite_type_alias(module_t *m, ast_type_alias_stmt_t *stmt) {
    assert(m->infer_current);
    // 如果不存在 params_hash 表示当前 fndef 不存在基于泛型的重写，所以 alias 也不需要进行重写
    if (!m->infer_current->params_hash) {
        return;
    }

    stmt->ident = str_connect_by(stmt->ident, m->infer_current->params_hash, ".");
    symbol_table_set(stmt->ident, SYMBOL_TYPE_ALIAS, stmt, true);
}

static void rewrite_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    assert(m->infer_current);
    if (!m->infer_current->params_hash) {
        return;
    }

    var_decl->ident = str_connect_by(var_decl->ident, m->infer_current->params_hash, ".");
    // 只有 local var decl 才需要进行 rewrite
    symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, true);
}


static void infer_body(module_t *m, slice_t *body) {
    for (int i = 0; i < body->count; ++i) {
#ifdef DEBUG_INFER
        debug_stmt("INFER", body->list[i]);
#endif

        // switch 结构导向优化
        infer_stmt(m, body->take[i]);
    }
}

/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param t
 * @return
 */
static bool type_confirmed(type_t t) {
    if (t.kind == TYPE_UNKNOWN) {
        return false;
    }

    // var a = []  这样就完全不知道是个啥。。。
    if (t.kind == TYPE_LIST) {
        type_list_t *l = t.list;
        if (l->element_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (t.kind == TYPE_MAP) {
        type_map_t *m = t.map;
        if (m->key_type.kind == TYPE_UNKNOWN) {
            return false;
        }
        if (m->value_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (t.kind == TYPE_SET) {
        type_set_t *m = t.set;
        if (m->element_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *element_type = ct_list_value(tuple->elements, i);
            if (element_type->kind == TYPE_UNKNOWN) {
                return false;
            }
        }
    }

    return true;
}

/**
 * @param expr
 * @return
 */
static type_t infer_binary(module_t *m, ast_binary_expr_t *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    type_t left_type = infer_right_expr(m, &expr->left, type_basic_new(TYPE_UNKNOWN));
    type_t right_type = infer_right_expr(m, &expr->right, left_type);

    // 目前 binary 的两侧符号只支持 int 和 float
    if (is_number(left_type.kind)) {
        // 右值也必须是 number
        INFER_ASSERTF(is_number(right_type.kind),
                      "binary operator '%s' only support number operand, actual '%s %s %s'",
                      ast_expr_op_str[expr->operator],
                      type_kind_str[left_type.kind],
                      ast_expr_op_str[expr->operator],
                      type_kind_str[right_type.kind]);
    }

    if (left_type.kind == TYPE_STRING) {
        // 右值必须是 string
        INFER_ASSERTF(right_type.kind == TYPE_STRING,
                      "binary operator '%s' only support string operand, actual '%s %s %s'",
                      ast_expr_op_str[expr->operator],
                      type_kind_str[left_type.kind],
                      ast_expr_op_str[expr->operator],
                      type_kind_str[right_type.kind]);
    }

    // 位运算只能支持整形
    if (is_integer_operator(expr->operator)) {
        INFER_ASSERTF(is_integer(left_type.kind) && is_integer(right_type.kind),
                      "binary operator '%s' only integer operand",
                      ast_expr_op_str[expr->operator]);
    }

    // 暂时取消类型隐式转换，而是使用左值的类型作为目标类型
    type_t target_type = left_type;

    switch (expr->operator) {
        // 算术运算符
        case AST_OP_LSHIFT:
        case AST_OP_RSHIFT:
        case AST_OP_AND:
        case AST_OP_OR:
        case AST_OP_XOR:
        case AST_OP_REM:
        case AST_OP_ADD:
        case AST_OP_SUB:
        case AST_OP_MUL:
        case AST_OP_DIV: {
            return target_type;
        }

            // 逻辑运算符
        case AST_OP_LT:
        case AST_OP_LE:
        case AST_OP_GT:
        case AST_OP_GE:
        case AST_OP_EE:
        case AST_OP_NE: {
            return type_basic_new(TYPE_BOOL);
        }
        default: {
            INFER_ASSERTF(false, "unknown operator");
            exit(1);
        }
    }
}

/**
 * 类型转换 type casting
 * i8 foo = 12 as i8
 *
 * 类型断言 type assert
 * i8|i16|i32 bar = 24
 * i16 foo = bar as i16
 * @param m
 * @param type_as
 * @return
 */
static type_t infer_as_expr(module_t *m, ast_expr_t *expr) {
    ast_as_expr_t *as_expr = expr->value;
    type_t target_type = reduction_type(m, as_expr->target_type);
    as_expr->target_type = target_type;

    // e.g. [] as [u8,5]
    // empty list handle
    if (as_expr->src_operand.assert_type == AST_EXPR_LIST_NEW) {
        ast_list_new_t *list_new_expr = as_expr->src_operand.value;
        if (list_new_expr->elements->length == 0 && target_type.kind == TYPE_LIST) {
            expr->type = target_type;
            expr->assert_type = as_expr->src_operand.assert_type;
            expr->value = as_expr->src_operand.value;
            return target_type;
        }
    }

    // {} as {u8}
    if (as_expr->src_operand.assert_type == AST_EXPR_SET_NEW) {
        ast_set_new_t *set_new_expr = as_expr->src_operand.value;
        if (set_new_expr->elements->length == 0 && target_type.kind == TYPE_SET) {
            expr->type = target_type;
            expr->assert_type = as_expr->src_operand.assert_type;
            expr->value = as_expr->src_operand.value;
            return target_type;
        }
    }

    // {} as {u8:string}
    if (as_expr->src_operand.assert_type == AST_EXPR_MAP_NEW) {
        ast_map_new_t *map_new_expr = as_expr->src_operand.value;
        if (map_new_expr->elements->length == 0 && target_type.kind == TYPE_MAP) {
            expr->type = target_type;
            expr->assert_type = as_expr->src_operand.assert_type;
            expr->value = as_expr->src_operand.value;
            return target_type;
        }
    }

    infer_right_expr(m, &as_expr->src_operand, type_basic_new(TYPE_UNKNOWN));
    if (as_expr->src_operand.type.kind == TYPE_UNION) {
        INFER_ASSERTF(target_type.kind != TYPE_UNION, "union to union type is not supported");

        // target_type 必须包含再 union 中
        INFER_ASSERTF(union_type_contains(as_expr->src_operand.type, target_type),
                      "type = %s not contains in union type",
                      type_kind_str[target_type.kind]);
        return target_type;
    }

    // 特殊类型转换 string -> list u8
    if (as_expr->src_operand.type.kind == TYPE_STRING && is_list_u8(target_type)) {
        return target_type;
    }

    // list u8 -> string
    if (is_list_u8(as_expr->src_operand.type) && target_type.kind == TYPE_STRING) {
        return target_type;
    }

    if (!is_float(as_expr->src_operand.type.kind) && target_type.kind == TYPE_CPTR) {
        return target_type;
    }

    INFER_ASSERTF(can_type_casting(target_type.kind), "type = %s not support casting", type_kind_str[target_type.kind]);
    return target_type;
}

static type_t infer_is_expr(module_t *m, ast_is_expr_t *is_expr) {
    type_t t = infer_right_expr(m, &is_expr->src_operand, type_basic_new(TYPE_UNKNOWN));
    INFER_ASSERTF(t.kind == TYPE_UNION, "only any/union type can use 'is' keyword");
    return type_basic_new(TYPE_BOOL);
}

/**
 * unary
 * @param expr
 * @return
 */
static type_t infer_unary(module_t *m, ast_unary_expr_t *expr) {
    if (expr->operator == AST_OP_NOT) {
        // bool 支持各种类型的 implicit type convert
        return infer_right_expr(m, &expr->operand, type_basic_new(TYPE_BOOL));
    }

    type_t type = infer_right_expr(m, &expr->operand, type_basic_new(TYPE_UNKNOWN));

    if ((expr->operator == AST_OP_NEG) && !is_number(type.kind)) {
        INFER_ASSERTF(false, "neg operand must applies to int or float type");
    }

    // &var
    if (expr->operator == AST_OP_LA) {
        INFER_ASSERTF(expr->operand.assert_type != AST_EXPR_LITERAL && expr->operand.assert_type != AST_CALL,
                      "cannot take the address of an literal or call");
        return type_ptrof(type);
    }

    // *var
    if (expr->operator == AST_OP_IA) {
        INFER_ASSERTF(type.kind == TYPE_POINTER, "cannot dereference non-pointer type");
        return type.pointer->value_type;
    }

    return type;
}

/**
 * use ident
 * @param expr
 * @return
 */
static type_t infer_ident(module_t *m, ast_ident *ident) {
    char *unique_ident = ident->literal;
    symbol_t *symbol = symbol_table_get(unique_ident);
    INFER_ASSERTF(symbol, "ident %s not found", unique_ident);

    // 引用了 local symbol 时则尝试对 ident 进行改写, 能够改写的前提是当前 infer 的 fn 是泛型 fn
    if (symbol->is_local) {
        ident->literal = rewrite_ident_use(m, ident->literal);
        // 基于重写过后的符号重新定位 symbol
        symbol = symbol_table_get(ident->literal);
        INFER_ASSERTF(symbol, "ident %s not found", ident->literal);
    }

    if (symbol->type == SYMBOL_VAR) {
        ast_var_decl_t *var_decl = symbol->ast_value;
        var_decl->type = reduction_type(m, var_decl->type); // 类型还原
        return var_decl->type;
    }

    // 比如 print 和 println 都已经注册在了符号表中
    if (symbol->type == SYMBOL_FN) {
        ast_fndef_t *fndef = symbol->ast_value;
        return infer_fn_decl(m, fndef);
    }

    INFER_ASSERTF(false, "symbol type not expect");
    exit(1);
}

/**
 * 这里如果有问题直接就退出了
 * [a, b(), c[1], d.foo]
 * @param list_new
 * @return 
 */
static type_t infer_list_new(module_t *m, ast_list_new_t *list_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_LIST);

    type_list_t *type_list = NEW(type_list_t);
    // 初始化时类型未知
    type_list->element_type = type_basic_new(TYPE_UNKNOWN);

    if (target_type.kind == TYPE_LIST) {
        // 如果 target 强制约定了类型则直接使用 target 的类型, 否则就自己推断
        // 考虑到 list_new 可能为空的情况，所以这里默认赋值一次
        type_list->element_type = target_type.list->element_type;
    }

    result.list = type_list;
    if (list_new->elements->length == 0) {
        INFER_ASSERTF(type_confirmed(type_list->element_type), "list element type not confirm");
        return result;
    }

    // target 类型不确定时，则按 list 首个元素类型进行推导
    if (type_list->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(list_new->elements, 0);
        type_list->element_type = infer_right_expr(m, item_expr, type_basic_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < list_new->elements->length; ++i) {
        ast_expr_t *item_expr = ct_list_value(list_new->elements, i);
        infer_right_expr(m, item_expr, type_list->element_type);
    }

    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param map_new
 * @return
 */
static type_t infer_map_new(module_t *m, ast_map_new_t *map_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_MAP);

    type_map_t *type_map = NEW(type_map_t);
    type_map->key_type = type_basic_new(TYPE_UNKNOWN);
    type_map->value_type = type_basic_new(TYPE_UNKNOWN);

    if (target_type.kind == TYPE_MAP) {
        // 考虑到 map 可能为空的情况，所以这里默认赋值一次, 如果为空就直接使用 target 的类型
        type_map->key_type = target_type.map->key_type;
        type_map->value_type = target_type.map->value_type;
    }
    result.map = type_map;
    if (map_new->elements->length == 0) {
        INFER_ASSERTF(type_confirmed(type_map->key_type), "map key type not confirm");
        INFER_ASSERTF(type_confirmed(type_map->value_type), "map value type not confirm");
        return result;
    }

    // 基于首个元素进行类型推断
    if (type_map->key_type.kind == TYPE_UNKNOWN) {
        ast_map_element_t *item = ct_list_value(map_new->elements, 0);
        type_map->key_type = infer_right_expr(m, &item->key, type_basic_new(TYPE_UNKNOWN));
        type_map->value_type = infer_right_expr(m, &item->value, type_basic_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element_t *item = ct_list_value(map_new->elements, i);
        infer_right_expr(m, &item->key, type_map->key_type);
        infer_right_expr(m, &item->value, type_map->value_type);
    }

    return result;
}

/**
 * {1, 2, a.b,value[1]}
 * @param set_new
 * @return
 */
static type_t infer_set_new(module_t *m, ast_set_new_t *set_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_SET);

    type_set_t *type_set = NEW(type_set_t);
    type_set->element_type = type_basic_new(TYPE_UNKNOWN);

    // 右值如果有推荐的类型，则基于推荐类型做 infer, 此时可能会触发类型转换
    if (target_type.kind == TYPE_SET) {
        type_set->element_type = target_type.set->element_type;
    }

    result.set = type_set;
    if (set_new->elements->length == 0) {
        INFER_ASSERTF(type_confirmed(type_set->element_type), "set element type not confirm");
        return result;
    }
    // target 类型不确定则按首个元素类型进行推导
    if (type_set->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(set_new->elements, 0);
        type_set->element_type = infer_right_expr(m, item_expr, type_basic_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < set_new->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(set_new->elements, i);
        infer_right_expr(m, expr, type_set->element_type);
    }

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
 *
 * @param ast
 * @return
 */
static type_t infer_struct_new(module_t *m, ast_struct_new_t *ast) {
    // person to struct
    ast->type = reduction_type(m, ast->type);

    INFER_ASSERTF(ast->type.kind == TYPE_STRUCT, "ident not struct, cannot struct new");

    type_struct_t *type_struct = ast->type.struct_;

    table_t *exists = table_new();
    // exists key
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *struct_property = ct_list_value(ast->properties, i);
        struct_property_t *expect_property = type_struct_property(type_struct, struct_property->key);


        table_set(exists, struct_property->key, struct_property);

        // struct_decl 已经是被还原过的类型了
        infer_right_expr(m, struct_property->right, expect_property->type);

        // type 冗余,方便计算 size (不能用来计算 offset)
        struct_property->type = expect_property->type;
    }

    list_t *default_properties = ast->type.struct_->properties;
    for (int i = 0; i < default_properties->length; ++i) {
        struct_property_t *d = ct_list_value(default_properties, i);
        if (!d->right || table_exist(exists, d->key)) {
            continue;
        }

        ct_list_push(ast->properties, d);
    }

    return ast->type;
}

/**
 * a[b]  list/map/tuple 都将通过中括号的方式进行访问
 * @param expr
 * @return
 */
static type_t infer_access(module_t *m, ast_expr_t *expr) {
    ast_access_t *access = expr->value;
    type_t left_type = infer_left_expr(m, &access->left);

    // ast_access to ast_map_access
    if (left_type.kind == TYPE_MAP) {
        ast_map_access_t *map_access = NEW(ast_map_access_t);
        type_map_t *type_map = left_type.map;

        // 基于 map 编译 key
        infer_right_expr(m, &access->key, type_map->key_type);

        // 参数改写(这里照抄就行了)
        map_access->left = access->left;
        map_access->key = access->key;

        // access_map type 字段冗余 冗余字段处理
        map_access->key_type = type_map->key_type;
        map_access->value_type = type_map->value_type;
        expr->assert_type = AST_EXPR_MAP_ACCESS;
        expr->value = map_access;

        // 返回值
        return type_map->value_type;
    }

    if (left_type.kind == TYPE_LIST) {
        type_t key_type = infer_right_expr(m, &access->key, type_basic_new(TYPE_INT));

        // ast_access -> ast_list_access
        ast_list_access_t *list_access = NEW(ast_list_access_t);

        type_list_t *type_list = left_type.list;

        // 参数改写
        list_access->left = access->left;
        list_access->index = access->key;
        list_access->element_type = type_list->element_type;
        expr->assert_type = AST_EXPR_LIST_ACCESS;
        expr->value = list_access;

        return type_list->element_type;
    }

    if (left_type.kind == TYPE_TUPLE) {
        type_t key_type = infer_right_expr(m, &access->key, type_basic_new(TYPE_INT));

        INFER_ASSERTF(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");

        type_tuple_t *type_tuple = left_type.tuple;

        ast_literal_t *index_literal = access->key.value; // 读取 index 的值
        INFER_ASSERTF(index_literal->kind == TYPE_INT, "tuple index field must int immediate value");
        uint64_t index = atoi(index_literal->value);

        INFER_ASSERTF(index < type_tuple->elements->length, "tuple index field '%d' not in tuples", index);

        // 返回值的类型, get tuple element.
        type_t *type = ct_list_value(type_tuple->elements, index);

        ast_tuple_access_t *tuple_access = NEW(ast_tuple_access_t);
        tuple_access->left = access->left;
        tuple_access->index = index;
        tuple_access->element_type = *type;
        expr->assert_type = AST_EXPR_TUPLE_ACCESS;
        expr->value = tuple_access;

        return *type;
    }

    INFER_ASSERTF(false, "access only support must map/list/tuple, cannot '%s'",
                  type_kind_str[left_type.kind]);
    exit(1);
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * self.test
 * @param select
 * @return
 */
static type_t infer_select(module_t *m, ast_expr_t *expr) {
    ast_select_t *select = expr->value;

    infer_left_expr(m, &select->left);

    // self type to select
    // self_select -> instance_select
    if (select->left.type.kind == TYPE_SELF) {
        ast_fndef_t *current = m->infer_current;
        INFER_ASSERTF(current->self_struct, "use 'self' in struct outside");

        // 当前 select 必定在 fn body 中，而处理 fn body 之前， fn.self_struct 在处理 body 之前已经进行了还原
        select->left.type = reduction_type(m, *current->self_struct);
    }

    // ast_access to ast_struct_access
    if (select->left.type.kind == TYPE_STRUCT) {
        // 经过上面对 infer_right_expr, 这里对 type 一定是 reduction 的
        type_struct_t *type_struct = select->left.type.struct_;
        struct_property_t *p = type_struct_property(type_struct, select->key);
        INFER_ASSERTF(p, "type %s struct no property '%s'", type_struct->ident, select->key);

        // 改写
        ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
        struct_select->left = select->left;
        struct_select->key = select->key;
        struct_select->property = p;
        expr->assert_type = AST_EXPR_STRUCT_SELECT;
        expr->value = struct_select;

        return p->type;
    }

    INFER_ASSERTF(false, "type '%s' cannot use dot syntax", type_kind_str[select->left.type.kind]);
    exit(1);
}

/**
 * 参考 infer_list_select_call 方法， 主要是要实现
 *
 * string.len() 返回 int
 * string.c_string() 返回 type cptr = uint(未还原)
 * @return
 */
static type_t infer_string_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;

    if (str_equal(s->key, BUILTIN_LEN_KEY)) {
        // 参数核验
        INFER_ASSERTF(call->actual_params->length == 0, "string len param failed");

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);

        call->left = *ast_ident_expr(RT_CALL_STRING_LENGTH);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }

    if (str_equal(s->key, BUILTIN_RAW_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 0, "string c_string param failed");

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);

        call->left = *ast_ident_expr(RT_CALL_STRING_RAW);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_CPTR);
        return type_basic_new(TYPE_CPTR);
    }

    INFER_ASSERTF(false, "string select call '%s' not support", s->key);
    exit(1);
}

/**
 * 对 call 参数验证后对 call 进行改写
 * @param m
 * @param call
 * @return
 */
static type_t infer_list_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_list_t *list_type = s->left.type.list; // 已经进行过类型推导了

    if (str_equal(s->key, LIST_PUSH_KEY)) {
        // push 对参数需要与 list element type 一致，否则抛出异常
        INFER_ASSERTF(call->actual_params->length == 1, "list push param failed");
        ast_expr_t *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, list_type->element_type);

        // 参数核验完成，对整个 call 进行改写, 改写成 list_push(l, value_ref)
        // 参数重写
        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left); // list operand
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA)); // value operand

        call->left = *ast_ident_expr(RT_CALL_LIST_PUSH);
        infer_left_expr(m, &call->left); // 对 ident 进行推导计算出其类型
        call->return_type = type_basic_new(TYPE_VOID);

        // list_push() 返回 void
        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, BUILTIN_LEN_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 0, "list length not param");

        // 改写
        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left); // list operand

        call->left = *ast_ident_expr(RT_CALL_LIST_LENGTH);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }

    if (str_equal(s->key, BUILTIN_CAP_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 0, "list length not param");

        // 改写
        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left); // list operand

        call->left = *ast_ident_expr(RT_CALL_LIST_CAPACITY);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }

    if (str_equal(s->key, BUILTIN_RAW_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 0, "list length not param");

        // 改写
        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left); // list operand

        call->left = *ast_ident_expr(RT_CALL_LIST_RAW);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_CPTR);

        return type_basic_new(TYPE_CPTR);
    }

    INFER_ASSERTF(false, "list not field '%s'", s->key);
    exit(0);
}

/**
 * 需要做值改写,所以这里需要将
 * @param m
 * @param call
 * @return
 */
static type_t infer_map_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_map_t *map_type = s->left.type.map; // 已经进行过类型推导了
    if (str_equal(s->key, MAP_DELETE_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 1, "map.delete param failed");
        ast_expr_t *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, map_type->key_type);

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_MAP_DELETE);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_VOID);

        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, MAP_LENGTH_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 0, "map.length not param");

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);

        call->left = *ast_ident_expr(RT_CALL_MAP_LENGTH);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }

    INFER_ASSERTF(false, "map not field '%s'", s->key);
    exit(0);
}

static type_t infer_set_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_set_t *set_type = s->left.type.set; // 已经进行过类型推导了
    if (str_equal(s->key, SET_DELETE_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 1, "set.delete param failed");
        ast_expr_t *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_DELETE);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_VOID);

        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, SET_ADD_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 1, "set.add param failed");
        ast_expr_t *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        // s = left.key() 这里到 left 才是目标即可
        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_ADD);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_BOOL);

        return type_basic_new(TYPE_BOOL);
    }

    if (str_equal(s->key, SET_HAS_KEY)) {
        INFER_ASSERTF(call->actual_params->length == 1, "set.contains param failed");
        ast_expr_t *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        call->actual_params = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_CONTAINS);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_BOOL);

        return type_basic_new(TYPE_BOOL);
    }

    INFER_ASSERTF(false, "set not field '%s'", s->key);
    exit(1);
}

static void infer_call_params(module_t *m, ast_call_t *call, type_fn_t *target_type_fn) {
    // 由于支持 fndef rest 语言，所以实参的数量大于等于形参的数量
    if (!target_type_fn->rest) {
        INFER_ASSERTF(call->actual_params->length == target_type_fn->formal_types->length, "call params count failed");
    }

    for (int i = 0; i < call->actual_params->length; ++i) {
        // first param from formal
        type_t *formal_type = select_formal_param(target_type_fn, i);
        if (i == 0 && formal_type->kind == TYPE_SELF) {
            // select first param 是 infer 自己伪造的，所以这里不需要在进行校验了
            continue;
        }

        ast_expr_t *actual_param = ct_list_value(call->actual_params, i);

        // rest 的最后一个参数做特殊处理
        if (target_type_fn->rest &&
            call->actual_params->length == target_type_fn->formal_types->length &&
            (i == target_type_fn->formal_types->length - 1)) {


            // 此时 actual params 可以是一个数组也可以是一个元素
            // TODO 这里左值被还原后不能进行二次推导了？二次推导就跳过 infer any 了
            type_t actual_type = infer_right_expr(m, actual_param, type_basic_new(TYPE_UNKNOWN));
            if (actual_type.kind == TYPE_LIST && type_compare(actual_type.list->element_type, *formal_type)) {
                continue;
            }
        }

        infer_right_expr(m, actual_param, *formal_type);
    }
}

/**
 * if call first param type is self
 * fn struct.call(param1) -> struct.key(struct, param1) 即可
 * @param m
 * @param call
 * @return
 */
static type_t infer_struct_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;

    type_struct_t *type_struct = s->left.type.struct_; // 已经进行过类型推导了
    struct_property_t *p = type_struct_property(type_struct, s->key);
    INFER_ASSERTF(p, "type %s struct no property '%s'", type_struct->ident, s->key);

    // call left 改写
    ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
    struct_select->left = s->left;
    struct_select->key = s->key;
    struct_select->property = p;
    call->left.assert_type = AST_EXPR_STRUCT_SELECT;
    call->left.value = struct_select;
    call->left.type = p->type;

    // 进入前已经进行了 infer left, 所以这里的 type 都是 reduction 过的
    INFER_ASSERTF(p->type.kind == TYPE_FN, "cannot call non-fn");
    type_fn_t *type_fn = p->type.fn;
    call->return_type = type_fn->return_type;

    type_t *first = ct_list_value(type_fn->formal_types, 0);
    if (first->kind != TYPE_SELF) {
        infer_call_params(m, call, type_fn);
        return type_fn->return_type;
    }

    // formal 的首个参数是 self, 且 self 未经过推断
    list_t *actual_params = call->actual_params;
    call->actual_params = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(call->actual_params, &struct_select->left);
    for (int i = 0; i < actual_params->length; ++i) {
        ct_list_push(call->actual_params, ct_list_value(actual_params, i));
    }
    infer_call_params(m, call, type_fn);
    return type_fn->return_type;
}

/**
 * 由于重载的存在，对参数的 compare 变成了基于 type 的 search 的过程
 * 如果找不到目标函数则说明 key 不存在或者没有找到匹配的函数类型
 * @param call
 * @return
 */
static type_t infer_call(module_t *m, ast_call_t *call) {
    // [].len()
    if (call->left.assert_type == AST_EXPR_SELECT) {
        ast_select_t *select = call->left.value;

        // 这里已经对 left 进行了类型推导，所以后续不需要在进行类型推导了
        infer_right_expr(m, &select->left, type_basic_new(TYPE_UNKNOWN));
        // self 还原成 struct
        if (select->left.type.kind == TYPE_SELF) {
            ast_fndef_t *current = m->infer_current;
            INFER_ASSERTF(current->self_struct, "use 'self' in struct outside");

            // 当前 select 必定在 fn body 中，而处理 fn body 之前， fn.self_struct 在处理 body 之前已经进行了还原
            select->left.type = reduction_type(m, *current->self_struct);
        }


        type_kind select_left_kind = select->left.type.kind;

        if (select_left_kind == TYPE_LIST) {
            return infer_list_select_call(m, call);
        }

        if (select_left_kind == TYPE_MAP) {
            return infer_map_select_call(m, call);
        }

        if (select_left_kind == TYPE_SET) {
            return infer_set_select_call(m, call);
        }

        if (select_left_kind == TYPE_STRING) {
            return infer_string_select_call(m, call);
        }

        if (select_left_kind == TYPE_STRUCT) {
            return infer_struct_select_call(m, call);
        }

        INFER_ASSERTF(false, "select dot call not support type=%s", type_kind_str[select_left_kind]);
    }

    if (call->left.assert_type == AST_EXPR_IDENT) {
        ast_ident *ident = call->left.value;
        symbol_t *s = symbol_table_get(ident->literal);
        INFER_ASSERTF(s, "symbol '%s' not found", ident->literal);

        // 可能存在泛型参数匹配
        if (s->type == SYMBOL_FN && s->fndefs->count > 1) {
            ast_fndef_t *f = fn_match(m, call->actual_params, s);

            assert(!f->closure_name); // 仅 global 函数可能会出现同名的情况

            rewrite_fndef(m, f); // 从泛型名称改为具体名称
            ident->literal = f->symbol_name; // 引用函数的新的符号
        }
    }

    // 左值符号推导
    type_t left_type = infer_left_expr(m, &call->left);
    INFER_ASSERTF(left_type.kind == TYPE_FN, "cannot call non-fn");
    type_fn_t *type_fn = left_type.fn;
    infer_call_params(m, call, type_fn);

    call->return_type = type_fn->return_type;
    return type_fn->return_type;
}

/**
 * var (foo, err) = catch foo()
 * var err = catch foo()
 * var err = catch foo['car'].car()
 * @param try
 * @return
 */
static type_t infer_try(module_t *m, ast_try_t *try) {
    type_t return_type = infer_right_expr(m, &try->expr, type_basic_new(TYPE_UNKNOWN));

    // 当表达式没有返回值时进行特殊处理
    type_t errort = type_new(TYPE_ALIAS, NULL);
    errort.alias = NEW(type_alias_t);
    errort.alias->ident = ERRORT_TYPE_ALIAS;
    errort.status = REDUCTION_STATUS_UNDO;
    errort = reduction_type(m, errort);
    if (return_type.kind == TYPE_VOID) {
        return errort;
    }

    type_t t = type_basic_new(TYPE_TUPLE);
    t.tuple = NEW(type_tuple_t);
    t.tuple->elements = ct_list_new(sizeof(type_t));
    ct_list_push(t.tuple->elements, &return_type);
    ct_list_push(t.tuple->elements, &errort);
    return t;
}


/**
 * 仅声明不再被允许，所有的变量都必须在初始化时进行赋值操作
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    var_decl->type = reduction_type(m, var_decl->type);
    type_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN ||
        type.kind == TYPE_VOID ||
        type.kind == TYPE_NULL ||
        type.kind == TYPE_SELF) {
        INFER_ASSERTF(false, "variable declaration cannot use type '%s'", type_kind_str[type.kind]);
    }

    // global var def 也会走该函数，所以需要特殊处理一下，必须携带 m->infer_current 时才进行 var_decl rewrite
    if (m->infer_current) {
        rewrite_var_decl(m, var_decl);
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
 * var h = call()
 * [i64] list = []
 */
static void infer_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);
    rewrite_var_decl(m, &stmt->var_decl);

    type_t right_type = infer_right_expr(m, &stmt->right, stmt->var_decl.type);

    // 需要进行类型推断
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        INFER_ASSERTF(type_confirmed(right_type), "type inference error, right type not confirmed");

        stmt->var_decl.type = right_type;
        return;
    }
}

/**
 * @param stmt
 */
static void infer_assign(module_t *m, ast_assign_stmt_t *stmt) {
    type_t left_type = infer_left_expr(m, &stmt->left);
    infer_right_expr(m, &stmt->right, left_type);
}

static void infer_if(module_t *m, ast_if_stmt_t *stmt) {
    infer_right_expr(m, &stmt->condition, type_basic_new(TYPE_BOOL));

    infer_body(m, stmt->consequent);
    infer_body(m, stmt->alternate);
}

static void infer_for_cond_stmt(module_t *m, ast_for_cond_stmt_t *stmt) {
    infer_right_expr(m, &stmt->condition, type_basic_new(TYPE_BOOL));

    infer_body(m, stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void infer_for_iterator(module_t *m, ast_for_iterator_stmt_t *stmt) {
    // 经过 infer_right_expr 的类型一定是已经被还原过的
    type_t iterate_type = infer_right_expr(m, &stmt->iterate, type_basic_new(TYPE_UNKNOWN));
    INFER_ASSERTF(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_LIST,
                  "for in iterate type must be map/list, actual=%s", type_kind_str[iterate_type.kind]);

    rewrite_var_decl(m, &stmt->first);

    if (stmt->second) {
        rewrite_var_decl(m, stmt->second);
    }

    // 类型推断 (value 可选)
    ast_var_decl_t *first = &stmt->first;
    ast_var_decl_t *second = stmt->second;

    // 为 key_decl 添加 type
    if (iterate_type.kind == TYPE_MAP) {
        type_map_t *type_map = iterate_type.map;
        first->type = type_map->key_type;
    } else {
        type_list_t *type_list = iterate_type.list;

        // 判断是否存储 second, 如何
        if (!second) {
            // list
            first->type = type_list->element_type;
        } else {
            first->type = type_basic_new(TYPE_INT);
        }

    }

    if (second) {
        if (iterate_type.kind == TYPE_MAP) {
            type_map_t *map_decl = iterate_type.map;
            second->type = map_decl->value_type;
        } else {
            type_list_t *list_decl = iterate_type.list;
            second->type = list_decl->element_type;
        }
    }


    infer_body(m, stmt->body);
}

static void infer_for_tradition(module_t *m, ast_for_tradition_stmt_t *stmt) {
    infer_stmt(m, stmt->init);
    infer_right_expr(m, &stmt->cond, type_basic_new(TYPE_BOOL));
    infer_stmt(m, stmt->update);
    infer_body(m, stmt->body);
}

/**
 * type nullable<t> = null|t
 * @param m
 * @param stmt
 */
static void infer_type_alias_stmt(module_t *m, ast_type_alias_stmt_t *stmt) {
    INFER_ASSERTF(stmt->type.kind != TYPE_GEN, "cannot use type gen in local scope");

    rewrite_type_alias(m, stmt);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void infer_return(module_t *m, ast_return_stmt_t *stmt) {
    type_t expect_type = m->infer_current->return_type;
    if (stmt->expr != NULL) {
        infer_right_expr(m, stmt->expr, expect_type);
    } else {
        INFER_ASSERTF(expect_type.kind == TYPE_VOID, "fn expect return type: %s", type_kind_str[expect_type.kind]);
    }
}

static type_t infer_literal(module_t *m, ast_literal_t *literal) {
    return type_basic_new(literal->kind);
}

static type_t infer_env_access(module_t *m, ast_env_access_t *expr) {
    ast_ident ident = {
            .literal = expr->unique_ident,
    };
    return infer_ident(m, &ident);
}

static void infer_throw(module_t *m, ast_throw_stmt_t *throw_stmt) {
    infer_right_expr(m, &throw_stmt->error, type_basic_new(TYPE_STRING));
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static type_t infer_tuple_destr(module_t *m, ast_tuple_destr_t *destr) {
    type_t t = type_basic_new(TYPE_TUPLE);
    t.tuple = NEW(type_tuple_t);
    t.tuple->elements = ct_list_new(sizeof(type_t));
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(destr->elements, i);
        type_t item_type = infer_left_expr(m, expr);
        ct_list_push(t.tuple->elements, &item_type);
    }

    return t;
}

/**
 * var (a, err) = xxx
 * 必须以 var 开头进行类型推断
 * tuple operand 的类型不能为 unknown, 且数量必须与 destr 一致
 * @param destr
 * @param t
 * @return
 */
static void infer_var_tuple_destr(module_t *m, ast_tuple_destr_t *destr, type_t t) {
    type_tuple_t *tuple_type = t.tuple;
    INFER_ASSERTF(destr->elements->length == tuple_type->elements->length,
                  "tuple destr length != tuple operand length");

    // 挨个对比
    for (int i = 0; i < destr->elements->length; ++i) {
        type_t *actual_type = ct_list_value(tuple_type->elements, i);
        INFER_ASSERTF(type_confirmed(*actual_type), "tuple operand index=%d type unknown");

        ast_expr_t *expr = ct_list_value(destr->elements, i);

        expr->type = *actual_type; // value is var_decl 不需要进行推导了
        if (expr->assert_type == AST_VAR_DECL) {
            // 直接推到出具体类型并回写到 operand 的 var_decl 中
            ast_var_decl_t *var_decl = expr->value;
            var_decl->type = *actual_type;
            rewrite_var_decl(m, var_decl);
        } else {
            INFER_ASSERTF(expr->assert_type == AST_EXPR_TUPLE_DESTR, "var tuple destr must var/tuple_destr");
            infer_var_tuple_destr(m, expr->value, *actual_type);
        }
    }
}

static void infer_var_tuple_def(module_t *m, ast_var_tuple_def_stmt_t *stmt) {
    // tuple 目前仅支持 var 形式的声明，所以此处和类型推导的形式一致
    type_t t = infer_right_expr(m, &stmt->right, type_basic_new(TYPE_UNKNOWN));
    assert(t.kind == TYPE_TUPLE);

    infer_var_tuple_destr(m, stmt->tuple_destr, t);
}


/**
 * @param m
 * @param tuple_new
 * @return
 */
static type_t infer_tuple_new(module_t *m, ast_tuple_new_t *tuple_new, type_t target_type) {
    type_t t = type_basic_new(TYPE_TUPLE);
    type_tuple_t *tuple_type = NEW(type_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(type_t));
    t.tuple = tuple_type;
    INFER_ASSERTF(tuple_new->elements->length > 0, "tuple elements empty");
    for (int i = 0; i < tuple_new->elements->length; ++i) {
        type_t element_target_type = type_basic_new(TYPE_UNKNOWN);
        if (target_type.kind == TYPE_TUPLE) {
            type_t *temp = ct_list_value(target_type.tuple->elements, i);
            element_target_type = *temp;
        }

        ast_expr_t *expr = ct_list_value(tuple_new->elements, i);
        type_t expr_type = infer_right_expr(m, expr, element_target_type);

        INFER_ASSERTF(type_confirmed(expr_type), "tuple element type type cannot confirmed");

        ct_list_push(tuple_type->elements, &expr_type);
    }

    return t;
}


static void infer_stmt(module_t *m, ast_stmt_t *stmt) {
    m->current_line = stmt->line;
    m->current_column = stmt->column;

    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return infer_var_decl(m, stmt->value);
        }
        case AST_STMT_VARDEF: {
            return infer_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return infer_var_tuple_def(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return infer_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            break;
        }
        case AST_CALL: {
            infer_call(m, stmt->value);
            break;
        }
        case AST_STMT_IF: {
            return infer_if(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return infer_for_cond_stmt(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return infer_for_iterator(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return infer_for_tradition(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return infer_throw(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return infer_return(m, stmt->value);
        }
        case AST_STMT_TYPE_ALIAS: {
            return infer_type_alias_stmt(m, stmt->value);
        }
        default: {
            return;
        }
    }
}

/**
 * 能作为左值的表达式有
 * a = 1
 * a[0] = 2
 * a.b = 3
 * @param m
 * @param expr
 * @return
 */
static type_t infer_left_expr(module_t *m, ast_expr_t *expr) {
    m->current_line = expr->line;
    m->current_column = expr->column;

    type_t type;
    switch (expr->assert_type) {
        case AST_EXPR_IDENT: {
            type = infer_ident(m, expr->value);
            break;
        }
        case AST_EXPR_TUPLE_DESTR: {
            type = infer_tuple_destr(m, expr->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            type = infer_access(m, expr);
            break;
        }
        case AST_EXPR_SELECT: {
            type = infer_select(m, expr);
            break;
        }
        case AST_EXPR_ENV_ACCESS: {
            type = infer_env_access(m, expr->value);
            break;
        }
        case AST_CALL: {
            type = infer_call(m, expr->value);
            break;
        }
        default: {
            INFER_ASSERTF(false, "operand assert=%d cannot used in left", expr->assert_type);
            exit(0);
        }
    }

    expr->type = type;
    return type;
}

/**
 * 大部分表达式都有一个 target 目标，如果需要做 implicit 类型转换，则需要将 target type 给到当前 operand
 * 如果 operand 没发转换成 target type, 则可以丢出类型不一致的报错
 *
 *  var a = 1 中 a 其实也是表达式 ast_ident, 其作为左值，原则上来说不需要作用目标
 * 表达式推断核心逻辑
 * @param expr
 * @return
 */
static type_t infer_right_expr(module_t *m, ast_expr_t *expr, type_t target_type) {
    m->current_line = expr->line;
    m->current_column = expr->column;

    type_t type;
    switch (expr->assert_type) {
        case AST_EXPR_AS: {
            type = infer_as_expr(m, expr);
            break;
        }
        case AST_EXPR_IS: {
            type = infer_is_expr(m, expr->value);
            break;
        }
        case AST_EXPR_BINARY: {
            type = infer_binary(m, expr->value);
            break;
        }
        case AST_EXPR_UNARY: {
            type = infer_unary(m, expr->value);
            break;
        }
        case AST_EXPR_IDENT: {
            type = infer_ident(m, expr->value);
            break;
        }
        case AST_EXPR_LIST_NEW: {
            type = infer_list_new(m, expr->value, target_type);
            break;
        }
        case AST_EXPR_MAP_NEW: {
            type = infer_map_new(m, expr->value, target_type);
            break;
        }
        case AST_EXPR_SET_NEW: {
            type = infer_set_new(m, expr->value, target_type);
            break;
        }
        case AST_EXPR_TUPLE_NEW: {
            type = infer_tuple_new(m, expr->value, target_type);
            break;
        }
        case AST_EXPR_STRUCT_NEW: {
            type = infer_struct_new(m, expr->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            // 这里需要做类型改写，确定具体的访问类型所以传递整个表达式
            type = infer_access(m, expr);
            break;
        }
        case AST_EXPR_SELECT: {
            type = infer_select(m, expr);
            break;
        }
        case AST_CALL: {
            type = infer_call(m, expr->value);
            break;
        }
        case AST_EXPR_TRY: {
            type = infer_try(m, expr->value);
            break;
        }
        case AST_FNDEF: {
            type = infer_fn_decl(m, expr->value);
            break;
        }
        case AST_EXPR_LITERAL: {
            type = infer_literal(m, expr->value);
            break;
        }
        case AST_EXPR_ENV_ACCESS: {
            type = infer_env_access(m, expr->value);
            break;
        }
        default: {
            INFER_ASSERTF(false, "unknown operand %d", expr->assert_type);
            exit(1);
        }
    }

    // 这里已经对表达式 type 做了调整
    target_type = reduction_type(m, target_type);
    expr->type = type;
    expr->target_type = target_type;

    // TYPE_UNKNOWN 表示需要进行类型推断
    if (target_type.kind == TYPE_UNKNOWN) {
        return expr->type;
    }

    if (target_type.kind == TYPE_VOID && expr->type.kind == TYPE_VOID) {
        return expr->type;
    }

    INFER_ASSERTF(expr->type.kind != TYPE_VOID, "cannot assign type void to %s", type_kind_str[target_type.kind]);

    // 如果 target_type 是 number, 并且 expr->assert_type 是字面量值，则进行编译时的字面量值判断与类型转换
    // 避免出现如 i8 foo = 1 as i8 这样的重复的在编译时就可以识别出来的转换
    if ((is_integer(target_type.kind) || target_type.kind == TYPE_CPTR) && expr->assert_type == AST_EXPR_LITERAL) {
        literal_integer_casting(m, expr, target_type);
    }

    if (is_float(target_type.kind) && expr->assert_type == AST_EXPR_LITERAL) {
        literal_float_casting(m, expr, target_type);
    }

    // single type to union type (必须保留)
    if (target_type.kind == TYPE_UNION && expr->type.kind != TYPE_UNION) {
        INFER_ASSERTF(union_type_contains(target_type, expr->type), "union type not contains '%s'",
                      type_kind_str[expr->type.kind]);
        *expr = ast_type_as(*expr, target_type);
    }

    INFER_ASSERTF(type_compare(target_type, expr->type), "type inconsistency, expect=%s, actual=%s",
                  type_kind_str[target_type.kind], type_kind_str[expr->type.kind]);
    return expr->type;
}

static type_t reduction_struct(module_t *m, type_t t) {
    INFER_ASSERTF(t.kind == TYPE_STRUCT, "type kind=%s unexpect", type_kind_str[t.kind]);

    type_struct_t *s = t.struct_;

    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        if (p->type.kind != TYPE_UNKNOWN) {
            p->type = reduction_type(m, p->type);
        }

        // 包含默认值
        if (p->right) {
            // 推断右值表达式类型(默认值推导)
            type_t right_type = infer_right_expr(m, p->right, p->type);
            if (p->type.kind == TYPE_UNKNOWN) {
                INFER_ASSERTF(type_confirmed(right_type), "struct property=%s type cannot confirmed", p->key);
                p->type = right_type;
            }
        }

        INFER_ASSERTF(type_confirmed(p->type), "struct property=%s type cannot confirmed", p->key);
        // 至此左值已经都是固定类型了, 如果存在 self 则 self 类型保持不变,self 不需要在这里处理
    }

    return t;
}

static type_t reduction_complex_type(module_t *m, type_t t) {
    if (t.kind == TYPE_POINTER) {
        type_pointer_t *type_pointer = t.pointer;
        type_pointer->value_type = reduction_type(m, type_pointer->value_type);
        return t;
    }

    if (t.kind == TYPE_LIST) {
        type_list_t *type_list = t.list;
        type_list->element_type = reduction_type(m, type_list->element_type);
        return t;
    }

    if (t.kind == TYPE_MAP) {
        t.map->key_type = reduction_type(m, t.map->key_type);
        t.map->value_type = reduction_type(m, t.map->value_type);
        INFER_ASSERTF(is_number(t.map->key_type.kind) ||
                      t.map->key_type.kind == TYPE_STRING ||
                      t.map->key_type.kind == TYPE_GEN,
                      "map key only support number/string");
        return t;
    }

    if (t.kind == TYPE_SET) {
        t.set->element_type = reduction_type(m, t.set->element_type);
        INFER_ASSERTF(is_number(t.set->element_type.kind) ||
                      t.set->element_type.kind == TYPE_STRING ||
                      t.set->element_type.kind == TYPE_GEN,
                      "set element only support number/string");
        return t;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        INFER_ASSERTF(tuple->elements->length > 0, "tuple element empty");
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *use = ct_list_value(tuple->elements, i);
            *use = reduction_type(m, *use);
        }
        return t;
    }

    // self 就 self 了,这里就不动 self 了,定义动时候也需要定义成 self!
    // 不能随便动名字
    if (t.kind == TYPE_FN) {
        type_fn_t *fn = t.fn;
        fn->return_type = reduction_type(m, fn->return_type);
        for (int i = 0; i < fn->formal_types->length; ++i) {
            type_t *formal_type = ct_list_value(fn->formal_types, i);
            *formal_type = reduction_type(m, *formal_type);
        }

        return t;
    }

    if (t.kind == TYPE_STRUCT) {
        return reduction_struct(m, t);
    }

    INFER_ASSERTF(false, "unknown type=%s", type_kind_str[t.kind]);
    exit(1);
}

/**
 * 泛型特化
 * @param m
 * @param ident
 * @param t
 */
static type_t generic_specialization(module_t *m, char *ident) {
    INFER_ASSERTF(m->infer_current->generic_assign,
                  "generic param must be used in global fn, cannot be directly used in local fn");
    type_t *assign = table_get(m->infer_current->generic_assign, ident);
    assert(assign);
    return reduction_type(m, *assign);
}

static type_t type_formal_specialization(module_t *m, type_t t) {
    assert(t.kind == TYPE_FORMAL);
    assert(m->type_actual_params);

    // 实参可以没有 reduction
    type_t *type = table_get(m->type_actual_params, t.formal->ident);
    return reduction_type(m, *type);
}

/**
 * custom_type a = ...
 * custom_type 此时就是一个 type_alias
 * @param m
 * @param t
 * @return
 */
static type_t reduction_type_alias(module_t *m, type_t t) {
    type_alias_t *alias = t.alias;
    symbol_t *symbol = symbol_table_get(alias->ident);
    INFER_ASSERTF(symbol, "type alias '%s' not found", alias->ident);
    INFER_ASSERTF(symbol->type == SYMBOL_TYPE_ALIAS, "'%s' is not a type", symbol->ident);

    ast_type_alias_stmt_t *type_alias_stmt = symbol->ast_value;

    // 判断是否包含 param, 如果包含 param 则需要每一次 reduction 都进行处理
    if (type_alias_stmt->formals) {
        assert(t.alias->actual_params);
        assert(t.alias->actual_params->length == type_alias_stmt->formals->length);

        m->type_actual_params = table_new();
        for (int i = 0; i < t.alias->actual_params->length; ++i) {
            type_t *actual_param = ct_list_value(t.alias->actual_params, i);
            ast_ident *formal_param = ct_list_value(type_alias_stmt->formals, i);
            table_set(m->type_actual_params, formal_param->literal, actual_param);
        }

        return reduction_type(m, type_copy(type_alias_stmt->type));
    }


    // 引用了 gen 类型的 alias, 现在需要进行泛型特化
    if (type_alias_stmt->type.kind == TYPE_GEN && !type_alias_stmt->type.gen->any) {
        return generic_specialization(m, type_alias_stmt->ident);
    }


    // 检查右值是否 reduce 完成
    if (type_alias_stmt->type.status == REDUCTION_STATUS_DONE) {
        return type_alias_stmt->type;
    }

    // 当前 ident 对应的 type 正在 reduction, 出现这种情况可能的原因是嵌套使用了 ident
    // 此时直接将 ident 丢回去就可以了
    if (type_alias_stmt->type.status == REDUCTION_STATUS_DOING) {
        return t;
    }

    type_alias_stmt->type.status = REDUCTION_STATUS_DOING; // 打上正在进行的标记,避免进入死循环
    type_alias_stmt->type = reduction_type(m, type_alias_stmt->type);

    return type_alias_stmt->type;
}

static type_t reduction_union_type(module_t *m, type_t t) {
    type_union_t *type_union = t.union_;
    if (type_union->any) {
        return t;
    }

    for (int i = 0; i < t.gen->elements->length; ++i) {
        type_t *temp = ct_list_value(t.gen->elements, i);
        *temp = reduction_type(m, *temp);
    }

    return t;
}

/**
 * - 验证 m->infer_current->generic_assign 是否包含该 t.ident, 不包含就直接 assert
 * - 取出对应的 assign 进行 reduction 并返回即可
 * @param m
 * @param t
 * @return
 */
static type_t reduction_generic_type(module_t *m, type_t t) {
    for (int i = 0; i < t.gen->elements->length; ++i) {
        type_t *temp = ct_list_value(t.gen->elements, i);
        *temp = reduction_type(m, *temp);
    }

    return t;
}

static type_t reduction_type(module_t *m, type_t t) {
//    m->current_line = t.line;
//    m->current_column = t.column;

    assert(t.kind > 0);

    if (t.kind == TYPE_UNKNOWN) {
        return t;
    }

    if (t.kind == TYPE_SELF) {
        t.status = REDUCTION_STATUS_DONE;
        t.in_heap = true;
        return t;
    }

    if (t.kind == TYPE_GEN) {
        t = reduction_generic_type(m, t);
        goto STATUS_DONE;
    }

    // 跳过已经完成 reduction 的 type
    if (t.status == REDUCTION_STATUS_DONE) {
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_ALIAS) {
        t = reduction_type_alias(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_FORMAL) {
        t = type_formal_specialization(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_UNION) {
        t = reduction_union_type(m, t);
        goto STATUS_DONE;
    }

    // 只有 typedef ident 才有中间状态的说法
    if (is_origin_type(t)) {
        goto STATUS_DONE;
    }

    // infer complex type
    if (is_reduction_type(t)) {
        t = reduction_complex_type(m, t);
        goto STATUS_DONE;
    }

    INFER_ASSERTF(false, "cannot parser type %s", type_kind_str[t.kind]);
    STATUS_DONE:
    t.status = REDUCTION_STATUS_DONE;
    t.in_heap = kind_in_heap(t.kind);

    // 计算 reflect type
    ct_reflect_type(t);
    return t;
}

/**
 * 对参数和返回值进行了类型推导，不包含 body
 * @param m
 * @param fndef
 */
static type_t infer_fn_decl(module_t *m, ast_fndef_t *fndef) {
    if (fndef->type.status == REDUCTION_STATUS_DONE) {
        return fndef->type;
    }

    // 对 fndef 进行类型还原
    type_fn_t *f = NEW(type_fn_t);

    f->name = fndef->symbol_name;
    f->formal_types = ct_list_new(sizeof(type_t));
    f->return_type = reduction_type(m, fndef->return_type);
    fndef->return_type = f->return_type;

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl_t *var = ct_list_value(fndef->formals, i);
        if (var->type.kind == TYPE_SELF) {
            INFER_ASSERTF(i == 0 && fndef->self_struct, "only use self in fn first param");
        }

        var->type = reduction_type(m, var->type);

        ct_list_push(f->formal_types, &var->type);
    }

    f->rest = fndef->rest_param;

    type_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_DONE;

    // 冗余一份，方便计算使用
    fndef->type = result;

    return result;
}

/**
 * 包含 body
 * @param m
 * @param fndef
 */
static void infer_fndef(module_t *m, ast_fndef_t *fndef) {
    // fndef params 已经进行了 reduction, 会同步影响到 symbol table 中的 fn
    type_t t = infer_fn_decl(m, fndef);

    // generic 阶段已经进行了平铺处理，所以这里会将遍历到所有的 fn->symbol 进行重写, 携带上 param_hash，且不用担心重复的问题
    // infer expr 中 fndef 确实会重复调用 infer_fn_decl 进行重复的推断
    rewrite_fndef(m, fndef);

    // rewrite_formals ident
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->formals, i);
        rewrite_var_decl(m, var_decl);
    }

    if (fndef->closure_name) {
        symbol_t *symbol = symbol_table_get(fndef->closure_name);
        INFER_ASSERTF(symbol, "fn var ident %s not found", fndef->closure_name);
        INFER_ASSERTF(symbol->type == SYMBOL_VAR, "symbol type not expected");

        ast_var_decl_t *var_decl = symbol->ast_value;
        var_decl->type = t;
    }

    // env 表达式类型还原
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr_t *env_expr = ct_list_value(fndef->capture_exprs, i);
        infer_left_expr(m, env_expr);
    }

    if (fndef->self_struct) {
        *fndef->self_struct = reduction_type(m, *fndef->self_struct);
    }

    // body infer
    infer_body(m, fndef->body);
}

void infer(module_t *m) {
    m->infer_current = NULL;
    m->current_line = 0;
    m->current_column = 0;

    // - 全局变量中也包含类型信息需要进行还原处理
    for (int j = 0; j < m->global_symbols->count; ++j) {
        symbol_t *s = m->global_symbols->take[j];
        if (s->type != SYMBOL_VAR) {
            continue;
        }

        infer_var_decl(m, s->ast_value); // 类型还原
    }

    // - 遍历所有 fndef 进行处理, 包含 global 和 local fn
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        m->infer_current = fndef;

        infer_fndef(m, fndef);
    }
}
