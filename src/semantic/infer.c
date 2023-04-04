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


static typedecl_t fn_to_type_decl(ast_fn_decl *fn_decl) {
    typedecl_fn_t *f = NEW(typedecl_fn_t);
    f->formal_types = ct_list_new(sizeof(typedecl_t));
    f->return_type = fn_decl->return_type;
    for (int i = 0; i < fn_decl->formals->length; ++i) {
        ast_var_decl *var_decl = ct_list_value(fn_decl->formals, i);
        ct_list_push(f->formal_types, &var_decl->type);
    }
    f->rest_param = fn_decl->rest_param;
    typedecl_t type = {
            .is_origin = false,
            .kind = TYPE_FN,
            .fn_decl = f
    };
    return type;
}


static typedecl_t infer_type_def(typedecl_ident_t *def) {
    // 符号表找到相关类型
    symbol_t *symbol = symbol_table_get(def->literal);
    if (symbol->type != SYMBOL_TYPE_DECL) {
        error_printf(infer_line, "'%s' is not a type", symbol->ident);
    }

    ast_typedef_stmt *type_decl_stmt = symbol->ast_value;

    // type_decl_stmt->ident 为自定义类型名称
    // type_decl_stmt->type 为引用的原始类型 int,my_int,struct....， 此时如果只有一次引用的话，实际上已经还原回去了
    typedecl_t origin_type = infer_type(type_decl_stmt->type);

    return origin_type;
}

/**
 * struct 允许顺序不通，但是 key 和 type 需要相同，在还原时需要根据 key 进行排序
 * 所有的类型数据都会经过该 fn 进行类型还原, 这里可以堆所有的 fn 进行 reflect 的计算以及注册
 * @param type
 * @return
 */
static typedecl_t infer_type(typedecl_t type) {
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

    // type foo = int, 'foo' is type_dec_ident
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
        for (int i = 0; i < type_decl_fn->formal_types->length; ++i) {
            typedecl_t *formal_type = ct_list_value(type_decl_fn->formal_types, i);
            *formal_type = infer_type(*formal_type);
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

static void infer_block(slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
#ifdef DEBUG_INFER
        debug_stmt("INFER", block->list[i]);
#endif

        // switch 结构导向优化
        infer_stmt(block->take[i]);
    }
}

/**
 * @param expr
 * @return
 */
static typedecl_t infer_binary(ast_binary_expr *expr) {
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
            error_exit("unknown operator type");
            exit(1);
        }
    }
}

/**
 * unary
 * @param expr
 * @return
 */
static typedecl_t infer_unary(ast_unary_expr *expr) {
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
static typedecl_t infer_ident(string unique_ident) {
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
        return infer_type(fn_to_type_decl(new_fn));
    }

    assertf(false, "symbol type not expect");
}

/**
 * [a, b(), c[1], d.foo]
 * @param new_list
 * @return 
 */
