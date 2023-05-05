#include <string.h>
#include "infer.h"
#include "utils/error.h"
#include "src/symbol/symbol.h"
#include "analyser.h"
#include "src/debug/debug.h"
#include "utils/helper.h"

static void infer_block(module_t *m, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
#ifdef DEBUG_INFER
        debug_stmt("INFER", block->list[i]);
#endif

        // switch 结构导向优化
        infer_stmt(m, block->take[i]);
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
static type_t infer_binary(module_t *m, ast_binary_expr *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    type_t left_type = infer_right_expr(m, &expr->left, type_basic_new(TYPE_UNKNOWN));
    type_t right_type = infer_right_expr(m, &expr->right, type_basic_new(TYPE_UNKNOWN));

    // 目前 binary 的两侧符号只支持 int 和 float
    assertf(is_number(left_type.kind) && is_number(right_type.kind),
            "binary operate only support number operand,actual operate=%s, operand=%s",
            ast_expr_op_str[expr->operator],
            type_kind_string[right_type.kind]);

    if (is_integer_operator(expr->operator)) {
        assertf(is_integer(left_type.kind) && is_integer(right_type.kind),
                "binary operator '%s' only integer operand",
                ast_expr_op_str[expr->operator]);
    }

    // 不需要类型提升，且返回的类型为 first 的类型
    if (expr->operator == AST_OP_LSHIFT || expr->operator == AST_OP_RSHIFT) {
        return left_type;
    }

    // 类型自动提升
    type_t target_type = number_type_lift(left_type.kind, right_type.kind);
    if (left_type.kind != target_type.kind) {
        expr->left = ast_type_convert(expr->left, target_type);
    }
    if (right_type.kind != target_type.kind) {
        expr->right = ast_type_convert(expr->right, target_type);
    }

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
            assertf(false, "unknown operator type");
            exit(1);
        }
    }
}

static type_t infer_type_convert(module_t *m, ast_type_convert_t *convert) {
    return reduction_type(m, convert->target_type);
}

/**
 * unary
 * @param expr
 * @return
 */
static type_t infer_unary(module_t *m, ast_unary_expr *expr) {
    if (expr->operator == AST_OP_NOT) {
        // bool 支持各种类型的 implicit type convert
        return infer_right_expr(m, &expr->operand, type_basic_new(TYPE_BOOL));
    }

    type_t type = infer_right_expr(m, &expr->operand, type_basic_new(TYPE_UNKNOWN));
    if ((expr->operator == AST_OP_NEG) && !is_number(type.kind)) {
        assertf(false, "neg operand must applies to int or float type");
    }

    return type;
}

/**
 * 参考 golang，声明是可能在使用之后的
 * @param expr
 * @return
 */
static type_t infer_ident(module_t *m, ast_ident *ident) {
    char *unique_ident = ident->literal;
    symbol_t *symbol = symbol_table_get(unique_ident);
    assertf(symbol, "ident %s not found", unique_ident);

    if (symbol->type == SYMBOL_VAR) {
        ast_var_decl *var_decl = symbol->ast_value;
        var_decl->type = reduction_type(m, var_decl->type); // 类型还原
        return var_decl->type;
    }

    // 比如 print 和 println 都已经注册在了符号表中
    if (symbol->type == SYMBOL_FN) {
        ast_fndef_t *fndef = symbol->ast_value;
        return infer_fndef_decl(m, fndef);
    }

    assertf(false, "symbol type not expect");
    exit(1);
}

/**
 * 这里如果有问题直接就退出了
 * [a, b(), c[1], d.foo]
 * @param list_new
 * @return 
 */
