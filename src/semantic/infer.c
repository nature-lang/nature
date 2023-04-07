#include <string.h>
#include "infer.h"
#include "utils/error.h"
#include "src/symbol/symbol.h"
#include "analysis.h"
#include "src/debug/debug.h"
#include "utils/helper.h"

static void set_expr_target(ast_expr *source_expr, typeuse_t target_type) {
    source_expr->target_type = target_type;
}

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
 * @param expr
 * @return
 */
static typeuse_t infer_binary(module_t *m, ast_binary_expr *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    typeuse_t left_type = infer_expr(m, &expr->left);
    typeuse_t right_type = infer_expr(m, &expr->right);

    // 目前 binary 的两侧符号只支持 int 和 float， TODO string + string 支持
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
            assertf(false, "unknown operator type");
            exit(1);
        }
    }
}

/**
 * unary
 * @param expr
 * @return
 */
static typeuse_t infer_unary(module_t *m, ast_unary_expr *expr) {
    typeuse_t operand_type = infer_expr(m, &expr->operand);

    // !
    if (expr->operator == AST_EXPR_OPERATOR_NOT && operand_type.kind != TYPE_BOOL) {
        assertf(false, "not operand must applies to bool type");
    }

    // -
    if ((expr->operator == AST_EXPR_OPERATOR_NEG) && operand_type.kind != TYPE_INT
        && operand_type.kind != TYPE_FLOAT) {
        assertf(false, "neg operand must applies to int or float type");
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
static typeuse_t infer_ident(module_t *m, ast_ident *ident) {
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
}

/**
 * [a, b(), c[1], d.foo]
 * @param list_new
 * @return 
 */
static typeuse_t infer_list_new(module_t *m, ast_list_new *list_new) {
    typeuse_t result = type_base_new(TYPE_LIST);

    type_list_t *type_list = NEW(type_list_t);
    // 初始化时类型未知
    type_list->element_type = type_base_new(TYPE_UNKNOWN);

    for (int i = 0; i < list_new->values->length; ++i) {
        ast_expr *item_expr = ct_list_value(list_new->values, i);
        typeuse_t item_type = infer_expr(m, item_expr);

        // 无法根据 [...] 右值推断出具体的类型
        if (type_list->element_type.kind == TYPE_UNKNOWN) {
            // 数组已经添加了初始化值，可以添加一种有值进行类型推导了
            type_list->element_type = item_type;
        } else {
            if (!type_compare(item_type, type_list->element_type)) {
                // 出现了多种类型，无法推导出具体的类型，可以暂定为 any, 并退出右值类型推导
                type_list->element_type = type_base_new(TYPE_ANY);
                break;
            }
        }
    }
    // list type 冗余,便于 compiler
    list_new->type = result;

    result.list = type_list;
    return result;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param map_new
 * @return
 */
static typeuse_t infer_map_new(module_t *m, ast_map_new *map_new) {
    typeuse_t result = type_base_new(TYPE_MAP);

    type_map_t *type_map = NEW(type_map_t);
    type_map->key_type = type_base_new(TYPE_UNKNOWN);
    type_map->value_type = type_base_new(TYPE_UNKNOWN);
    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element *item = ct_list_value(map_new->elements, i);
        typeuse_t key_type = infer_expr(m, &item[i].key);
        typeuse_t value_type = infer_expr(m, &item[i].value);

        // key
        if (type_map->key_type.kind == TYPE_UNKNOWN) {
            type_map->key_type = key_type;
        } else {
            if (!type_compare(key_type, type_map->key_type)) {
                type_map->key_type = type_base_new(TYPE_ANY);
                break;
            }
        }

        // value
        if (type_map->value_type.kind == TYPE_UNKNOWN) {
            type_map->value_type = value_type;
        } else {
            if (!type_compare(value_type, type_map->value_type)) {
                type_map->value_type = type_base_new(TYPE_ANY);
                break;
            }
        }
    }

    result.map = type_map;
    return result;
}

/**
 * {1, 2, a.b,value[1]}
 * @param set_new
 * @return
 */
static typeuse_t infer_set_new(module_t *m, ast_set_new *set_new) {
    typeuse_t result = type_base_new(TYPE_SET);

    type_set_t *set_decl = NEW(type_set_t);
    set_decl->key_type = type_base_new(TYPE_UNKNOWN);

    for (int i = 0; i < set_new->keys->length; ++i) {
        ast_expr *expr = ct_list_value(set_new->keys, i);
        typeuse_t key_type = infer_expr(m, expr);

        if (set_decl->key_type.kind == TYPE_UNKNOWN) {
            set_decl->key_type = key_type;
        } else {
            if (!type_compare(key_type, set_decl->key_type)) {
                set_decl->key_type = type_base_new(TYPE_ANY);
                break;
            }
        }

    }

    result.set = set_decl;
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
 * @param s
 * @return
 */
static typeuse_t infer_struct_new(module_t *m, ast_struct_new_t *s) {
    // person to struct
    s->type = reduction_type(m, s->type);

    assertf(s->type.kind == TYPE_STRUCT, "ident not struct, cannot new instance");

    type_struct_t *struct_decl = s->type.struct_;

    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *struct_property = ct_list_value(s->properties, i);

        // struct_decl 已经是被还原过的类型了
        typeuse_t expect_type = struct_property->type;
        typeuse_t actual_type = infer_expr(m, struct_property->right);

        // expect type 并不允许为 var
        assertf(type_compare(actual_type, expect_type),
                "key '%s' expect '%s' type, actual '%s' type",
                struct_property->key,
                type_kind_string[expect_type.kind],
                type_kind_string[actual_type.kind]);
    }

    return s->type;
}

/**
 * a[b]  list/map/tuple 都将通过中括号的方式进行访问
 * @param expr
 * @return
 */
static typeuse_t infer_access(module_t *m, ast_expr *expr) {
    ast_access *access = expr->value;
    typeuse_t left_type = infer_expr(m, &access->left);
    typeuse_t key_type = infer_expr(m, &access->key);

    // ast_access to ast_map_access
    if (left_type.kind == TYPE_MAP) {
        ast_map_access_t *map_access = malloc(sizeof(ast_map_access_t));
        type_map_t *type_map = left_type.map;

        // 参数改写
        map_access->left = access->left;
        map_access->key = access->key;

        // access_map 冗余字段处理
        map_access->key_type = type_map->key_type;
        map_access->value_type = type_map->value_type;
        expr->assert_type = AST_EXPR_MAP_ACCESS;
        expr->value = map_access;

        // 返回值
        return type_map->value_type;
    }

    if (left_type.kind == TYPE_LIST) {
        assertf(key_type.kind == TYPE_INT, "access list failed, index type must by int");

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
        assertf(key_type.kind == TYPE_INT, "tuple index field type must int");
        assertf(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");
        type_tuple_t *type_tuple = left_type.tuple;

        ast_literal *index_literal = access->key.value; // 读取 index 的值
        assertf(index_literal->kind == TYPE_INT, "tuple index field must int immediate value");
        uint64_t index = atoi(index_literal->value);

        assertf(index < type_tuple->elements->length, "tuple index field '%d' not in tuples", index);

        // 返回值的类型, get tuple element.
        typeuse_t *type = ct_list_value(type_tuple->elements, index);

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
 * list.push
 * list.length
 * @param m
 * @param l
 * @return
 */
static typeuse_t infer_list_select(module_t *m, ast_list_select_t *s) {
    if (str_equal(s->key, "push")) {
        return type_base_new(TYPE_VOID);
    }
    if (str_equal(s->key, "length")) {
        return type_base_new(TYPE_INT);
    }

    assertf(false, "list not field '%s'", s->key);
}

static typeuse_t infer_map_select(module_t *m, ast_map_select_t *s) {
    // map.delete(key)
    if (str_equal(s->key, "delete")) {
        return type_base_new(TYPE_VOID);
    }

    // map.length
    if (str_equal(s->key, "length")) {
        return type_base_new(TYPE_INT);
    }

    assertf(false, "map not field '%s'", s->key);
}

static typeuse_t infer_set_select(module_t *m, ast_map_select_t *s) {
    if (str_equal(s->key, "contains")) {
        return type_base_new(TYPE_BOOL);
    }

    if (str_equal(s->key, "add")) {
        return type_base_new(TYPE_VOID);
    }

    if (str_equal(s->key, "delete")) {
        return type_base_new(TYPE_VOID);
    }

    assertf(false, "set not field '%s'", s->key);
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * self.test
 * @param select
 * @return
 */
static typeuse_t infer_select(module_t *m, ast_expr *expr) {
    ast_select *select = expr->value;

    select->left.type = infer_expr(m, &select->left);

    typeuse_t left_type = select->left.type;
    // self_select -> instance_select
    if (left_type.kind == TYPE_SELF) {
        ast_fndef_t *current = m->infer_current;
        assertf(current->self_struct, "use 'self' in struct outside");

        // 当前 select 必定在 fn body 中，而处理 fn body 之前， fn.self_struct 在处理 body 之前已经进行了还原
        left_type = *current->self_struct;
    }

    // ast_access to ast_struct_access
    if (left_type.kind == TYPE_STRUCT) {
        // 经过上面对 infer_expr, 这里对 type 一定是 reduction 的
        type_struct_t *type_struct = left_type.struct_;
        struct_property_t *p = type_struct_property(type_struct, select->key);
        assertf(p, "type %s struct no property '%s'", type_struct->ident, select->key);

        // 改写
        ast_instance_select_t *struct_select = NEW(ast_instance_select_t);
        struct_select->left = select->left;
        struct_select->key = select->key;
        struct_select->property = p;
        expr->assert_type = AST_EXPR_STRUCT_SELECT;
        expr->value = struct_select;

        return p->type;
    }

    // [1].push(2) / {1:2, 3:4}.length / {1, 2}.contains(12)
    if (left_type.kind == TYPE_LIST) {
        expr->assert_type = AST_EXPR_LIST_SELECT;
        return infer_list_select(m, expr->value);
    }

    if (left_type.kind == TYPE_MAP) {
        expr->assert_type = AST_EXPR_MAP_SELECT;
        return infer_map_select(m, expr->value);
    }

    if (left_type.kind == TYPE_SET) {
        expr->assert_type = AST_EXPR_SET_SELECT;
        return infer_set_select(m, expr->value);
    }

    assertf(false, "type '%s' cannot use dot syntax", type_kind_string[left_type.kind]);
    exit(1);
}

/**
 * TODO builtin call handle, example set()
 * @param call
 * @return
 */
static typeuse_t infer_call(module_t *m, ast_call *call) {
    // 左值符号推导
    typeuse_t left_type = infer_expr(m, &call->left);
    assertf(left_type.kind == TYPE_FN, "cannot call non-fn");

    type_fn_t *type_fn = left_type.fn;

    // 实参类型推导与类型还原
    for (int i = 0; i < call->actual_params->length; ++i) {
        ast_expr *expr = ct_list_value(call->actual_params, i);
        infer_expr(m, expr);  // right 类型还原，其中也包括 spread param
    }

    // 参数对比，由于存在 spread 和 rest 运算，所以不能直接根据参数数量左 assert
    uint8_t count = max(type_fn->formal_types->length, call->actual_params->length);

    for (int i = 0; i < count; ++i) {
        // first param from actual
        typeuse_t actual = select_actual_param(call, i);

        // first param from formal
        typeuse_t formal = select_formal_param(type_fn, i);
        assertf(type_compare(formal, actual), "call param[%d] type error, expect '%s' type, actual '%s' type", i,
                type_kind_string[formal.kind], type_kind_string[actual.kind]);

        ast_expr *expr = ct_list_value(call->actual_params, i);
        set_expr_target(expr, formal);

        // 如果 i < actual_param_count,则 actual_param 需要配置 target type
//        bool is_spread = call->spread_param && i == call->actual_params->length - 1;
//        if (i < call->actual_params->length && !is_spread) {
//            ast_expr *expr = ct_list_value(call->actual_params, i);
//            set_expr_target(expr, formal);
//        }
    }

    return type_fn->return_type;
}


/**
 * (xx, errort) = catch foo()
 * @param catch
 * @return
 */
static typeuse_t infer_catch(module_t *m, ast_catch *catch) {
    typeuse_t t = type_base_new(TYPE_TUPLE);
    t.tuple = NEW(type_tuple_t);
    t.tuple->elements = ct_list_new(sizeof(typeuse_t));

    typeuse_t return_type = infer_call(m, catch->call);
    ct_list_push(t.tuple->elements, &return_type);

    typeuse_t errort = type_base_new(TYPE_IDENT);
    errort.ident = NEW(type_ident_t);
    errort.ident->literal = BUILTIN_ERRORT;
    errort = reduction_type(m, errort);

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
    typeuse_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL || type.kind == TYPE_SELF) {
        assertf(false, "variable declaration cannot use type '%s'", type_kind_string[type.kind]);
    }
}


/**
 * 判断该类型是否能够帮助 var 进行推导
 * @param t
 * @return
 */
static bool type_confirmed(typeuse_t t) {
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
        if (m->key_type.kind == TYPE_UNKNOWN) {
            return false;
        }
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            typeuse_t *element_type = ct_list_value(tuple->elements, i);
            if (element_type->kind == TYPE_UNKNOWN) {
                return false;
            }
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
static void infer_vardef(module_t *m, ast_vardef_stmt *stmt) {
    typeuse_t right_type = infer_expr(m, &stmt->right);

    // 开始类型推断, err 的类型大多数情况都是像这样推断出来的
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        assertf(type_confirmed(right_type), "type inference error, right type not confirmed");
        stmt->var_decl.type = right_type;
        return;
    }

    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);

    // 判断类型是否一致 compare
    assertf(type_compare(stmt->var_decl.type, right_type), "cannot assigned variables, because type inconsistency");
    set_expr_target(&stmt->right, stmt->var_decl.type);
}

/**
 * @param stmt
 */
static void infer_assign(module_t *m, ast_assign_stmt *stmt) {
    typeuse_t left_type = infer_expr(m, &stmt->left);
    typeuse_t right_type = infer_expr(m, &stmt->right);

    assertf(type_compare(left_type, right_type), "type inconsistency");
    set_expr_target(&stmt->right, stmt->left.type);
}

static void infer_if(module_t *m, ast_if_stmt *stmt) {
    typeuse_t condition_type = infer_expr(m, &stmt->condition);
    assertf(type_compare(type_base_new(TYPE_BOOL), condition_type),
            "if condition must bool type");
    set_expr_target(&stmt->condition, type_base_new(TYPE_BOOL));

    infer_block(m, stmt->consequent);
    infer_block(m, stmt->alternate);
}

static void infer_for_cond_stmt(module_t *m, ast_for_cond_stmt *stmt) {
    typeuse_t cond_type = infer_expr(m, &stmt->condition);
    assertf(cond_type.kind == TYPE_BOOL, "for stmt condition must bool");

    infer_block(m, stmt->body);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void infer_for_iterator(module_t *m, ast_for_iterator_stmt *stmt) {
    // 经过 infer_expr 的类型一定是已经被还原过的
    typeuse_t iterate_type = infer_expr(m, &stmt->iterate);

    assertf(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_LIST,
            "for in iterate type must be map/list, actual=%s", type_kind_string[iterate_type.kind]);

    // 类型推断 (value 可选)
    ast_var_decl key_decl = stmt->key;
    // 为 key_decl 添加 type
    if (iterate_type.kind == TYPE_MAP) {
        type_map_t *map_decl = iterate_type.map;
        key_decl.type = map_decl->key_type;
    } else {
        // list
        key_decl.type = type_base_new(TYPE_INT);
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
    typeuse_t cond_type = infer_expr(m, &stmt->cond);
    assertf(cond_type.kind == TYPE_BOOL, "for stmt condition must bool");
    infer_stmt(m, stmt->update);
    infer_block(m, stmt->body);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void infer_return(module_t *m, ast_return_stmt *stmt) {
    typeuse_t return_type = type_base_new(TYPE_VOID);
    if (stmt->expr != NULL) {
        return_type = infer_expr(m, stmt->expr);
    }

    typeuse_t expect_type = m->infer_current->return_type;
    assertf(type_compare(expect_type, return_type), "expect return '%s' type,actual return type '%s'",
            type_kind_string[return_type.kind],
            type_kind_string[expect_type.kind]);
}

static typeuse_t infer_literal(module_t *m, ast_literal *literal) {
    return type_base_new(literal->kind);
}

static typeuse_t infer_env_access(module_t *m, ast_env_access *expr) {
    ast_ident ident = {
            .literal = expr->unique_ident,
    };
    return infer_ident(m, &ident);
}

static void infer_throw(module_t *m, ast_throw_stmt *throw_stmt) {
    typeuse_t t = infer_expr(m, &throw_stmt->error);
    assertf(t.kind == TYPE_STRING, "only support throw string");
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static typeuse_t infer_tuple_destr(module_t *m, ast_tuple_destr *destr) {
    typeuse_t t = type_base_new(TYPE_TUPLE);
    type_tuple_t *tuple = NEW(type_tuple_t);
    tuple->elements = ct_list_new(sizeof(typeuse_t));
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(destr->elements, i);
        typeuse_t item_type = infer_expr(m, expr);
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
static void infer_var_tuple_destr(module_t *m, ast_tuple_destr *destr, typeuse_t t) {
    type_tuple_t *tuple_type = t.tuple;
    assertf(destr->elements->length == tuple_type->elements->length, "tuple destr length != tuple expr length");

    // 挨个对比
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(destr->elements, i);
        typeuse_t *actual_type = ct_list_value(tuple_type->elements, i);

        assertf(type_confirmed(*actual_type), "tuple expr index=%d type unknown");

        if (expr->assert_type == AST_VAR_DECL) {
            // 直接推到出具体类型
            ast_var_decl *var_decl = expr->value;
            var_decl->type = *actual_type;
        } else {
            assertf(expr->assert_type == AST_EXPR_TUPLE_DESTR, "tuple destr must var/tuple_destr");
            infer_var_tuple_destr(m, expr->value, *actual_type);
        }
    }
}

static void infer_var_tuple_def(module_t *m, ast_var_tuple_def_stmt *stmt) {
    typeuse_t expr_type = infer_expr(m, &stmt->right);

    infer_var_tuple_destr(m, stmt->tuple_destr, expr_type);
}

static typeuse_t infer_tuple_new(module_t *m, ast_tuple_new *tuple_new) {
    typeuse_t t = type_base_new(TYPE_TUPLE);
    type_tuple_t *tuple_type = NEW(type_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(typeuse_t));
    t.tuple = tuple_type;

    for (int i = 0; i < tuple_new->elements->length; ++i) {
        ast_expr *expr = ct_list_value(tuple_new->elements, i);
        typeuse_t expr_type = infer_expr(m, expr);
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
 * 表达式推断核心逻辑
 * @param expr
 * @return
 */
static typeuse_t infer_expr(module_t *m, ast_expr *expr) {
    typeuse_t type;
    switch (expr->assert_type) {
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
        case AST_EXPR_TUPLE_DESTR: {
            type = infer_tuple_destr(m, expr->value);
            break;
        }
        case AST_EXPR_LIST_NEW: {
            type = infer_list_new(m, expr->value);
            break;
        }
        case AST_EXPR_MAP_NEW: {
            type = infer_map_new(m, expr->value);
            break;
        }
        case AST_EXPR_SET_NEW: {
            type = infer_set_new(m, expr->value);
            break;
        }
        case AST_EXPR_TUPLE_NEW: {
            type = infer_tuple_new(m, expr->value);
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
            assertf(false, "unknown expr %d", expr->assert_type);
            exit(1);
        }
    }

    // 这里已经对表达式 type 做了调整
    expr->type = type;
    return expr->type;
}

static typeuse_t reduction_struct(module_t *m, typeuse_t t) {
    assertf(t.kind == TYPE_STRUCT, "type kind=%s unexpect", type_kind_string[t.kind]);

    type_struct_t *s = t.struct_;

    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        if (p->type.kind == TYPE_UNKNOWN) {
            p->type = reduction_type(m, p->type);
        }

        if (p->right) {
            // 推断右值表达式类型
            typeuse_t right_type = infer_expr(m, p->right);
            if (p->type.kind == TYPE_UNKNOWN) {
                assertf(type_confirmed(right_type), "struct property=%s type cannot confirmed", p->key);
                p->type = right_type;
            } else {
                assertf(type_compare(p->type, right_type), "struct property=%s, type inconsistency", p->key);
            }
        }

        assertf(type_confirmed(p->type), "struct property=%s type cannot confirmed", p->key);
        // 至此左值已经都是固定类型了, 如果存在 self 则 self 类型保持不变,self 不需要在这里处理
    }

    return t;
}

static typeuse_t reduction_complex_type(module_t *m, typeuse_t t) {
    if (t.kind == TYPE_LIST) {
        type_list_t *type_list = t.list;
        type_list->element_type = reduction_type(m, type_list->element_type);
        return t;
    }

    if (t.kind == TYPE_MAP) {
        t.map->key_type = reduction_type(m, t.map->key_type);
        t.map->value_type = reduction_type(m, t.map->key_type);
        return t;
    }

    if (t.kind == TYPE_SET) {
        t.set->key_type = reduction_type(m, t.set->key_type);
        return t;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            typeuse_t *use = ct_list_value(tuple->elements, i);
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
            typeuse_t *formal_type = ct_list_value(fn->formal_types, i);
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

static typeuse_t reduction_type_ident(module_t *m, typeuse_t t) {
    type_ident_t *ident = t.ident;
    symbol_t *symbol = symbol_table_get(ident->literal);


    assertf(symbol->type == SYMBOL_TYPEDEF, "'%s' is not a type", symbol->ident);
    ast_typedef_stmt *typedef_stmt = symbol->ast_value;

    assertf(m->reduction_typedef && typedef_stmt->type.status == REDUCTION_STATUS_DONE,
            "typedef stage exception, all typedef ident expr must done");

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
    return reduction_type(m, t);
}

static typeuse_t reduction_type(module_t *m, typeuse_t t) {
    assertf(t.kind == TYPE_SELF, "cannot use type self everywhere except struct fn decl");

    if (t.status == REDUCTION_STATUS_DONE) {
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_IDENT) {
        t = reduction_type_ident(m, t);
        goto STATUS_DONE;
    }

    // 只有 typedef ident 才有中间状态的说法
    if (is_basic_type(t)) {
        goto STATUS_DONE;
    }

    // infer complex type
    if (is_complex_type(t)) {
        t = reduction_complex_type(m, t);
        goto STATUS_DONE;
    }

    assertf(false, "cannot parser type %s", type_kind_string[t.kind]);
    STATUS_DONE:
    t.status = REDUCTION_STATUS_DONE;
    t.in_heap = type_default_in_heap(t);

    // 计算 reflect type
    ct_reflect_type(t);
    return t;
}

/**
 * 仅 infer 了 fn 对声明部分，不包含 body
 * @param m
 * @param fndef
 */
static typeuse_t infer_fndef_decl(module_t *m, ast_fndef_t *fndef) {
    // 对 fndef 进行类型还原
    type_fn_t *f = NEW(type_fn_t);
    f->formal_types = ct_list_new(sizeof(typeuse_t));

    f->return_type = reduction_type(m, fndef->return_type);

    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *var = ct_list_value(fndef->formals, i);

        // 首个参数，且 type 为 self 时不需要走 reduction_type, 直接定义为以还原
        if (i == 0 && var->type.kind == TYPE_SELF) {
            assertf(fndef->self_struct, "use 'self' in struct outside");
            var->type.status = REDUCTION_STATUS_DONE;
        } else {
            // 对 var 对 type 部分进行类型还原即可
            var->type = reduction_type(m, var->type);
        };

        ct_list_push(f->formal_types, &var->type);
    }

    f->rest_param = fndef->rest_param;

    typeuse_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_DONE;

    return result;
}

/**
 * 包含 body
 * @param m
 * @param fndef
 */
static void infer_fndef(module_t *m, ast_fndef_t *fndef) {
    infer_fndef_decl(m, fndef);

    // env 表达式类型还原
    for (int i = 0; i < fndef->parent_view_envs->length; ++i) {
        ast_expr *env_expr = ct_list_value(fndef->parent_view_envs, i);
        infer_expr(m, env_expr);
    }

    if (fndef->self_struct) {
        *fndef->self_struct = reduction_type(m, *fndef->self_struct);
    }

    m->infer_current = fndef;

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