static typedecl_t infer_list_new(ast_list_new *new_list) {
    typedecl_t result = {
            .is_origin = true,
            .kind = TYPE_LIST,
    };
    typedecl_list_t *decl = NEW(typedecl_list_t);
    decl->element_type = type_base_new(TYPE_UNKNOWN); // unknown 可以适配任何类型

    for (int i = 0; i < new_list->values->length; ++i) {
        ast_expr *item_expr = ct_list_value(new_list->values, i);
        typedecl_t item_type = infer_expr(item_expr);

        // 无法根据 [...] 右值推断出具体的类型
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
 * @param map_new
 * @return
 */
static typedecl_t infer_map_new(ast_map_new *map_new) {
    typedecl_t result = type_base_new(TYPE_MAP);

    typedecl_map_t *map_decl = NEW(typedecl_map_t);
    map_decl->key_type = type_base_new(TYPE_UNKNOWN);
    map_decl->value_type = type_base_new(TYPE_UNKNOWN);
    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element *item = ct_list_value(map_new->elements, i);
        typedecl_t key_type = infer_expr(&item[i].key);
        typedecl_t value_type = infer_expr(&item[i].value);

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

    result.map_decl = map_decl;

    return result;
}

/**
 * {1, 2, a.b,value[1]}
 * @param set_new
 * @return
 */
static typedecl_t infer_set_new(ast_set_new *set_new) {
    typedecl_t result = type_base_new(TYPE_SET);

    typedecl_set_t *set_decl = NEW(typedecl_set_t);
    set_decl->key_type = type_base_new(TYPE_UNKNOWN);

    for (int i = 0; i < set_new->keys->length; ++i) {
        ast_expr *expr = ct_list_value(set_new->keys, i);
        typedecl_t key_type = infer_expr(expr);

        if (set_decl->key_type.kind == TYPE_UNKNOWN) {
            set_decl->key_type = key_type;
        } else {
            if (!type_compare(key_type, set_decl->key_type)) {
                set_decl->key_type = type_base_new(TYPE_ANY);
                break;
            }
        }

    }

    result.set_decl = set_decl;

    return result;
}

/**
 * 返回对应的 type
 * @param struct_decl
 * @param ident
 * @return
 */
static typedecl_t infer_struct_property_type(typedecl_struct_t *struct_decl, char *ident) {
    for (int i = 0; i < struct_decl->count; ++i) {
        if (strcmp(struct_decl->properties[i].key, ident) == 0) {
            return struct_decl->properties[i].type;
        }
    }

    assertf(0, "line=%d, not found struct key '%s'", infer_line, ident);
    exit(1);
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
static typedecl_t infer_struct_new(ast_struct_new_t *new_struct) {
    // 类型还原, struct ident 一定会被还原回 struct 原始结构
    // 如果本身已经是 struct 结构，那么期中的 struct key type 也会被还原成原始类型
    new_struct->type = infer_type(new_struct->type);

    if (new_struct->type.kind != TYPE_STRUCT) {
        error_printf(infer_line, "ident not struct");
    }

    typedecl_struct_t *struct_decl = new_struct->type.struct_decl;
    // new_struct->count 小于等于 struct_decl->count, 允许 new_struct 期间不进行赋值
    for (int i = 0; i < new_struct->properties->length; ++i) {
        ast_struct_property *struct_property = ct_list_value(new_struct->properties, i);

        // struct_decl 已经是被还原过的类型了
        typedecl_t expect_type = infer_struct_property_type(struct_decl, struct_property->key);
        typedecl_t actual_type = infer_expr(&struct_property->value);

        // expect type 并不允许为 var
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
 * a[b]  list/map/tuple 都将通过中括号的方式进行访问
 * @param expr
 * @return
 */
static typedecl_t infer_access(ast_expr *expr) {
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
        return map_decl->value_type;
    }

    if (left_type.kind == TYPE_LIST) {
        assertf(key_type.kind == TYPE_INT, "access list failed, index type must by int");

        ast_list_access_t *access_list = NEW(ast_list_access_t);
        typedecl_list_t *list_decl = left_type.list_decl;

        // 参数改写
        access_list->left = access->left;
        access_list->index = access->key;
        access_list->element_type = list_decl->element_type;
        expr->assert_type = AST_EXPR_LIST_ACCESS;
        expr->value = access_list;

        return list_decl->element_type;
    }

    if (left_type.kind == TYPE_TUPLE) {
        assertf(key_type.kind == TYPE_INT, "tuple index field type must int");
        assertf(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");
        typedecl_tuple_t *tuple_decl = left_type.tuple_decl;

        ast_literal *index_literal = access->key.value; // 读取 index 的值
        assertf(index_literal->kind == TYPE_INT, "tuple index field must int immediate value");
        uint64_t index = atoi(index_literal->value);

        assertf(index < tuple_decl->elements->length, "tuple index field '%d' not in tuples", index);

        // 返回值的类型, get tuple element.
        typedecl_t *typedecl = ct_list_value(tuple_decl->elements, index);

        ast_tuple_access_t *tuple_access = NEW(ast_tuple_access_t);
        tuple_access->left = access->left;
        tuple_access->index = index;
        tuple_access->element_type = *typedecl;
        expr->assert_type = AST_EXPR_TUPLE_ACCESS;
        expr->value = tuple_access;

        return *typedecl;
    }

    assertf(false, "line: %d, access left type must map/list/tuple, cannot '%s'", infer_line,
            type_kind_string[left_type.kind]);
    exit(1);
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * @param struct_access
 * @return
 */
static typedecl_t infer_struct_access(ast_struct_access *struct_access) {
    infer_expr(&struct_access->left);

    // infer 完成后？
    // TODO left 如果是 list 或者 map 或者 set 怎么处理？
    // TODO if left_type.kind == LIST left_type = TYPE_STRUCT (list)

    typedecl_t left_type = struct_access->left.type;


    assertf(left_type.kind == TYPE_STRUCT, "left not struct, cannot access key");

    typedecl_struct_t *struct_decl = left_type.struct_decl;
    for (int i = 0; i < struct_decl->count; ++i) {
        if (str_equal(struct_decl->properties[i].key, struct_access->key)) {
            struct_decl->properties[i].type = infer_type(struct_decl->properties[i].type);
            struct_access->property = struct_decl->properties[i];
            return struct_decl->properties[i].type;
        }
    }

    assertf(false, "cannot get struct key '%s'", struct_access->key);
    exit(1);
}

/**
 * @param call
 * @return
 */
static typedecl_t infer_call(ast_call *call) {
    // 左值符号推导
    typedecl_t left_type = infer_expr(&call->left);
    assertf(left_type.kind == TYPE_FN, "left right not fn, cannot call");

    typedecl_fn_t *type_fn = left_type.fn_decl;

    // 实参类型推导与类型还原
    for (int i = 0; i < call->actual_params->length; ++i) {
        infer_expr(ct_list_value(call->actual_params, i));  // right 类型还原，其中也包括 spread param
    }

    // 参数对比，由于存在 spread 和 rest 运算，所以不能直接根据参数数量左 assert
    uint8_t count = max(type_fn->formal_types->length, call->actual_params->length);

    for (int i = 0; i < count; ++i) {
        // first param from actual
        typedecl_t actual = select_actual_param(call, i);
        // first param from formal
        typedecl_t formal = select_formal_param(type_fn, i);
        assertf(type_compare(formal, actual), "call param[%d] type error, expect '%s' type, actual '%s' type", i,
                type_kind_string[formal.kind], type_kind_string[actual.kind]);

        // 如果 i < actual_param_count,则 actual_param 需要配置 target type
        bool is_spread = call->spread_param && i == call->actual_params->length - 1;
        if (i < call->actual_params->length && !is_spread) {
            ast_expr *expr = ct_list_value(call->actual_params, i);
            set_expr_target(expr, formal);
        }
    }

    return type_fn->return_type;
}


/**
 * (xx, errort)
 * @param catch
 * @return
 */
static typedecl_t infer_catch(ast_catch *catch) {
    typedecl_t t = type_base_new(TYPE_TUPLE);
    t.tuple_decl = NEW(typedecl_tuple_t);
    t.tuple_decl->elements = ct_list_new(sizeof(typedecl_t));

    typedecl_t call_return_type = infer_call(catch->call);
    ct_list_push(t.tuple_decl->elements, &call_return_type);

    typedecl_t errort = type_base_new(TYPE_IDENT);
    errort.ident_decl = NEW(typedecl_ident_t);
    errort.ident_decl->literal = BUILTIN_ERRORT;
    errort = infer_type(errort);

    ct_list_push(t.tuple_decl->elements, &errort);

    return t;
}


/**
 * int a;
 * float b;
 * @param var_decl
 */
static void infer_var_decl(ast_var_decl *var_decl) {
    var_decl->type = infer_type(var_decl->type);
    typedecl_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL) {
        error_printf(infer_line, "variable declarations cannot use '%s'", type_kind_string[type.kind]);
    }
}


/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param t
 * @return
 */
static bool type_confirmed(typedecl_t t) {
    if (t.kind == TYPE_UNKNOWN) {
        return false;
    }

    // var a = []  这样就完全不知道是个啥。。。
    if (t.kind == TYPE_LIST) {
        typedecl_list_t *list_decl = t.list_decl;
        if (list_decl->element_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (t.kind == TYPE_MAP) {
        typedecl_map_t *map_decl = t.map_decl;
        if (map_decl->key_type.kind == TYPE_UNKNOWN) {
            return false;
        }
        if (map_decl->value_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    return true;
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
static void infer_var_decl_assign(ast_var_assign_stmt *stmt) {
    typedecl_t expr_type = infer_expr(&stmt->right);

    // 开始类型推断, err 的类型大多数情况都是像这样推断出来的
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        assertf(type_confirmed(expr_type), "type inference error, right type not confirmed");
        stmt->var_decl.type = expr_type; // right type is origin type
        return;
    }

    stmt->var_decl.type = infer_type(stmt->var_decl.type);

    // 判断类型是否一致 compare
    assertf(type_compare(stmt->var_decl.type, expr_type),
            "line: %d, cannot assigned variables, because type inconsistency", infer_line);
    set_expr_target(&stmt->right, stmt->var_decl.type);
}

/**
 * @param stmt
 */
static void infer_assign(ast_assign_stmt *stmt) {
    typedecl_t target_type = infer_expr(&stmt->left);
    typedecl_t source_type = infer_expr(&stmt->right);

    assertf(type_compare(target_type, source_type), "line: %d, type inconsistency", infer_line);
    set_expr_target(&stmt->right, stmt->left.type);
}

static void infer_if(ast_if_stmt *stmt) {
    typedecl_t condition_type = infer_expr(&stmt->condition);
    assertf(type_compare(type_base_new(TYPE_BOOL), condition_type),
            "line: %d, if condition must bool type", infer_line);
    set_expr_target(&stmt->condition, type_base_new(TYPE_BOOL));

    infer_block(stmt->consequent);
    infer_block(stmt->alternate);
}

static void infer_for_cond_stmt(ast_for_cond_stmt *stmt) {
    typedecl_t cond_type = infer_expr(&stmt->condition);
    assertf(cond_type.kind == TYPE_BOOL, "for stmt condition must bool");

    infer_block(stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void infer_for_iterator(ast_for_iterator_stmt *stmt) {
    // 经过 infer_expr 的类型一定是已经被还原过的
    typedecl_t iterate_type = infer_expr(&stmt->iterate);

    assertf(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_LIST,
            "for in iterate type must be map/list, actual=%s", type_kind_string[iterate_type.kind]);

    // 类型推断 (value 可选)
    ast_var_decl key_decl = stmt->key;
    // 为 key_decl 添加 type
    if (iterate_type.kind == TYPE_MAP) {
        typedecl_map_t *map_decl = iterate_type.map_decl;
        key_decl.type = map_decl->key_type;
    } else {
        // list
        key_decl.type = type_base_new(TYPE_INT);
    }


    ast_var_decl *value_decl = stmt->value;
    if (value_decl) {
        if (iterate_type.kind == TYPE_MAP) {
            typedecl_map_t *map_decl = iterate_type.map_decl;
            value_decl->type = map_decl->value_type;
        } else {
            typedecl_list_t *list_decl = iterate_type.list_decl;
            value_decl->type = list_decl->element_type;

        }
    }

    infer_block(stmt->body);
}

static void infer_for_tradition(ast_for_tradition_stmt *stmt) {
    infer_stmt(stmt->init);
    typedecl_t cond_type = infer_expr(&stmt->cond);
    assertf(cond_type.kind == TYPE_BOOL, "for stmt condition must bool");
    infer_stmt(stmt->update);
    infer_block(stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void infer_return(ast_return_stmt *stmt) {
    typedecl_t return_type = type_base_new(TYPE_VOID);
    if (stmt->expr != NULL) {
        return_type = infer_expr(stmt->expr);
    }

    typedecl_t expect_type = infer_current->closure_decl->fn->return_type;
    assertf(type_compare(expect_type, return_type), "line: %d, return type '%s' failed,expect '%s' type", infer_line,
            type_kind_string[return_type.kind],
            type_kind_string[expect_type.kind]);
}

static infer_closure *infer_current_init(ast_closure_t *closure_decl) {
    infer_closure *new = malloc(sizeof(infer_closure));
    new->closure_decl = closure_decl;
    new->parent = infer_current;

    infer_current = new;
    return infer_current;
}

static typedecl_t infer_literal(ast_literal *literal) {
    return type_base_new(literal->kind);
}

static typedecl_t infer_access_env(ast_env_value *expr) {
    typedecl_t t = infer_ident(expr->unique_ident);
    return t;
}


static typedecl_t infer_closure_decl(ast_closure_t *closure_decl) {
    ast_fn_decl *function_decl = closure_decl->fn;

    // 类型还原
    function_decl->return_type = infer_type(function_decl->return_type);
    for (int i = 0; i < function_decl->formals->length; ++i) {
        ast_var_decl *var_decl = ct_list_value(function_decl->formals, i);
        var_decl->type = infer_type(var_decl->type);
    }

    // env 表达式也有类型，需要还原
    for (int i = 0; i < closure_decl->env_list->length; ++i) {
        infer_expr(ct_list_value(closure_decl->env_list, i));
    }

    infer_current_init(closure_decl);
    typedecl_t result = fn_to_type_decl(function_decl);
    result = infer_type(result);

    infer_block(function_decl->body);

    infer_current = infer_current->parent;

    return result;
}

static void infer_throw(ast_throw_stmt *throw_stmt) {
    typedecl_t t = infer_expr(&throw_stmt->error);
    assertf(t.kind == TYPE_STRING, "only support throw string type");
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static typedecl_t infer_tuple_destr(ast_tuple_destr *destr) {
    typedecl_t t = type_base_new(TYPE_TUPLE);
    typedecl_tuple_t *tuple = NEW(typedecl_tuple_t);
    tuple->elements = ct_list_new(sizeof(typedecl_t));
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(destr->elements, i);
        typedecl_t item_type = infer_expr(expr);
        ct_list_push(tuple->elements, &item_type);
    }

    return t;
}

/**
 * var (a, err) = xxx
 * 必须以 var 开头进行类型推断
 * tuple expr 的类型不能为 unknown, 且数量必须与 destr 一致
 * @param destr
 * @param t
 * @return
 */
static void infer_var_tuple_destr(ast_tuple_destr *destr, typedecl_t t) {
    typedecl_tuple_t *tuple_type = t.tuple_decl;
    assertf(destr->elements->length == tuple_type->elements->length, "tuple destr length != tuple expr length");

    // 挨个对比
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(destr->elements, i);
        typedecl_t *actual_type = ct_list_value(tuple_type->elements, i);

        assertf(type_confirmed(*actual_type), "tuple expr index=%d type unknown");

        if (expr->assert_type == AST_VAR_DECL) {
            // 直接推到出具体类型
            ast_var_decl *var_decl = expr->value;
            var_decl->type = *actual_type;
        } else {
            assertf(expr->assert_type == AST_EXPR_TUPLE_DESTR, "tuple destr must var/tuple_destr");
            infer_var_tuple_destr(expr->value, *actual_type);
        }
    }
}

static void infer_var_tuple_destr_stmt(ast_var_tuple_destr_stmt *stmt) {
    typedecl_t expr_type = infer_expr(&stmt->right);

    infer_var_tuple_destr(stmt->tuple_destr, expr_type);
}

static typedecl_t infer_tuple_new(ast_tuple_new *tuple_new) {
    typedecl_t t = type_base_new(TYPE_TUPLE);
    typedecl_tuple_t *tuple_type = NEW(typedecl_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(typedecl_t));
    t.tuple_decl = tuple_type;

    for (int i = 0; i < tuple_new->elements->length; ++i) {
        ast_expr *expr = ct_list_value(tuple_new->elements, i);
        typedecl_t expr_type = infer_expr(expr);
        ct_list_push(tuple_type->elements, &expr_type);
    }

    return t;
}


static void infer_stmt(ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return infer_var_decl(stmt->value);
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            return infer_var_decl_assign(stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return infer_var_tuple_destr_stmt(stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return infer_assign(stmt->value);
        }
        case AST_CLOSURE_NEW: {
            infer_closure_decl(stmt->value);
            break;
        }
        case AST_CALL: {
            infer_call(stmt->value);
            break;
        }
        case AST_STMT_IF: {
            return infer_if(stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return infer_for_cond_stmt(stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return infer_for_iterator(stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return infer_for_tradition(stmt->value);
        }
        case AST_STMT_THROW: {
            return infer_throw(stmt->value);
        }
        case AST_STMT_RETURN: {
            return infer_return(stmt->value);
        }
        default: {
            return;
        }
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
            type = infer_binary(expr->value);
            break;
        }
        case AST_EXPR_UNARY: {
            type = infer_unary(expr->value);
            break;
        }
        case AST_EXPR_IDENT: {
            type = infer_ident(((ast_ident *) expr->value)->literal);
            break;
        }
        case AST_EXPR_TUPLE_DESTR: {
            type = infer_tuple_destr(expr->value);
            break;
        }
        case AST_EXPR_LIST_NEW: {
            type = infer_list_new(expr->value);
            break;
        }
        case AST_EXPR_MAP_NEW: {
            type = infer_map_new(expr->value);
            break;
        }
        case AST_EXPR_SET_NEW: {
            type = infer_set_new(expr->value);
            break;
        }
        case AST_EXPR_TUPLE_NEW: {
            type = infer_tuple_new(expr->value);
            break;
        }
        case AST_EXPR_STRUCT_NEW: {
            type = infer_struct_new(expr->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            // 这里需要做类型改写，确定具体的访问类型所以传递整个表达式
            type = infer_access(expr);
            break;
        }
        case AST_EXPR_STRUCT_ACCESS: {
            type = infer_struct_access(expr->value);
            break;
        }
        case AST_CALL: {
            type = infer_call(expr->value);
            break;
        }
        case AST_EXPR_CATCH: {
            type = infer_catch(expr->value);
            break;
        }
        case AST_CLOSURE_NEW: {
            type = infer_closure_decl(expr->value);
            break;
        }
        case AST_EXPR_LITERAL: {
            type = infer_literal(expr->value);
            break;
        }
        case AST_EXPR_ENV_VALUE: {
            type = infer_access_env(expr->value);
            break;
        }
        default: {
            assertf(false, "unknown expr %d", expr->assert_type);
            exit(1);
        }
    }

    expr->type = type;
    return expr->type;
}


void infer(ast_closure_t *closure_decl) {
    infer_line = 0;
    infer_closure_decl(closure_decl);
}