static type_t infer_list_new(module_t *m, ast_list_new *list_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_LIST);

    type_list_t *type_list = NEW(type_list_t);
    // 初始化时类型未知
    type_list->element_type = type_basic_new(TYPE_UNKNOWN);

    if (target_type.kind != TYPE_UNKNOWN) {
        // 如果 target 强制约定了类型则直接使用 target 的类型, 否则就自己推断
        // 考虑到 list_new 可能为空的情况，所以这里默认赋值一次
        type_list->element_type = target_type.list->element_type;
    }
    result.list = type_list;
    if (list_new->elements == 0) {
        return result;
    }

    // target 类型不确定则按首个元素类型进行推导
    if (target_type.kind == TYPE_UNKNOWN) {
        ast_expr *item_expr = ct_list_value(list_new->elements, 0);
        type_list->element_type = infer_right_expr(m, item_expr, type_basic_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < list_new->elements->length; ++i) {
        ast_expr *item_expr = ct_list_value(list_new->elements, i);
        infer_right_expr(m, item_expr, type_list->element_type);
    }

    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param map_new
 * @return
 */
static type_t infer_map_new(module_t *m, ast_map_new *map_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_MAP);

    type_map_t *type_map = NEW(type_map_t);
    type_map->key_type = type_basic_new(TYPE_UNKNOWN);
    type_map->value_type = type_basic_new(TYPE_UNKNOWN);

    if (target_type.kind != TYPE_UNKNOWN) {
        // 考虑到 map 可能为空的情况，所以这里默认赋值一次, 如果为空就直接使用 target 的类型
        type_map->key_type = target_type.map->key_type;
        type_map->value_type = target_type.map->value_type;
    }
    result.map = type_map;
    if (map_new->elements == 0) {
        return result;
    }

    // target 类型不确定则按首个元素类型进行推导
    if (target_type.kind == TYPE_UNKNOWN) {
        ast_map_element *item = ct_list_value(map_new->elements, 0);
        type_map->key_type = infer_right_expr(m, &item->key, type_basic_new(TYPE_UNKNOWN));
        type_map->value_type = infer_right_expr(m, &item->value, type_basic_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element *item = ct_list_value(map_new->elements, i);
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
static type_t infer_set_new(module_t *m, ast_set_new *set_new, type_t target_type) {
    type_t result = type_basic_new(TYPE_SET);

    type_set_t *type_set = NEW(type_set_t);
    type_set->element_type = type_basic_new(TYPE_UNKNOWN);

    // 右值如果有推荐的类型，则基于推荐类型做 infer, 此时可能会触发类型转换
    if (target_type.kind != TYPE_UNKNOWN) {
        type_set->element_type = target_type.set->element_type;
    }

    result.set = type_set;
    if (set_new->elements == 0) {
        return result;
    }
    // target 类型不确定则按首个元素类型进行推导
    if (target_type.kind == TYPE_UNKNOWN) {
        ast_expr *item_expr = ct_list_value(set_new->elements, 0);
        type_set->element_type = infer_right_expr(m, item_expr, type_basic_new(TYPE_UNKNOWN));
    }
    for (int i = 0; i < set_new->elements->length; ++i) {
        ast_expr *expr = ct_list_value(set_new->elements, i);
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

    assertf(ast->type.kind == TYPE_STRUCT, "ident not struct, cannot struct new");

    type_struct_t *struct_decl = ast->type.struct_;

    table_t *exists = table_new();
    // exists key
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *struct_property = ct_list_value(ast->properties, i);
        struct_property_t *type_property = ct_list_value(struct_decl->properties, i);

        // type 冗余,方便计算 size (不能用来计算 offset)
        struct_property->type = type_property->type;

        table_set(exists, struct_property->key, struct_property);

        // struct_decl 已经是被还原过的类型了
        infer_right_expr(m, struct_property->right, type_property->type);
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
static type_t infer_access(module_t *m, ast_expr *expr) {
    ast_access *access = expr->value;
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

        assertf(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");

        type_tuple_t *type_tuple = left_type.tuple;

        ast_literal *index_literal = access->key.value; // 读取 index 的值
        assertf(index_literal->kind == TYPE_INT, "tuple index field must int immediate value");
        uint64_t index = atoi(index_literal->value);

        assertf(index < type_tuple->elements->length, "tuple index field '%d' not in tuples", index);

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

    assertf(false, "line: %d, access only support must map/list/tuple, cannot '%s'",
            m->infer_line,
            type_kind_string[left_type.kind]);
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
static type_t infer_select(module_t *m, ast_expr *expr) {
    ast_select *select = expr->value;

    infer_left_expr(m, &select->left);

    // self type to select
    // self_select -> instance_select
    if (select->left.type.kind == TYPE_SELF) {
        ast_fndef_t *current = m->infer_current;
        assertf(current->self_struct, "use 'self' in struct outside");

        // 当前 select 必定在 fn body 中，而处理 fn body 之前， fn.self_struct 在处理 body 之前已经进行了还原
        select->left.type = reduction_type(m, *current->self_struct);
    }

    // ast_access to ast_struct_access
    if (select->left.type.kind == TYPE_STRUCT) {
        // 经过上面对 infer_right_expr, 这里对 type 一定是 reduction 的
        type_struct_t *type_struct = select->left.type.struct_;
        struct_property_t *p = type_struct_property(type_struct, select->key);
        assertf(p, "type %s struct no property '%s'", type_struct->ident, select->key);

        // 改写
        ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
        struct_select->left = select->left;
        struct_select->key = select->key;
        struct_select->property = p;
        expr->assert_type = AST_EXPR_STRUCT_SELECT;
        expr->value = struct_select;

        return p->type;
    }

    assertf(false, "type '%s' cannot use dot syntax", type_kind_string[select->left.type.kind]);
    exit(1);
}

/**
 * 对 call 参数验证后对 call 进行改写
 * @param m
 * @param call
 * @return
 */
static type_t infer_list_select_call(module_t *m, ast_call *call) {
    ast_select *s = call->left.value;
    type_list_t *list_type = s->left.type.list; // 已经进行过类型推导了

    if (str_equal(s->key, LIST_PUSH_KEY)) {
        // push 对参数需要与 list element type 一致，否则抛出异常
        assertf(call->actual_params->length == 1, "list push param failed");
        ast_expr *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, list_type->element_type);

        // 参数核验完成，对整个 call 进行改写, 改写成 list_push(l, value_ref)
        // 参数重写
        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left); // list operand
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA)); // value operand

        call->left = *ast_ident_expr(RT_CALL_LIST_PUSH);
        infer_left_expr(m, &call->left); // 对 ident 进行推导计算出其类型
        call->return_type = type_basic_new(TYPE_VOID);

        // list_push() 返回 void
        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, LIST_LENGTH_KEY)) {
        assertf(call->actual_params->length == 0, "list length not param");

        // 改写
        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left); // list operand

        call->left = *ast_ident_expr(RT_CALL_LIST_LENGTH);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }


    assertf(false, "list not field '%s'", s->key);
    exit(0);
}

/**
 * 需要做值改写,所以这里需要将
 * @param m
 * @param call
 * @return
 */
static type_t infer_map_select_call(module_t *m, ast_call *call) {
    ast_select *s = call->left.value;
    type_map_t *map_type = s->left.type.map; // 已经进行过类型推导了
    if (str_equal(s->key, MAP_DELETE_KEY)) {
        assertf(call->actual_params->length == 1, "map.delete param failed");
        ast_expr *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, map_type->key_type);

        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_MAP_DELETE);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_VOID);

        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, LIST_LENGTH_KEY)) {
        assertf(call->actual_params->length == 0, "map.length not param");

        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left);

        call->left = *ast_ident_expr(RT_CALL_MAP_LENGTH);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_INT);

        return type_basic_new(TYPE_INT);
    }

    assertf(false, "map not field '%s'", s->key);
    exit(0);
}

static type_t infer_set_select_call(module_t *m, ast_call *call) {
    ast_select *s = call->left.value;
    type_set_t *set_type = s->left.type.set; // 已经进行过类型推导了
    if (str_equal(s->key, SET_DELETE_KEY)) {
        assertf(call->actual_params->length == 1, "set.delete param failed");
        ast_expr *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_DELETE);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_VOID);

        return type_basic_new(TYPE_VOID);
    }

    if (str_equal(s->key, SET_ADD_KEY)) {
        assertf(call->actual_params->length == 1, "set.add param failed");
        ast_expr *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        // s = left.key() 这里到 left 才是目标即可
        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_ADD);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_BOOL);

        return type_basic_new(TYPE_BOOL);
    }

    if (str_equal(s->key, SET_CONTAINS_KEY)) {
        assertf(call->actual_params->length == 1, "set.contains param failed");
        ast_expr *expr = ct_list_value(call->actual_params, 0);
        infer_right_expr(m, expr, set_type->element_type);

        call->actual_params = ct_list_new(sizeof(ast_expr));
        ct_list_push(call->actual_params, &s->left);
        ct_list_push(call->actual_params, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(RT_CALL_SET_CONTAINS);
        infer_left_expr(m, &call->left);
        call->return_type = type_basic_new(TYPE_BOOL);

        return type_basic_new(TYPE_BOOL);
    }

    assertf(false, "set not field '%s'", s->key);
    exit(1);
}

static void infer_call_params(module_t *m, ast_call *call, type_fn_t *target_type_fn) {
    // 由于支持 fndef rest 语言，所以实参的数量应该大于等于形参的数量
    if (!target_type_fn->rest) {
        assertf(call->actual_params->length == target_type_fn->formal_types->length, "call params count failed");
    }

    for (int i = 0; i < call->actual_params->length; ++i) {
        // first param from formal
        type_t formal_target_type = select_formal_param(target_type_fn, i);
        ast_expr *actual_param = ct_list_value(call->actual_params, i);
        if (i == 0 && formal_target_type.kind == TYPE_SELF) {
            // select first param 是 infer 自己伪造的，所以这里不需要在进行校验了
            continue;
        }

        infer_right_expr(m, actual_param, formal_target_type);
    }
}


/**
 * if call first param type is self
 * fn struct.call(param1) -> struct.key(struct, param1) 即可
 * @param m
 * @param call
 * @return
 */
static type_t infer_struct_select_call(module_t *m, ast_call *call) {
    ast_select *s = call->left.value;
    type_struct_t *type_struct = s->left.type.struct_; // 已经进行过类型推导了
    struct_property_t *p = type_struct_property(type_struct, s->key);
    assertf(p, "type %s struct no property '%s'", type_struct->ident, s->key);

    // call left 改写
    ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
    struct_select->left = s->left;
    struct_select->key = s->key;
    struct_select->property = p;
    call->left.assert_type = AST_EXPR_STRUCT_SELECT;
    call->left.value = struct_select;
    call->left.type = p->type;

    // 进入前已经进行了 infer left, 所以这里的 type 都是 reduction 过的
    assertf(p->type.kind == TYPE_FN, "cannot call non-fn");
    type_fn_t *type_fn = p->type.fn;

    type_t *first = ct_list_value(type_fn->formal_types, 0);
    if (first->kind != TYPE_SELF) {
        infer_call_params(m, call, type_fn);
        return type_fn->return_type;
    }

    // formal 的首个参数是 self, 且 self 未经过推断
    list_t *actual_params = call->actual_params;
    call->actual_params = ct_list_new(sizeof(ast_expr));
    ct_list_push(call->actual_params, &struct_select->left);
    for (int i = 0; i < actual_params->length; ++i) {
        ct_list_push(call->actual_params, ct_list_value(actual_params, i));
    }
    infer_call_params(m, call, type_fn);

    call->return_type = type_fn->return_type;
    return type_fn->return_type;
}

/**
 * 无论是什么类型对 select call 都需要走定制处理
 * @param call
 * @return
 */
static type_t infer_call(module_t *m, ast_call *call) {
    if (call->left.assert_type == AST_EXPR_SELECT) {
        ast_select *select = call->left.value;
        // 这里已经对 left 进行了类型推导，所以后续不需要在进行类型推导了
        infer_right_expr(m, &select->left, type_basic_new(TYPE_UNKNOWN));
        // self 快速改写
        if (select->left.type.kind == TYPE_SELF) {
            ast_fndef_t *current = m->infer_current;
            assertf(current->self_struct, "use 'self' in struct outside");

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

        if (select_left_kind == TYPE_STRUCT) {
            return infer_struct_select_call(m, call);
        }

        assertf(false, "select dot call not support type=%s", type_kind_string[select_left_kind]);
    }


    // 左值符号推导
    type_t left_type = infer_left_expr(m, &call->left);
    assertf(left_type.kind == TYPE_FN, "cannot call non-fn");
    type_fn_t *type_fn = left_type.fn;

    infer_call_params(m, call, type_fn);

    call->return_type = type_fn->return_type;
    return type_fn->return_type;
}


/**
 * var (foo, error) = catch foo()
 * var error = catch foo()
 * @param catch
 * @return
 */
static type_t infer_catch(module_t *m, ast_catch *catch) {

    type_t return_type = infer_call(m, catch->call);
    type_t errort = type_new(TYPE_IDENT, NULL);
    errort.ident = NEW(type_ident_t);
    errort.ident->literal = ERRORT_TYPE_IDENT;
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
 * int a;
 * float b;
 * @param var_decl
 */
void infer_var_decl(module_t *m, ast_var_decl *var_decl) {
    var_decl->type = reduction_type(m, var_decl->type);
    type_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL || type.kind == TYPE_SELF) {
        assertf(false, "variable declaration cannot use type '%s'", type_kind_string[type.kind]);
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
static void infer_vardef(module_t *m, ast_vardef_stmt *stmt) {
    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);

    type_t right_type = infer_right_expr(m, &stmt->right, stmt->var_decl.type);

    // 需要进行类型推断
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        assertf(type_confirmed(right_type), "type inference error, right type not confirmed");

        stmt->var_decl.type = right_type;
        return;
    }
}

/**
 * @param stmt
 */
static void infer_assign(module_t *m, ast_assign_stmt *stmt) {
    type_t left_type = infer_left_expr(m, &stmt->left);
    infer_right_expr(m, &stmt->right, left_type);
}

static void infer_if(module_t *m, ast_if_stmt *stmt) {
    infer_right_expr(m, &stmt->condition, type_basic_new(TYPE_BOOL));

    infer_block(m, stmt->consequent);
    infer_block(m, stmt->alternate);
}

static void infer_for_cond_stmt(module_t *m, ast_for_cond_stmt *stmt) {
    infer_right_expr(m, &stmt->condition, type_basic_new(TYPE_BOOL));

    infer_block(m, stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void infer_for_iterator(module_t *m, ast_for_iterator_stmt *stmt) {
    // 经过 infer_right_expr 的类型一定是已经被还原过的
    type_t iterate_type = infer_right_expr(m, &stmt->iterate, type_basic_new(TYPE_UNKNOWN));
    assertf(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_LIST,
            "for in iterate type must be map/list, actual=%s", type_kind_string[iterate_type.kind]);

    // 类型推断 (value 可选)
    ast_var_decl *key_decl = &stmt->key;
    // 为 key_decl 添加 type
    if (iterate_type.kind == TYPE_MAP) {
        type_map_t *map_decl = iterate_type.map;
        key_decl->type = map_decl->key_type;
    } else {
        // list
        key_decl->type = type_basic_new(TYPE_INT);
    }

    ast_var_decl *value_decl = stmt->value;
    if (value_decl) {
        if (iterate_type.kind == TYPE_MAP) {
            type_map_t *map_decl = iterate_type.map;
            value_decl->type = map_decl->value_type;
        } else {
            type_list_t *list_decl = iterate_type.list;
            value_decl->type = list_decl->element_type;

        }
    }


    infer_block(m, stmt->body);
}

static void infer_for_tradition(module_t *m, ast_for_tradition_stmt *stmt) {
    infer_stmt(m, stmt->init);
    infer_right_expr(m, &stmt->cond, type_basic_new(TYPE_BOOL));
    infer_stmt(m, stmt->update);
    infer_block(m, stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void infer_return(module_t *m, ast_return_stmt *stmt) {
    type_t expect_type = m->infer_current->return_type;
    if (stmt->expr != NULL) {
        infer_right_expr(m, stmt->expr, expect_type);
    } else {
        assertf(expect_type.kind == TYPE_VOID, "fn expect return type: %s", type_kind_string[expect_type.kind]);
    }
}

static type_t infer_literal(module_t *m, ast_literal *literal) {
    return type_basic_new(literal->kind);
}

static type_t infer_env_access(module_t *m, ast_env_access *expr) {
    ast_ident ident = {
            .literal = expr->unique_ident,
    };
    return infer_ident(m, &ident);
}

static void infer_throw(module_t *m, ast_throw_stmt *throw_stmt) {
    infer_right_expr(m, &throw_stmt->error, type_basic_new(TYPE_STRING));
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static type_t infer_tuple_destr(module_t *m, ast_tuple_destr *destr) {
    type_t t = type_basic_new(TYPE_TUPLE);
    t.tuple = NEW(type_tuple_t);
    t.tuple->elements = ct_list_new(sizeof(type_t));
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(destr->elements, i);
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
static void infer_var_tuple_destr(module_t *m, ast_tuple_destr *destr, type_t t) {
    type_tuple_t *tuple_type = t.tuple;
    assertf(destr->elements->length == tuple_type->elements->length, "tuple destr length != tuple operand length");

    // 挨个对比
    for (int i = 0; i < destr->elements->length; ++i) {
        type_t *actual_type = ct_list_value(tuple_type->elements, i);
        assertf(type_confirmed(*actual_type), "tuple operand index=%d type unknown");

        ast_expr *expr = ct_list_value(destr->elements, i);

        expr->type = *actual_type; // value is var_decl 不需要进行推导了
        if (expr->assert_type == AST_VAR_DECL) {
            // 直接推到出具体类型并回写到 operand 的 var_decl 中
            ast_var_decl *var_decl = expr->value;
            var_decl->type = *actual_type;
        } else {
            assertf(expr->assert_type == AST_EXPR_TUPLE_DESTR, "var tuple destr must var/tuple_destr");
            infer_var_tuple_destr(m, expr->value, *actual_type);
        }
    }
}

static void infer_var_tuple_def(module_t *m, ast_var_tuple_def_stmt *stmt) {
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
static type_t infer_tuple_new(module_t *m, ast_tuple_new *tuple_new, type_t target_type) {
    type_t t = type_basic_new(TYPE_TUPLE);
    type_tuple_t *tuple_type = NEW(type_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(type_t));
    t.tuple = tuple_type;
    assertf(tuple_new->elements->length > 0, "tuple elements emtpy");
    for (int i = 0; i < tuple_new->elements->length; ++i) {
        type_t element_target_type = type_basic_new(TYPE_UNKNOWN);
        if (target_type.kind != TYPE_UNKNOWN) {
            type_t *temp = ct_list_value(target_type.tuple->elements, i);
            element_target_type = *temp;
        }

        ast_expr *expr = ct_list_value(tuple_new->elements, i);
        type_t expr_type = infer_right_expr(m, expr, element_target_type);

        assertf(type_confirmed(expr_type), "tuple element type type cannot confirmed");

        ct_list_push(tuple_type->elements, &expr_type);
    }

    return t;
}


static void infer_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return infer_var_decl(m, stmt->value);
        }
        case AST_STMT_VAR_DEF: {
            return infer_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return infer_var_tuple_def(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return infer_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            infer_fndef_decl(m, stmt->value);
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
static type_t infer_left_expr(module_t *m, ast_expr *expr) {
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
            assertf(false, "operand assert=%d cannot used in left", expr->assert_type);
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
static type_t infer_right_expr(module_t *m, ast_expr *expr, type_t target_type) {
    type_t type;
    switch (expr->assert_type) {
        case AST_EXPR_TYPE_CONVERT: {
            type = infer_type_convert(m, expr->value);
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
        case AST_EXPR_CATCH: {
            type = infer_catch(m, expr->value);
            break;
        }
        case AST_FNDEF: {
            type = infer_fndef_decl(m, expr->value);
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
            assertf(false, "unknown operand %d", expr->assert_type);
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

    assertf(expr->type.kind != TYPE_VOID, "cannot assign type void to %s", type_kind_string[target_type.kind]);

    // 数值类型转换
    if (is_number(target_type.kind) && is_number(expr->type.kind) &&
        cross_kind_trans(expr->type.kind) != cross_kind_trans(target_type.kind)) {
        *expr = ast_type_convert(*expr, target_type);
    }

    // bool 类型转换
    if (target_type.kind == TYPE_BOOL && expr->type.kind != TYPE_BOOL) {
        *expr = ast_type_convert(*expr, type_basic_new(TYPE_BOOL));
    }

    if (target_type.kind == TYPE_ANY && expr->type.kind != TYPE_ANY) {
        *expr = ast_type_convert(*expr, target_type);
    }

    assertf(type_compare(target_type, expr->type), "line=%d, type inconsistency", expr->line);
    return expr->type;
}

static type_t reduction_struct(module_t *m, type_t t) {
    assertf(t.kind == TYPE_STRUCT, "type kind=%s unexpect", type_kind_string[t.kind]);

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
                assertf(type_confirmed(right_type), "struct property=%s type cannot confirmed", p->key);
                p->type = right_type;
            }
        }

        assertf(type_confirmed(p->type), "struct property=%s type cannot confirmed", p->key);
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
        assertf(is_number(t.map->key_type.kind) ||
                t.map->key_type.kind == TYPE_STRING ||
                t.map->key_type.kind == TYPE_GENERIC,
                "map key only support float/integer/string");
        return t;
    }

    if (t.kind == TYPE_SET) {
        t.set->element_type = reduction_type(m, t.set->element_type);
        return t;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        assertf(tuple->elements->length > 0, "tuple element empty");
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
        // 可选的返回类型
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

    assertf(false, "unknown type=%s", type_kind_string[t.kind]);
    exit(1);
}

static type_t reduction_type_ident(module_t *m, type_t t) {
    type_ident_t *ident = t.ident;
    symbol_t *symbol = symbol_table_get(ident->literal);


    assertf(symbol->type == SYMBOL_TYPEDEF, "'%s' is not a type", symbol->ident);
    ast_typedef_stmt *typedef_stmt = symbol->ast_value;

    assertf(m->reduction_typedef && typedef_stmt->type.status == REDUCTION_STATUS_DONE,
            "typedef stage exception, all typedef ident operand must done");

    // 检查右值是否 reduce 完成
    if (typedef_stmt->type.status == REDUCTION_STATUS_DONE) {
        return typedef_stmt->type;
    }

    // 当前 ident 对应的 type 正在 reduction, 出现这种情况可能的原因是嵌套使用了 ident
    // 此时直接将 ident 丢回去就可以了
    if (typedef_stmt->type.status == REDUCTION_STATUS_DOING) {
        return t;
    }

    typedef_stmt->type.status = REDUCTION_STATUS_DOING; // 打上正在进行的标记,避免进入死循环
    return reduction_type(m, typedef_stmt->type);
}

static type_t reduction_type(module_t *m, type_t t) {
    assert(t.kind > 0);

    if (t.kind == TYPE_UNKNOWN) {
        return t;
    }

    if (t.kind == TYPE_SELF) {
        t.status = REDUCTION_STATUS_DONE;
        t.in_heap = true;
        return t;
    }

    if (t.status == REDUCTION_STATUS_DONE) {
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_IDENT) {
        t = reduction_type_ident(m, t);
        goto STATUS_DONE;
    }

    // 只有 typedef ident 才有中间状态的说法
    if (is_basic_decl_type(t)) {
        goto STATUS_DONE;
    }

    // infer complex type
    if (is_complex_decl_type(t)) {
        t = reduction_complex_type(m, t);
        goto STATUS_DONE;
    }

    assertf(false, "cannot parser type %s", type_kind_string[t.kind]);
    STATUS_DONE:
    t.status = REDUCTION_STATUS_DONE;
    t.in_heap = kind_in_heap(t.kind);

    // 计算 reflect type
    ct_reflect_type(t);
    return t;
}

/**
 * 仅 infer 了 fn 对声明部分，不包含 body
 * @param m
 * @param fndef
 */
static type_t infer_fndef_decl(module_t *m, ast_fndef_t *fndef) {
    // 对 fndef 进行类型还原
    type_fn_t *f = NEW(type_fn_t);

    f->name = fndef->symbol_name;
    f->formal_types = ct_list_new(sizeof(type_t));
    f->return_type = reduction_type(m, fndef->return_type);

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var = ct_list_value(fndef->formals, i);
        if (var->type.kind == TYPE_SELF) {
            assertf(i == 0 && fndef->self_struct, "only use self in fn first param");
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
    m->infer_current = fndef;

    type_t t = infer_fndef_decl(m, fndef);
    if (fndef->closure_name) {
        symbol_t *symbol = symbol_table_get(fndef->closure_name);
        assertf(symbol, "fn var ident %s not found", fndef->closure_name);
        assert(symbol->type == SYMBOL_VAR);
        ast_var_decl *var_decl = symbol->ast_value;
        var_decl->type = t;
    }

    // env 表达式类型还原
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr *env_expr = ct_list_value(fndef->capture_exprs, i);
        infer_left_expr(m, env_expr);
    }

    if (fndef->self_struct) {
        *fndef->self_struct = reduction_type(m, *fndef->self_struct);
    }

    // body infer
    infer_block(m, fndef->body);
}

void infer(module_t *m) {
    m->infer_line = 0;

    // 1. 从符号表中遍历所有 typedef 进行预处理
    for (int i = 0; i < symbol_typedef_list->count; ++i) {
        symbol_t *symbol = symbol_typedef_list->take[i];
        ast_typedef_stmt *typedef_stmt = symbol->ast_value;
        typedef_stmt->type.status = REDUCTION_STATUS_DOING;
        // 符号表覆写
        typedef_stmt->type = reduction_type(m, typedef_stmt->type);
    }

    // 所有的 typedef 都已经还原完成
    m->reduction_typedef = true;

    // 2. 遍历所有 closure_fndefs 进行 infer 处理(包含 body)
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        infer_fndef(m, fndef);
    }
}
