#include "checking.h"

#include <string.h>

#include "analyzer.h"
#include "src/debug/debug.h"
#include "src/error.h"

static bool union_type_contains(type_t union_type, type_t sub) {
    assert(union_type.kind == TYPE_UNION);
    assert(sub.kind != TYPE_UNION);

    if (union_type.union_->any) {
        return true;
    }

    for (int i = 0; i < union_type.union_->elements->length; ++i) {
        type_t *t = ct_list_value(union_type.union_->elements, i);
        if (type_compare(*t, sub, NULL)) {
            return true;
        }
    }

    return false;
}

bool type_union_compare(type_union_t *left, type_union_t *right) {
    // 因为 any 的作用域大于非 any 的作用域
    if (right->any && !left->any) {
        return false;
    }

    // 创建一个标记数组，用于标记left中的类型是否已经匹配
    // 遍历right中的类型，确保每个类型都存在于left中
    for (int i = 0; i < right->elements->length; ++i) {
        type_t *right_type = ct_list_value(right->elements, i);

        // 检查right_type是否存在于left中
        bool type_found = false;
        for (int j = 0; j < left->elements->length; ++j) {
            type_t *left_type = ct_list_value(left->elements, j);
            if (type_compare(*left_type, *right_type, NULL)) {
                type_found = true;
                break;
            }
        }
        // 如果right_type不存在于left中，则释放内存并返回false
        if (!type_found) {
            return false;
        }
    }

    return true;
}

/**
 * reduction 阶段就应该完成 cross left kind 的定位
 * @param dst
 * @param src
 * @return
 */
bool type_compare(type_t dst, type_t src, table_t *generics_param_table) {
    assertf(dst.status == REDUCTION_STATUS_DONE && src.status == REDUCTION_STATUS_DONE, "type not origin, left: '%s', right: '%s'",
            type_kind_str[dst.kind], type_kind_str[src.kind]);

    assertf(dst.kind != TYPE_UNKNOWN && src.kind != TYPE_UNKNOWN, "type unknown cannot checking");

    // all_t 可以匹配任何类型
    if (dst.kind == TYPE_ALL_T) {
        return true;
    }

    // fn_t 可以匹配所在的 fn 类型
    if (dst.kind == TYPE_FN_T && src.kind == TYPE_FN) {
        return true;
    }

    // nptr<t> 可以匹配 null 和 ptr<t>
    // nptr is ptr<...>|null so can access null and ptr
    if (dst.kind == TYPE_NPTR) {
        if (src.kind == TYPE_NULL) {
            return true;
        }

        if (src.kind == TYPE_PTR) {
            type_t dst_ptr = dst.pointer->value_type;
            type_t src_ptr = src.pointer->value_type;
            return type_compare(dst_ptr, src_ptr, NULL);
        }
    }

    // type param special
    if (dst.kind == TYPE_PARAM) {
        assert(src.status == REDUCTION_STATUS_DONE);
        char *param_ident = dst.param->ident;
        type_t *target_type = table_get(generics_param_table, param_ident);
        if (target_type) {
            dst = *target_type;
        } else {
            type_t *src_t = NEW(type_t);
            memmove(src_t, &src, sizeof(type_t));
            table_set(generics_param_table, param_ident, src_t);
        }
    }

    if (dst.kind != src.kind) {
        return false;
    }

    if (dst.kind == TYPE_UNION) {
        type_union_t *left_union_decl = dst.union_;
        type_union_t *right_union_decl = src.union_;

        if (left_union_decl->any) {
            return true;
        }

        return type_union_compare(left_union_decl, right_union_decl);
    }

    if (dst.kind == TYPE_MAP) {
        type_map_t *left_map_decl = dst.map;
        type_map_t *right_map_decl = src.map;

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type, NULL)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type, NULL)) {
            return false;
        }

        return true;
    }

    if (dst.kind == TYPE_SET) {
        type_set_t *left_decl = dst.set;
        type_set_t *right_decl = src.set;

        if (!type_compare(left_decl->element_type, right_decl->element_type, NULL)) {
            return false;
        }

        return true;
    }

    if (dst.kind == TYPE_VEC) {
        type_vec_t *left_list_decl = dst.vec;
        type_vec_t *right_list_decl = src.vec;
        return type_compare(left_list_decl->element_type, right_list_decl->element_type, NULL);
    }

    if (dst.kind == TYPE_ARR) {
        type_array_t *left_array_decl = dst.array;
        type_array_t *right_array_decl = src.array;
        if (left_array_decl->length != right_array_decl->length) {
            return false;
        }
        return type_compare(left_array_decl->element_type, right_array_decl->element_type, NULL);
    }

    if (dst.kind == TYPE_TUPLE) {
        type_tuple_t *left_tuple = dst.tuple;
        type_tuple_t *right_tuple = src.tuple;

        if (left_tuple->elements->length != right_tuple->elements->length) {
            return false;
        }
        for (int i = 0; i < left_tuple->elements->length; ++i) {
            type_t *left_item = ct_list_value(left_tuple->elements, i);
            type_t *right_item = ct_list_value(right_tuple->elements, i);
            if (!type_compare(*left_item, *right_item, NULL)) {
                return false;
            }
        }
        return true;
    }

    if (dst.kind == TYPE_FN) {
        type_fn_t *left_type_fn = dst.fn;
        type_fn_t *right_type_fn = src.fn;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type, NULL)) {
            return false;
        }

        // TODO rest 支持
        if (left_type_fn->param_types->length != right_type_fn->param_types->length) {
            return false;
        }

        for (int i = 0; i < left_type_fn->param_types->length; ++i) {
            type_t *left_formal_type = ct_list_value(left_type_fn->param_types, i);
            type_t *right_formal_type = ct_list_value(right_type_fn->param_types, i);
            if (!type_compare(*left_formal_type, *right_formal_type, NULL)) {
                return false;
            }
        }
        return true;
    }

    if (dst.kind == TYPE_STRUCT) {
        type_struct_t *left_struct = dst.struct_;
        type_struct_t *right_struct = src.struct_;
        if (left_struct->properties->length != right_struct->properties->length) {
            return false;
        }

        for (int i = 0; i < left_struct->properties->length; ++i) {
            struct_property_t *left_property = ct_list_value(left_struct->properties, i);
            struct_property_t *right_property = ct_list_value(right_struct->properties, i);

            // key 比较
            if (!str_equal(left_property->key, right_property->key)) {
                return false;
            }

            // type 比较
            if (!type_compare(left_property->type, right_property->type, NULL)) {
                return false;
            }
        }

        return true;
    }

    if (dst.kind == TYPE_PTR) {
        type_t left_pointer = dst.pointer->value_type;
        type_t right_pointer = src.pointer->value_type;
        return type_compare(left_pointer, right_pointer, NULL);
    }

    return true;
}

/**
 * foo(a) -> fn foo<T, U>
 * table_t 响应
 * @param m
 * @param tpl_fn
 * @param call
 * @param return_target_type
 * @return
 */
static table_t *infer_generics_args(module_t *m, ast_fndef_t *tpl_fn, ast_call_t *call, type_t return_target_type) {
    assert(tpl_fn->is_generics);
    // 如果 call 已经给定了类型实参，则直接使用类型实参

    // 进行参数解析，可以顺便使用 type_compare 了！
    table_t *generics_args_table = table_new();

    if (call->generics_args == NULL) {
        for (int i = 0; i <= call->args->length; i++) {
            bool is_spread = call->spread && (i == call->args->length - 1);

            ast_expr_t *arg = ct_list_value(call->args, i);
            type_t arg_type = checking_right_expr(m, arg, type_kind_new(TYPE_UNKNOWN));

            // 形参 type reduction
            type_t *formal_type = select_fn_param(tpl_fn->type.fn, i, is_spread);
            *formal_type = reduction_type(m, *formal_type);

            bool compare = type_compare(*formal_type, arg_type, generics_args_table);
            CHECKING_ASSERTF(compare, "cannot infer generics type")
        }

        if (return_target_type.kind != TYPE_UNKNOWN && return_target_type.kind != TYPE_VOID) {
            return_target_type = reduction_type(m, return_target_type);
            bool compare = type_compare(tpl_fn->return_type, return_target_type, generics_args_table);
            CHECKING_ASSERTF(compare, "cannot infer generics type")
        }

        // 判断泛型参数是否全部推断完成
        for (int i = 0; i <= tpl_fn->generics_params->length; i++) {
            ast_ident *param = ct_list_value(tpl_fn->generics_params, i);
            CHECKING_ASSERTF(table_exist(generics_args_table, param->literal), "cannot infer generics param '%s'", param->literal);
        }
    } else {
        // 按顺序生成 param
        for (int i = 0; i < call->generics_args->length; i++) {
            type_t *arg_type = ct_list_value(call->generics_args, i);
            *arg_type = reduction_type(m, *arg_type);
            ast_ident *param = ct_list_value(tpl_fn->generics_params, i);
            table_set(generics_args_table, param->literal, &arg_type);
        }
    }

    return generics_args_table;
}

static bool can_assign_to_union(type_t t) {
    if (t.kind == TYPE_UNION) {
        return false;
    }

    if (t.kind == TYPE_VOID) {
        return false;
    }

    return true;
}

static void literal_integer_casting(module_t *m, ast_expr_t *expr, type_t target_type) {
    CHECKING_ASSERTF(expr->assert_type == AST_EXPR_LITERAL, "integer casting only support literal");
    type_kind target_kind = target_type.kind;
    if (target_kind == TYPE_CPTR) {
        target_kind = TYPE_UINT;
    }

    ast_literal_t *literal = expr->value;

    CHECKING_ASSERTF(is_integer(literal->kind), "type inconsistency, expect %s, actual: %s", type_format(target_type),
                     type_kind_str[literal->kind]);

    int64_t i = atoll(literal->value);

    CHECKING_ASSERTF(integer_range_check(cross_kind_trans(target_kind), i), "integer out of range");

    literal->kind = target_kind;

    expr->type = target_type;
}

static void literal_float_casting(module_t *m, ast_expr_t *expr, type_t target_type) {
    CHECKING_ASSERTF(expr->assert_type == AST_EXPR_LITERAL, "float casting only support literal");

    ast_literal_t *literal = expr->value;

    CHECKING_ASSERTF(is_number(literal->kind), "type inconsistency");

    type_kind target_kind = cross_kind_trans(target_type.kind);
    double f = atof(literal->value);

    CHECKING_ASSERTF(float_range_check(cross_kind_trans(target_type.kind), f), "float out of range");

    literal->kind = target_type.kind;
    expr->type = target_type;
}

static uint32_t fn_param_types_hash(list_t *param_types) {
    char *str = itoa(TYPE_FN);
    for (int i = 0; i < param_types->length; ++i) {
        type_t *t = ct_list_value(param_types, i);
        rtype_t r = ct_reflect_type(*t);
        str = str_connect(str, itoa(r.hash));
    }

    return hash_string(str);
}

static char *generics_args_hash(ast_fndef_t *tpl_fn, table_t *arg_table) {
    assert(tpl_fn->is_generics);
    assert(tpl_fn->generics_params->length > 0);

    char *str = itoa(TYPE_FN);
    for (int i = 0; i < tpl_fn->generics_params->length; ++i) {
        ast_ident *ident = ct_list_value(tpl_fn->generics_params, i);
        type_t *t = table_get(arg_table, ident->literal);
        assert(t);
        rtype_t r = ct_reflect_type(*t);
        str = str_connect(str, itoa((int64_t) r.hash));
    }

    return itoa(hash_string(str));
}

/**
 * 当函数名称中存在多个函数时，将会通过简单的无类型转换进行函数匹配 (允许 single -> union type)
 * @param args
 * @param s
 * @return
 */
static ast_fndef_t *fn_match(module_t *m, ast_call_t *call, symbol_t *s) {
    list_t *args = call->args;
    assert(s->type == SYMBOL_FN);
    assert(s->fndefs);
    assert(s->fndefs->count > 0);

    // 非泛型/重载函数，这让 literal 的类型转换有了可能
    if (s->fndefs->count == 1) {
        return s->ast_value;
    }

    slice_t *fndefs = s->fndefs;

    // 基于参数数量进行一次过滤
    slice_t *temps = slice_new();
    for (int i = 0; i < fndefs->count; ++i) {
        ast_fndef_t *fndef = fndefs->take[i];
        assert(fndef->type.status == REDUCTION_STATUS_DONE);

        type_t type_fn = fndef->type;
        if (type_fn.fn->rest) {
            // 实参的数量大于等于 type_fn.params.length - 1
            if (args->length < (type_fn.fn->param_types->length - 1)) {
                continue;
            }
        } else {
            // 数量必须相等
            if (args->length != type_fn.fn->param_types->length) {
                continue;
            }
        }

        slice_push(temps, fndef);
    }

    fndefs = temps;

    // 开始匹配
    for (int i = 0; i < args->length; ++i) {
        ast_expr_t *expr = ct_list_value(args, i);
        type_t actual_type = checking_right_expr(m, expr, type_kind_new(TYPE_UNKNOWN));
        bool is_spread = call->spread && (i == args->length - 1);

        // ast_fndef
        temps = slice_new();
        for (int j = 0; j < fndefs->count; ++j) {
            ast_fndef_t *fndef = fndefs->take[j];
            type_t type_fn = fndef->type;

            // 会对 rest 进行自动解构
            type_t *formal_type = select_fn_param(type_fn.fn, i, is_spread);
            if (!formal_type) {
                continue;
            }

            CHECKING_ASSERTF(formal_type->kind != TYPE_GEN, "generic type not specialization");

            if (!type_compare(*formal_type, actual_type, NULL)) {
                continue;
            }
            slice_push(temps, fndef);
        }

        fndefs = temps;
    }

    table_t *fn_params_table = table_new();
    temps = slice_new();
    for (int i = 0; i < fndefs->count; ++i) {
        ast_fndef_t *fndef = fndefs->take[i];

        assert(fndef->generics_args_hash);
        // 根据类型 hash 进行去冲
        if (table_exist(fn_params_table, fndef->generics_args_hash)) {
            continue;
        }

        table_set(fn_params_table, fndef->generics_args_hash, fndef);
        slice_push(temps, fndef);
    }
    fndefs = temps;

    CHECKING_ASSERTF(fndefs->count > 0, "cannot match fn=%s", s->ident);
    CHECKING_ASSERTF(fndefs->count < 2, "fn=%s match more than one fndef", s->ident);

    return fndefs->take[0];
}

/**
 * 所有的 fndef 都将从这里进入，一旦 reduction 完成，就能基于 param 快速计算出唯一标识
 * @param m
 * @param fndef
 * @param t
 */
static bool rewrite_local_fndef(module_t *m, ast_fndef_t *fndef) {
    //已经注册并改写完毕，不需要重复改写
    if (fndef->generics_args_hash) {
        return true;
    }

    // local fn 直接适用 parent 的 hash 即可, 这么做也是为了兼容 generic 的情况
    // 否则 local fn 根据不会存在同名的情况, 另外 local fn 的调用作用域仅仅在当前函数内
    assert(fndef->global_parent);
    assert(strlen(fndef->global_parent->generics_args_hash) > 0);

    fndef->generics_args_hash = fndef->global_parent->generics_args_hash;

    // 如果 local fndef 引用了外部环境，是一个 closure, closure name 同样寻要基于 params hash 进行改写, 并将新的 symbol
    // 写入到全局符号表中
    if (fndef->closure_name) {
        fndef->closure_name = str_connect_by(fndef->closure_name, fndef->generics_args_hash, GEN_REWRITE_SEPARATOR);

        ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
        var_decl->ident = fndef->closure_name;
        var_decl->type = fndef->type;
        symbol_table_set(fndef->closure_name, SYMBOL_VAR, var_decl, true);
    }

    fndef->symbol_name = str_connect_by(fndef->symbol_name, fndef->generics_args_hash, GEN_REWRITE_SEPARATOR);

    symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, fndef->is_local);
    return true;
}

static char *rewrite_ident_use(module_t *m, char *old) {
    assert(m->current_fn);
    if (!m->current_fn->generics_args_hash) {
        return old;
    }

    assertf(!str_char(old, '@'), "repeat rewrite");

    return str_connect_by(old, m->current_fn->generics_args_hash, GEN_REWRITE_SEPARATOR);
}

static void rewrite_type_alias(module_t *m, ast_type_alias_stmt_t *stmt) {
    assert(m->current_fn);
    // 如果不存在 params_hash 表示当前 fndef 不存在基于泛型的重写，所以 alias 也不需要进行重写
    if (!m->current_fn->generics_args_hash) {
        return;
    }

    stmt->ident = str_connect_by(stmt->ident, m->current_fn->generics_args_hash, GEN_REWRITE_SEPARATOR);
    symbol_table_set(stmt->ident, SYMBOL_TYPE_ALIAS, stmt, true);
}

static void rewrite_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    assert(m->current_fn);
    if (!m->current_fn->generics_args_hash) {
        return;
    }

    var_decl->ident = str_connect_by(var_decl->ident, m->current_fn->generics_args_hash, GEN_REWRITE_SEPARATOR);

    // 进行符号表重新添加
    symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, true);
}

static void checking_body(module_t *m, slice_t *body) {
    for (int i = 0; i < body->count; ++i) {
#ifdef DEBUG_CHECKING
        debug_stmt("CHECKING", body->list[i]);
#endif

        // switch 结构导向优化
        checking_stmt(m, body->take[i]);
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
    if (t.kind == TYPE_VEC) {
        type_vec_t *l = t.vec;
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
static type_t checking_binary(module_t *m, ast_binary_expr_t *expr) {
    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    type_t left_type = checking_right_expr(m, &expr->left, type_kind_new(TYPE_UNKNOWN));
    type_t right_type = checking_right_expr(m, &expr->right, left_type);

    // 目前 binary 的两侧符号只支持 int 和 float
    if (is_number(left_type.kind)) {
        // 右值也必须是 number
        CHECKING_ASSERTF(is_number(right_type.kind),
                         "binary operator '%s' only support number operand, actual '%s %s %s'",
                         ast_expr_op_str[expr->operator],
                         type_format(left_type),
                         ast_expr_op_str[expr->operator],
                         type_format(right_type));
    }

    if (left_type.kind == TYPE_STRING) {
        // 右值必须是 string
        CHECKING_ASSERTF(right_type.kind == TYPE_STRING,
                         "binary operator '%s' only support string operand, actual '%s %s %s'",
                         ast_expr_op_str[expr->operator],
                         type_format(left_type),
                         ast_expr_op_str[expr->operator],
                         type_format(right_type));
    }

    // 位运算只能支持整形
    if (is_integer_operator(expr->operator)) {
        CHECKING_ASSERTF(is_integer(left_type.kind) && is_integer(right_type.kind),
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
        case AST_OP_OR_OR:
        case AST_OP_AND_AND:
        case AST_OP_LT:
        case AST_OP_LE:
        case AST_OP_GT:
        case AST_OP_GE:
        case AST_OP_EE:
        case AST_OP_NE: {
            return type_kind_new(TYPE_BOOL);
        }
        default: {
            CHECKING_ASSERTF(false, "unknown operator");
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
static type_t checking_as_expr(module_t *m, ast_expr_t *expr) {
    ast_as_expr_t *as_expr = expr->value;
    type_t target_type = reduction_type(m, as_expr->target_type);
    as_expr->target_type = target_type;

    // 此处进行了类型的约束
    type_t src_type = checking_expr(m, &as_expr->src, target_type);
    as_expr->src.type = src_type;

    // 如果此时 src type 和 dst type 一致，则直接跳过，不做任何报错
    if (type_compare(src_type, target_type, NULL)) {
        return target_type;
    }

    if (as_expr->src.type.kind == TYPE_UNION) {
        CHECKING_ASSERTF(target_type.kind != TYPE_UNION, "union to union type is not supported");

        // target_type 必须包含再 union 中
        CHECKING_ASSERTF(union_type_contains(as_expr->src.type, target_type), "type = %s not contains in union type",
                         type_format(target_type));
        return target_type;
    }

    if (as_expr->src.type.kind == TYPE_NPTR) {
        // 只能 as 为 ptr<t>
        CHECKING_ASSERTF(target_type.kind == TYPE_PTR || target_type.kind == TYPE_CPTR,
                         "nullable pointer only support as ptr<type> or cptr");
        if (target_type.kind == TYPE_PTR) {
            CHECKING_ASSERTF(type_compare(target_type.pointer->value_type, as_expr->src.type.pointer->value_type, NULL),
                             "pointer value not equal");
        }

        return target_type;
    }

    // 特殊类型转换 string -> list u8
    if (as_expr->src.type.kind == TYPE_STRING && is_list_u8(target_type)) {
        return target_type;
    }

    // list u8 -> string
    if (is_list_u8(as_expr->src.type) && target_type.kind == TYPE_STRING) {
        return target_type;
    }

    // 除了 float 以外所有类型都可以转换成 cptr
    if (!is_float(as_expr->src.type.kind) && target_type.kind == TYPE_CPTR) {
        return target_type;
    }

    CHECKING_ASSERTF(can_type_casting(target_type.kind), "type = %s not support casting", type_format(target_type));
    return target_type;
}

static type_t checking_new_expr(module_t *m, ast_new_expr_t *new_expr) {
    new_expr->type = reduction_type(m, new_expr->type);

    // 目前只有结构体可以使用 new
    CHECKING_ASSERTF(new_expr->type.kind == TYPE_STRUCT, "only struct type can use new");

    new_expr->properties = ct_list_new(sizeof(struct_property_t));

    table_t *exists = table_new();
    list_t *default_properties = new_expr->type.struct_->properties;
    for (int i = 0; i < default_properties->length; ++i) {
        struct_property_t *d = ct_list_value(default_properties, i);
        if (!d->right || table_exist(exists, d->key)) {
            continue;
        }

        ct_list_push(new_expr->properties, d);
    }

    return type_ptrof(new_expr->type);
}


/**
 * TODO 未完成
 * go 的返回值类型应该是 co.ctx 类型, 该类型在 pre_checking 中已经初步确认并返回
 * @param m
 * @param pVoid
 * @return
 */
static type_t checking_go(module_t *m, ast_go_t *go) {
    assert(false);
}

static type_t checking_catch(module_t *m, ast_catch_t *catch_expr) {
    type_t t = checking_right_expr(m, &catch_expr->try_expr, type_kind_new(TYPE_UNKNOWN));

    type_t errort = type_new(TYPE_ALIAS, NULL);
    errort.alias = NEW(type_alias_t);
    errort.alias->ident = ERRORT_TYPE_ALIAS;
    errort.origin_ident = ERRORT_TYPE_ALIAS;
    errort.impl_ident = ERRORT_TYPE_ALIAS;
    errort.status = REDUCTION_STATUS_UNDO;
    errort = reduction_type(m, errort);
    catch_expr->catch_err.type = errort;

    rewrite_var_decl(m, &catch_expr->catch_err);

    stack_push(m->current_fn->continue_target_types, &t);
    checking_body(m, catch_expr->catch_body);
    stack_pop(m->current_fn->continue_target_types);

    return t;
}

static type_t checking_is_expr(module_t *m, ast_is_expr_t *is_expr) {
    type_t t = checking_right_expr(m, &is_expr->src, type_kind_new(TYPE_UNKNOWN));
    is_expr->target_type = reduction_type(m, is_expr->target_type);
    CHECKING_ASSERTF(t.kind == TYPE_UNION || t.kind == TYPE_NPTR, "only any/union/nptr<t> type can use 'is' keyword");
    return type_kind_new(TYPE_BOOL);
}

static type_t checking_sizeof_expr(module_t *m, ast_sizeof_expr_t *sizeof_expr) {
    sizeof_expr->target_type = reduction_type(m, sizeof_expr->target_type);
    return type_kind_new(TYPE_INT);
}

/**
 * unary
 * @param expr
 * @return
 */
static type_t checking_unary(module_t *m, ast_unary_expr_t *expr) {
    if (expr->operator== AST_OP_NOT) {
        // bool 支持各种类型的 implicit type convert
        return checking_right_expr(m, &expr->operand, type_kind_new(TYPE_BOOL));
    }

    type_t type = checking_right_expr(m, &expr->operand, type_kind_new(TYPE_UNKNOWN));

    if ((expr->operator== AST_OP_NEG) && !is_number(type.kind)) {
        CHECKING_ASSERTF(false, "neg operand must applies to int or float type");
    }

    // &var
    if (expr->operator== AST_OP_LA) {
        CHECKING_ASSERTF(expr->operand.assert_type != AST_EXPR_LITERAL && expr->operand.assert_type != AST_CALL,
                         "cannot take the address of an literal or call");
        return type_ptrof(type);
    }

    // *var
    if (expr->operator== AST_OP_IA) {
        CHECKING_ASSERTF(type.kind == TYPE_PTR, "cannot dereference non-pointer type");
        return type.pointer->value_type;
    }

    return type;
}

/**
 * use ident
 * @param expr
 * @return
 */
static type_t checking_ident(module_t *m, ast_ident *ident) {
    char *unique_ident = ident->literal;
    symbol_t *symbol = symbol_table_get(unique_ident);
    CHECKING_ASSERTF(symbol, "ident %s not found", unique_ident);

    // 引用了 local symbol 时则尝试对 ident 进行改写, 能够改写的前提是当前 checking 的 fn 是泛型 fn
    if (symbol->is_local) {
        ident->literal = rewrite_ident_use(m, ident->literal);
        // 基于重写过后的符号重新定位 symbol
        symbol = symbol_table_get(ident->literal);
        CHECKING_ASSERTF(symbol, "ident %s not found", ident->literal);
    }

    if (symbol->type == SYMBOL_VAR) {
        ast_var_decl_t *var_decl = symbol->ast_value;
        var_decl->type = reduction_type(m, var_decl->type);// 类型还原
        return var_decl->type;
    }

    // 比如 print 和 println 都已经注册在了符号表中
    if (symbol->type == SYMBOL_FN) {
        ast_fndef_t *fndef = symbol->ast_value;
        return infer_fn_decl(m, fndef);
    }

    CHECKING_ASSERTF(false, "symbol type not expect");
    exit(1);
}

/**
 * 这里如果有问题直接就退出了
 * [a, b(), c[1], d.foo]
 * @param list_new
 * @return
 */
static type_t checking_vec_new(module_t *m, ast_expr_t *expr, type_t target_type) {
    ast_vec_new_t *ast = expr->value;

    if (target_type.kind == TYPE_ARR) {
        expr->assert_type = AST_EXPR_ARRAY_NEW;// 直接进行表达式类型的改写(list_new 和 array_new 同构,所以可以这么做)

        // 严格限定类型为 array
        type_t result = type_kind_new(TYPE_ARR);
        type_array_t *type_array = NEW(type_array_t);

        type_array->element_type = target_type.array->element_type;
        type_array->length = target_type.array->length;

        result.array = type_array;
        if (ast->elements->length == 0) {
            return result;
        }

        for (int i = 0; i < ast->elements->length; ++i) {
            ast_expr_t *item_expr = ct_list_value(ast->elements, i);
            checking_right_expr(m, item_expr, type_array->element_type);
        }

        return result;
    }

    type_t result = type_kind_new(TYPE_VEC);

    type_vec_t *type_vec = NEW(type_vec_t);

    // 初始化时类型未知
    type_vec->element_type = type_kind_new(TYPE_UNKNOWN);

    if (target_type.kind == TYPE_VEC) {
        // 如果 target 强制约定了类型则直接使用 target 的类型, 否则就自己推断
        // 考虑到 list_new 可能为空的情况，所以这里默认赋值一次
        type_vec->element_type = target_type.vec->element_type;
    }

    result.vec = type_vec;
    if (ast->elements->length == 0) {
        CHECKING_ASSERTF(type_confirmed(type_vec->element_type), "list element type not confirm");
        return result;
    }

    // target 类型不确定时，则按 list 首个元素类型进行推导
    if (type_vec->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(ast->elements, 0);
        type_vec->element_type = checking_right_expr(m, item_expr, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < ast->elements->length; ++i) {
        ast_expr_t *item_expr = ct_list_value(ast->elements, i);
        checking_right_expr(m, item_expr, type_vec->element_type);
    }

    return result;
}

static type_t checking_empty_curly_new(module_t *m, ast_expr_t *expr, type_t target_type) {
    // 必须要 target 引导，才能确定具体的类型
    CHECKING_ASSERTF(target_type.kind > 0, "map key/value type cannot confirm");

    CHECKING_ASSERTF(target_type.kind == TYPE_MAP || target_type.kind == TYPE_SET, "{} cannot ref type %s", type_format(target_type));
    if (target_type.kind == TYPE_MAP) {
        expr->assert_type = AST_EXPR_MAP_NEW;
    } else if (target_type.kind == TYPE_SET) {
        expr->assert_type = AST_EXPR_SET_NEW;
    }

    return target_type;
}

/**
 * {key: value, key(): value(), key[1]: value[1]}
 * @param map_new
 * @return
 */
static type_t checking_map_new(module_t *m, ast_map_new_t *map_new, type_t target_type) {
    type_t result = type_kind_new(TYPE_MAP);

    type_map_t *type_map = NEW(type_map_t);
    type_map->key_type = type_kind_new(TYPE_UNKNOWN);
    type_map->value_type = type_kind_new(TYPE_UNKNOWN);

    if (target_type.kind == TYPE_MAP) {
        // 考虑到 map 可能为空的情况，所以这里默认赋值一次, 如果为空就直接使用 target 的类型
        type_map->key_type = target_type.map->key_type;
        type_map->value_type = target_type.map->value_type;
    }
    result.map = type_map;
    if (map_new->elements->length == 0) {
        CHECKING_ASSERTF(type_confirmed(type_map->key_type), "map key type not confirm");
        CHECKING_ASSERTF(type_confirmed(type_map->value_type), "map value type not confirm");
        return result;
    }

    // 基于首个元素进行类型推断
    if (type_map->key_type.kind == TYPE_UNKNOWN) {
        ast_map_element_t *item = ct_list_value(map_new->elements, 0);
        type_map->key_type = checking_right_expr(m, &item->key, type_kind_new(TYPE_UNKNOWN));
        type_map->value_type = checking_right_expr(m, &item->value, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element_t *item = ct_list_value(map_new->elements, i);
        checking_right_expr(m, &item->key, type_map->key_type);
        checking_right_expr(m, &item->value, type_map->value_type);
    }

    return result;
}

/**
 * {1, 2, a.b,value[1]}
 * @param set_new
 * @return
 */
static type_t checking_set_new(module_t *m, ast_set_new_t *set_new, type_t target_type) {
    type_t result = type_kind_new(TYPE_SET);

    type_set_t *type_set = NEW(type_set_t);
    type_set->element_type = type_kind_new(TYPE_UNKNOWN);

    // 右值如果有推荐的类型，则基于推荐类型做 checking, 此时可能会触发类型转换
    if (target_type.kind == TYPE_SET) {
        type_set->element_type = target_type.set->element_type;
    }

    result.set = type_set;
    if (set_new->elements->length == 0) {
        CHECKING_ASSERTF(type_confirmed(type_set->element_type), "set element type not confirm");
        return result;
    }
    // target 类型不确定则按首个元素类型进行推导
    if (type_set->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(set_new->elements, 0);
        type_set->element_type = checking_right_expr(m, item_expr, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < set_new->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(set_new->elements, i);
        checking_right_expr(m, expr, type_set->element_type);
    }

    return result;
}

static type_t checking_vec_struct_new(module_t *m, ast_expr_t *expr) {
    // 返回的类型是 type_vec
    ast_struct_new_t *ast = expr->value;
    assert(ast->type.kind == TYPE_VEC);

    ast_vec_new_t *vec = NEW(ast_vec_new_t);

    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *p = ct_list_value(ast->properties, i);

        if (str_equal(p->key, VEC_LENGTH_KEY)) {
            vec->len = p->right;
            checking_right_expr(m, vec->len, type_kind_new(TYPE_INT));
        } else if (str_equal(p->key, VEC_CAPACITY_KEY)) {
            vec->cap = p->right;
            checking_right_expr(m, vec->cap, type_kind_new(TYPE_INT));
        } else {
            CHECKING_ASSERTF(false, "vec exclude property %s", p->key);
        }
    }

    expr->assert_type = AST_EXPR_VEC_NEW;
    expr->value = vec;

    return ast->type;
}

/**
 * person{
 *  age = 1
 * }
 *
 * struct{
 *   int age
 * } {
 *  age = 1
 * }
 *
 * type<arg1,arg2>{
 * }
 *
 * vec<u8>{ // rewrite
 * }
 *
 * @param ast
 * @return
 */
static type_t checking_struct_new(module_t *m, ast_expr_t *expr) {
    ast_struct_new_t *ast = expr->value;

    // 对类型进行了实例化 alias<int> -> struct{int}
    ast->type = reduction_type(m, ast->type);

    // if ast->type->kind is type vec, rewrite expr to ast_list_new_t
    // TODO 用不到了
    if (ast->type.kind == TYPE_VEC) {
        return checking_vec_struct_new(m, expr);
    }

    CHECKING_ASSERTF(ast->type.kind == TYPE_STRUCT, "ident '%s' not struct, cannot struct new", ast->type.origin_ident);

    type_struct_t *type_struct = ast->type.struct_;


    // exists 记录已经存在的参数用来辅助 struct 默认参数
    table_t *exists = table_new();
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *struct_property = ct_list_value(ast->properties, i);
        struct_property_t *expect_property = type_struct_property(type_struct, struct_property->key);

        CHECKING_ASSERTF(expect_property, "not found property '%s'", struct_property->key);

        table_set(exists, struct_property->key, struct_property);

        // struct_decl 已经是被还原过的类型了
        checking_right_expr(m, struct_property->right, expect_property->type);

        // type 冗余,方便计算 size (不能用来计算 offset)
        struct_property->type = expect_property->type;
    }

    // struct 默认参数处理, 默认参数的 checking 已经提前完成了，所以这里不需要二次重复进行 checking_right_expr
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
static type_t checking_access(module_t *m, ast_expr_t *expr) {
    ast_access_t *access = expr->value;
    type_t left_type = checking_left_expr(m, &access->left);

    // ast_access to ast_map_access
    if (left_type.kind == TYPE_MAP) {
        ast_map_access_t *map_access = NEW(ast_map_access_t);
        type_map_t *type_map = left_type.map;

        // 基于 map 编译 key
        checking_right_expr(m, &access->key, type_map->key_type);

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

    if (left_type.kind == TYPE_VEC || left_type.kind == TYPE_STRING) {
        type_t key_type = checking_right_expr(m, &access->key, type_kind_new(TYPE_INT));

        // ast_access -> ast_list_access
        ast_vec_access_t *list_access = NEW(ast_vec_access_t);

        type_t element_type = type_kind_new(TYPE_UINT8);
        if (left_type.kind == TYPE_VEC) {
            element_type = left_type.vec->element_type;
        }

        // 参数改写
        list_access->left = access->left;
        list_access->index = access->key;
        list_access->element_type = element_type;

        expr->assert_type = AST_EXPR_VEC_ACCESS;
        expr->value = list_access;

        return element_type;
    }

    if (left_type.kind == TYPE_ARR) {
        type_t key_type = checking_right_expr(m, &access->key, type_kind_new(TYPE_INT));

        ast_array_access_t *array_access = NEW(ast_array_access_t);

        type_array_t *type_array = left_type.array;
        array_access->left = access->left;
        array_access->index = access->key;
        array_access->element_type = type_array->element_type;
        expr->assert_type = AST_EXPR_ARRAY_ACCESS;
        expr->value = array_access;

        return type_array->element_type;
    }

    if (left_type.kind == TYPE_TUPLE) {
        type_t key_type = checking_right_expr(m, &access->key, type_kind_new(TYPE_INT));

        CHECKING_ASSERTF(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");

        type_tuple_t *type_tuple = left_type.tuple;

        ast_literal_t *index_literal = access->key.value;// 读取 index 的值
        CHECKING_ASSERTF(is_integer(index_literal->kind), "tuple index field must int immediate value");
        uint64_t index = atoi(index_literal->value);

        CHECKING_ASSERTF(index < type_tuple->elements->length, "tuple index field '%d' not in tuples", index);

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

    CHECKING_ASSERTF(false, "access only support must map/list/tuple, cannot '%s'", type_format(left_type));
    exit(1);
}

// expr rewrite call
static type_t checking_vec_select(module_t *m, ast_expr_t *expr) {
    ast_select_t *s = expr->value;// select left is reduction

    if (str_equal(s->key, "len")) {
        ast_call_t *call = NEW(ast_call_t);
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);// list operand
        call->left = *ast_ident_expr(expr->line, expr->column, RT_CALL_VEC_LENGTH);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_INT);

        expr->assert_type = AST_CALL;
        expr->value = call;

        return type_kind_new(TYPE_INT);
    }

    if (str_equal(s->key, "cap")) {
        ast_call_t *call = NEW(ast_call_t);
        call->args = ct_list_new(sizeof(ast_expr_t));

        ct_list_push(call->args, &s->left);// list operand

        call->left = *ast_ident_expr(expr->line, expr->column, RT_CALL_VEC_CAPACITY);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_INT);

        expr->assert_type = AST_CALL;
        expr->value = call;

        return type_kind_new(TYPE_INT);
    }

    CHECKING_ASSERTF(false, "vec no property '%s'", s->key);
    exit(EXIT_FAILURE);
}

static type_t checking_map_select(module_t *m, ast_expr_t *expr) {
    ast_select_t *s = expr->value;// select left is reduction

    if (str_equal(s->key, "len")) {
        ast_call_t *call = NEW(ast_call_t);
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);// list operand
        call->left = *ast_ident_expr(expr->line, expr->column, RT_CALL_MAP_LENGTH);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_INT);

        expr->assert_type = AST_CALL;
        expr->value = call;

        return type_kind_new(TYPE_INT);
    }

    CHECKING_ASSERTF(false, "map no property '%s'", s->key);
    exit(EXIT_FAILURE);
}

static type_t checking_string_select(module_t *m, ast_expr_t *expr) {
    ast_select_t *s = expr->value;// select left is reduction

    if (str_equal(s->key, "len")) {
        ast_call_t *call = NEW(ast_call_t);
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);// list operand
        call->left = *ast_ident_expr(expr->line, expr->column, RT_CALL_STRING_LENGTH);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_INT);

        expr->assert_type = AST_CALL;
        expr->value = call;

        return type_kind_new(TYPE_INT);
    }

    CHECKING_ASSERTF(false, "string no property '%s'", s->key);
    exit(EXIT_FAILURE);
}

/**
 * foo.bar
 * foo[1].bar
 * foo().bar
 * self.test
 * xxx.len
 * @param select
 * @return
 */
static type_t checking_select(module_t *m, ast_expr_t *expr) {
    ast_select_t *select = expr->value;

    checking_right_expr(m, &select->left, type_kind_new(TYPE_UNKNOWN));

    // if left is list -> xxx()
    if (select->left.type.kind == TYPE_VEC) {
        return checking_vec_select(m, expr);
    }

    if (select->left.type.kind == TYPE_MAP) {
        return checking_map_select(m, expr);
    }

    if (select->left.type.kind == TYPE_STRING) {
        return checking_string_select(m, expr);
    }

    // self.foo 这里是通过 self 访问属性，类似与 self.foo() 中的处理
    if (select->left.type.kind == TYPE_SELF) {
        ast_fndef_t *current = m->current_fn;
        CHECKING_ASSERTF(current->self_struct_ptr, "use 'self' in struct outside");

        // 初始化 current->self_struct_ptr 的时候就已经 reduction 完成了
        assert(current->self_struct_ptr->status == REDUCTION_STATUS_DONE && current->self_struct_ptr->kind == TYPE_PTR);

        // 当前 select 必定在 fn body 中，而处理 fn body 之前， fn.self_struct 在处理 body 之前已经进行了还原
        select->left.type = *current->self_struct_ptr;
    }

    // 不能直接改写 select->instance!
    type_t left_type = select->left.type;
    if (left_type.kind == TYPE_PTR) {
        type_t value_type = select->left.type.pointer->value_type;
        if (value_type.kind == TYPE_STRUCT) {
            left_type = value_type;
        }
    }

    // ast_access to ast_struct_access
    if (left_type.kind == TYPE_STRUCT) {
        // 经过上面对 checking_right_expr, 这里对 type 一定是 reduction 的
        type_struct_t *type_struct = left_type.struct_;
        struct_property_t *p = type_struct_property(type_struct, select->key);
        CHECKING_ASSERTF(p, "type %s no property '%s'", type_format(left_type), select->key);

        // 改写
        ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
        struct_select->instance = select->left;// 可能是 pointer<struct> 也可能是 struct
        struct_select->key = select->key;
        struct_select->property = p;
        expr->assert_type = AST_EXPR_STRUCT_SELECT;
        expr->value = struct_select;

        return p->type;
    }

    CHECKING_ASSERTF(false, "type '%s' no property .%s", type_format(select->left.type), select->key);
    exit(1);
}

/**
 * 参考 checking_vec_select_call 方法， 主要是要实现
 *
 * string.len() 返回 int
 * string.c_string() 返回 type cptr = uint(未还原)
 * @return
 */
static type_t checking_string_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;

    if (str_equal(s->key, BUILTIN_REF_KEY)) {
        CHECKING_ASSERTF(call->args->length == 0, "string c_string param failed");

        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_STRING_REF);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_CPTR);
        return type_kind_new(TYPE_CPTR);
    }

    CHECKING_ASSERTF(false, "string no fn property '%s'", s->key);
    exit(1);
}

/**
 * 对 call 参数验证后对 call 进行改写
 * @param m
 * @param call
 * @return
 */
static type_t checking_vec_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_vec_t *type_vec = s->left.type.vec;// 已经进行过类型推导了

    if (str_equal(s->key, VEC_PUSH_KEY)) {
        // push 对参数需要与 list element type 一致，否则抛出异常
        CHECKING_ASSERTF(call->args->length == 1, "list push param failed");
        ast_expr_t *expr = ct_list_value(call->args, 0);

        checking_right_expr(m, expr, type_vec->element_type);

        // 参数核验完成，对整个 call 进行改写, 改写成 list_push(l, value_ref)
        // 参数重写
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);// list operand

        // value operand, load address
        ct_list_push(call->args, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_VEC_PUSH);
        checking_left_expr(m, &call->left);// 对 ident 进行推导计算出其类型
        call->return_type = type_kind_new(TYPE_VOID);

        // list_push() 返回 void
        return type_kind_new(TYPE_VOID);
    }

    if (str_equal(s->key, VEC_SLICE_KEY)) {
        // n_list_t *list_slice(uint64_t rtype_hash, n_list_t *l, int64_t start, int64_t end);

        CHECKING_ASSERTF(call->args->length == 2, "list slice param exception");
        ast_expr_t *first_arg = ct_list_value(call->args, 0);
        ast_expr_t *second_arg = ct_list_value(call->args, 1);
        call->args = ct_list_new(sizeof(ast_expr_t));
        checking_right_expr(m, first_arg, type_kind_new(TYPE_INT));
        checking_right_expr(m, second_arg, type_kind_new(TYPE_INT));

        // 由于会产生新的 list, 所以需要把 rtype_hash 丢进去
        uint64_t rtype_hash = ct_find_rtype_hash(s->left.type);
        ct_list_push(call->args, ast_int_expr(first_arg->line, first_arg->column, rtype_hash));
        ct_list_push(call->args, &s->left);// list operand
        ct_list_push(call->args, first_arg);
        ct_list_push(call->args, second_arg);

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_VEC_SLICE);
        checking_left_expr(m, &call->left);// 对 ident 进行推导计算出其类型 type_fn
        call->return_type = s->left.type;
        return call->return_type;
    }

    if (str_equal(s->key, VEC_CONCAT_KEY)) {
        CHECKING_ASSERTF(call->args->length == 1, "list concat param exception");

        ast_expr_t *first_arg = ct_list_value(call->args, 0);
        call->args = ct_list_new(sizeof(ast_expr_t));

        checking_right_expr(m, first_arg, s->left.type);
        uint64_t rtype_hash = ct_find_rtype_hash(s->left.type);
        ct_list_push(call->args, ast_int_expr(first_arg->line, first_arg->column, rtype_hash));
        ct_list_push(call->args, &s->left);// list operand
        ct_list_push(call->args, first_arg);
        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_VEC_CONCAT);
        checking_left_expr(m, &call->left);// 对 ident 进行推导计算出其类型 type_fn
        call->return_type = s->left.type;
        return call->return_type;
    }

    if (str_equal(s->key, BUILTIN_REF_KEY)) {
        CHECKING_ASSERTF(call->args->length == 0, "list length not param");

        // 改写
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);// list operand

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_VEC_REF);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_CPTR);

        return type_kind_new(TYPE_CPTR);
    }

    CHECKING_ASSERTF(false, "vec no fn property '%s'", s->key);
    exit(EXIT_FAILURE);
}

/**
 * 需要做值改写,所以这里需要将
 * @param m
 * @param call
 * @return
 */
static type_t checking_map_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_map_t *map_type = s->left.type.map;// 已经进行过类型推导了
    if (str_equal(s->key, MAP_DELETE_KEY)) {
        CHECKING_ASSERTF(call->args->length == 1, "map.delete param failed");
        ast_expr_t *expr = ct_list_value(call->args, 0);
        checking_right_expr(m, expr, map_type->key_type);

        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);
        ct_list_push(call->args, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_MAP_DELETE);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_VOID);

        return type_kind_new(TYPE_VOID);
    }

    CHECKING_ASSERTF(false, "map no fn property '%s'", s->key);
    exit(0);
}

static type_t checking_set_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;
    type_set_t *set_type = s->left.type.set;// 已经进行过类型推导了
    if (str_equal(s->key, SET_DELETE_KEY)) {
        CHECKING_ASSERTF(call->args->length == 1, "set.delete param failed");
        ast_expr_t *expr = ct_list_value(call->args, 0);
        checking_right_expr(m, expr, set_type->element_type);

        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);
        ct_list_push(call->args, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_SET_DELETE);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_VOID);

        return type_kind_new(TYPE_VOID);
    }

    if (str_equal(s->key, SET_ADD_KEY)) {
        CHECKING_ASSERTF(call->args->length == 1, "set.add param failed");
        ast_expr_t *expr = ct_list_value(call->args, 0);
        checking_right_expr(m, expr, set_type->element_type);

        // s = left.key() 这里到 left 才是目标即可
        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);
        ct_list_push(call->args, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_SET_ADD);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_BOOL);

        return type_kind_new(TYPE_BOOL);
    }

    if (str_equal(s->key, SET_CONTAINS_KEY)) {
        CHECKING_ASSERTF(call->args->length == 1, "set.contains param failed");
        ast_expr_t *expr = ct_list_value(call->args, 0);
        checking_right_expr(m, expr, set_type->element_type);

        call->args = ct_list_new(sizeof(ast_expr_t));
        ct_list_push(call->args, &s->left);
        ct_list_push(call->args, ast_unary(expr, AST_OP_LA));

        call->left = *ast_ident_expr(call->left.line, call->left.column, RT_CALL_SET_CONTAINS);
        checking_left_expr(m, &call->left);
        call->return_type = type_kind_new(TYPE_BOOL);

        return type_kind_new(TYPE_BOOL);
    }

    CHECKING_ASSERTF(false, "set no fn property '%s'", s->key);
    exit(1);
}

static void checking_call_args(module_t *m, ast_call_t *call, type_fn_t *target_type_fn) {
    // 由于支持 fndef rest 语言，所以实参的数量大于等于形参的数量
    if (!target_type_fn->rest) {
        CHECKING_ASSERTF(call->args->length == target_type_fn->param_types->length, "number of call args not match declaration");
    }

    for (int i = 0; i < call->args->length; ++i) {
        bool is_spread = call->spread && (i == call->args->length - 1);

        // first param from formal
        type_t *formal_type = select_fn_param(target_type_fn, i, is_spread);

        if (i == 0 && formal_type->kind == TYPE_SELF) {
            // select first param 是 checking 自己伪造的，所以这里不需要在进行校验了
            continue;
        }

        ast_expr_t *arg = ct_list_value(call->args, i);

        checking_right_expr(m, arg, *formal_type);
    }
}

/**
 * if call first param type is self
 * struct.call(param1) -> struct.call(struct, param1) 即可
 * @param m
 * @param call
 * @return
 */
static type_t checking_struct_select_call(module_t *m, ast_call_t *call) {
    ast_select_t *s = call->left.value;

    type_struct_t *type_struct;
    if (is_struct_ptr(s->left.type)) {
        type_struct = s->left.type.pointer->value_type.struct_;
    } else {
        type_struct = s->left.type.struct_;// 已经进行过类型推导了
    }

    struct_property_t *p = type_struct_property(type_struct, s->key);
    CHECKING_ASSERTF(p, "type %s struct no property '%s'", type_struct->ident, s->key);

    // call left 改写成 struct select
    ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
    struct_select->instance = s->left;
    struct_select->key = s->key;
    struct_select->property = p;
    call->left.assert_type = AST_EXPR_STRUCT_SELECT;
    call->left.value = struct_select;
    call->left.type = p->type;

    // 进入前已经进行了 checking left, 所以这里的 type 都是 reduction 过的
    CHECKING_ASSERTF(p->type.kind == TYPE_FN, "cannot call non-fn");
    type_fn_t *type_fn = p->type.fn;
    call->return_type = type_fn->return_type;

    type_t *first = ct_list_value(type_fn->param_types, 0);

    // 按照普通 foo.bar(param) 的方式 checking 返回即可
    if (first->kind != TYPE_SELF) {
        checking_call_args(m, call, type_fn);
        return type_fn->return_type;
    }

    // formal 的首个参数是 self, 且 self 未经过推断
    list_t *args = call->args;
    call->args = ct_list_new(sizeof(ast_expr_t));

    // 在 person.set_age() 的示例中， person 虽然是 struct, 但是传参给 self 时，需要变成一个 ptr<struct> 传递
    ast_expr_t *self_replace = &struct_select->instance;

    if (self_replace->type.kind == TYPE_STRUCT) {
        self_replace = ast_unary(self_replace, AST_OP_LA);
        // self_replace->type = type_ptrof(self_replace->type);
    }

    ct_list_push(call->args, self_replace);
    for (int i = 0; i < args->length; ++i) {
        ct_list_push(call->args, ct_list_value(args, i));
    }
    checking_call_args(m, call, type_fn);
    return type_fn->return_type;
}

/**
 * self.foo()
 * 由于重载的存在，对参数的 compare 变成了基于 type 的 search 的过程
 * 如果找不到目标函数则说明 key 不存在或者没有找到匹配的函数类型
 * @param call
 * @return
 */
static type_t checking_call(module_t *m, ast_call_t *call, type_t target_type) {
    // [].push()
    // alias.push()
    // --------------------------------------------impl type handle----------------------------------------------------------
    if (call->left.assert_type == AST_EXPR_SELECT) {
        ast_select_t *select = call->left.value;

        // 这里已经对 left 进行了类型推导，所以后续不需要在进行类型推导了
        type_t select_left_type = checking_right_expr(m, &select->left, type_kind_new(TYPE_UNKNOWN));

        // [].push() 首先判断左值的类型，基于左值的类型，判断是否存在 impl_alias, 如果存在则直接进行改写(struct 也进行改写, 默认 impl 优先级高于 struct property)
        type_kind select_left_kind = select_left_type.kind;

        // 基于 left 进行拼装定位改写
        char *impl_ident = select_left_type.impl_ident;

        char *impl_symbol_name = str_connect_by(impl_ident, select->key, "_");

        // 查找对应的 fn
        symbol_t *s = symbol_table_get(impl_symbol_name);
        if (s != NULL) {
            // 对 call left 进行改写，TODO self 怎么传递，数据怎么传递？还是通过 self? 只有全局函数有 self 这个概念吧
            call->left = *ast_ident_expr(call->left.line, call->left.column, impl_symbol_name);

            // 重写 args
            list_t *args = ct_list_new(sizeof(ast_expr_t));
            ct_list_push(args, &select->left);
            for (int i = 0; i < call->args->length; ++i) {
                ct_list_push(args, ct_list_value(call->args, i));
            }
            call->args = args;
        }
    }

    // analyzer 阶段已经对 ident 进行了 resolve/module ident with
    // --------------------------------------------generics handle----------------------------------------------------------
    if (call->left.assert_type == AST_EXPR_IDENT) {
        do {
            ast_ident *ident = call->left.value;
            symbol_t *s = symbol_table_get(ident->literal);
            CHECKING_ASSERTF(s, "symbol '%s' not found", ident->literal);
            CHECKING_ASSERTF(s->type == SYMBOL_FN, "ident '%s' call non-fn", ident->literal);

            ast_fndef_t *tpl_fn = s->ast_value;
            if (!tpl_fn->is_generics) {
                break;
            }

            assert(!tpl_fn->is_local);

            // 首次初始化
            if (tpl_fn->generics_hash_table == NULL) {
                tpl_fn->generics_hash_table = table_new();
            }
            if (tpl_fn->generics_special_fns == NULL) {
                tpl_fn->generics_special_fns = slice_new();
            }

            // 泛型特化
            table_t *args_table = infer_generics_args(m, tpl_fn, call, target_type);
            char *args_hash = generics_args_hash(tpl_fn, args_table);
            ast_fndef_t *special_fn = table_get(tpl_fn->generics_hash_table, args_hash);
            if (special_fn == NULL) {
                // local_children 关系也已经重新构建
                m->analyzer_global = NULL;
                special_fn = ast_fndef_copy(m, tpl_fn);

                // 分配泛型参数，此时泛型函数中的 type_param 还没有进行特化处理
                table_set(tpl_fn->generics_hash_table, args_hash, special_fn);
                slice_push(tpl_fn->generics_special_fns, special_fn);

                special_fn->generics_args_hash = args_hash;
                special_fn->generics_args_table = args_table;

                // rewrite special fn
                special_fn->symbol_name = str_connect_by(special_fn->symbol_name, special_fn->generics_args_hash, GEN_REWRITE_SEPARATOR);

                // 注册到全局符号表(还未基于 args_hash checking + reduction)
                assert(!special_fn->is_local);
                symbol_table_set(special_fn->symbol_name, SYMBOL_FN, special_fn, special_fn->is_local);
                special_fn->type.status = REDUCTION_STATUS_UNDO;

                // 基于 args_table 进行 fn->type 泛型初始化
                stack_push(m->checking_type_args_stack, args_table);
                infer_fn_decl(m, special_fn);
                stack_pop(m->checking_type_args_stack);
            }

            assert(special_fn->type.status == REDUCTION_STATUS_DONE);

            // checking call args
            checking_call_args(m, call, special_fn->type.fn);
            call->return_type = special_fn->type.fn->return_type;
            return call->return_type;
        } while (0);
    }

    // 左值是一个表达式，进行表达式的类型 checking
    type_t left_type = checking_left_expr(m, &call->left);
    CHECKING_ASSERTF(left_type.kind == TYPE_FN, "cannot call non-fn");

    type_fn_t *type_fn = left_type.fn;
    checking_call_args(m, call, type_fn);

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
static type_t checking_try(module_t *m, ast_try_t *try) {
    type_t return_type = checking_right_expr(m, &try->expr, type_kind_new(TYPE_UNKNOWN));

    // 当表达式没有返回值时进行特殊处理
    type_t errort = type_new(TYPE_ALIAS, NULL);
    errort.alias = NEW(type_alias_t);
    errort.alias->ident = ERRORT_TYPE_ALIAS;
    errort.origin_ident = ERRORT_TYPE_ALIAS;
    errort.impl_ident = ERRORT_TYPE_ALIAS;
    errort.status = REDUCTION_STATUS_UNDO;
    errort = reduction_type(m, errort);
    if (return_type.kind == TYPE_VOID) {
        return errort;
    }

    type_t t = type_kind_new(TYPE_TUPLE);
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
void checking_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    var_decl->type = reduction_type(m, var_decl->type);
    type_t type = var_decl->type;
    if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL || type.kind == TYPE_SELF) {
        CHECKING_ASSERTF(false, "variable declaration cannot use type '%s'", type_format(type));
    }

    // global var def 也会走该函数，所以需要特殊处理一下，必须携带 m->checking_current 时才进行 var_decl rewrite
    if (m->current_fn) {
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
static void checking_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);
    rewrite_var_decl(m, &stmt->var_decl);

    type_t right_type = checking_right_expr(m, &stmt->right, stmt->var_decl.type);

    // 需要进行类型推断
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        CHECKING_ASSERTF(type_confirmed(right_type), "type checkingence error, right type not confirmed");

        stmt->var_decl.type = right_type;
        return;
    }
}

static void checking_global_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);
    type_t right_type = checking_right_expr(m, &stmt->right, stmt->var_decl.type);

    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        CHECKING_ASSERTF(type_confirmed(right_type), "type checkingence error, right type not confirmed");

        stmt->var_decl.type = right_type;
        return;
    }
}

/**
 * @param stmt
 */
static void checking_assign(module_t *m, ast_assign_stmt_t *stmt) {
    type_t left_type = checking_left_expr(m, &stmt->left);
    checking_right_expr(m, &stmt->right, left_type);
}

static void checking_if(module_t *m, ast_if_stmt_t *stmt) {
    checking_right_expr(m, &stmt->condition, type_kind_new(TYPE_BOOL));

    checking_body(m, stmt->consequent);
    checking_body(m, stmt->alternate);
}

static void checking_for_cond_stmt(module_t *m, ast_for_cond_stmt_t *stmt) {
    checking_right_expr(m, &stmt->condition, type_kind_new(TYPE_BOOL));

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->continue_target_types, &t);
    checking_body(m, stmt->body);
    stack_pop(m->current_fn->continue_target_types);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void checking_for_iterator(module_t *m, ast_for_iterator_stmt_t *stmt) {
    // 经过 checking_right_expr 的类型一定是已经被还原过的
    type_t iterate_type = checking_right_expr(m, &stmt->iterate, type_kind_new(TYPE_UNKNOWN));
    CHECKING_ASSERTF(iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_VEC || iterate_type.kind == TYPE_STRING,
                     "for in iterate type must be map/list/string, actual=%s", type_format(iterate_type));

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
    } else if (iterate_type.kind == TYPE_STRING) {
        if (!second) {
            first->type = type_kind_new(TYPE_UINT8);
        } else {
            first->type = type_kind_new(TYPE_INT);
        }
    } else {
        type_vec_t *type_vec = iterate_type.vec;

        // 判断是否存储 second, 如何
        if (!second) {
            // list
            first->type = type_vec->element_type;
        } else {
            first->type = type_kind_new(TYPE_INT);
        }
    }

    if (second) {
        if (iterate_type.kind == TYPE_MAP) {
            type_map_t *map_decl = iterate_type.map;
            second->type = map_decl->value_type;
        } else if (iterate_type.kind == TYPE_STRING) {
            second->type = type_kind_new(TYPE_UINT8);
        } else {
            type_vec_t *list_decl = iterate_type.vec;
            second->type = list_decl->element_type;
        }
    }

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->continue_target_types, &t);
    checking_body(m, stmt->body);
    stack_pop(m->current_fn->continue_target_types);
}

static void checking_for_tradition(module_t *m, ast_for_tradition_stmt_t *stmt) {
    checking_stmt(m, stmt->init);
    checking_right_expr(m, &stmt->cond, type_kind_new(TYPE_BOOL));
    checking_stmt(m, stmt->update);

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->continue_target_types, &t);
    checking_body(m, stmt->body);
    stack_pop(m->current_fn->continue_target_types);
}

/**
 * type nullable<t> = null|t
 * @param m
 * @param stmt
 */
static void checking_type_alias_stmt(module_t *m, ast_type_alias_stmt_t *stmt) {
    rewrite_type_alias(m, stmt);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void checking_return(module_t *m, ast_return_stmt_t *stmt) {
    type_t expect_type = m->current_fn->return_type;
    if (stmt->expr != NULL) {
        checking_right_expr(m, stmt->expr, expect_type);
    } else {
        CHECKING_ASSERTF(expect_type.kind == TYPE_VOID, "fn expect return type: %s", type_format(expect_type));
    }
}

static void checking_continue(module_t *m, ast_continue_t *stmt) {
    type_t *expect_type = stack_top(m->current_fn->continue_target_types);
    assert(expect_type);
    if (stmt->expr != NULL) {
        checking_right_expr(m, stmt->expr, *expect_type);
    } else {
        CHECKING_ASSERTF(expect_type->kind == TYPE_VOID, "continue cannot with expr");
    }
}

static type_t checking_literal(module_t *m, ast_literal_t *literal) {
    return reduction_type(m, type_kind_new(literal->kind));
}

static type_t checking_env_access(module_t *m, ast_env_access_t *expr) {
    ast_ident ident = {
            .literal = expr->unique_ident,
    };
    return checking_ident(m, &ident);
}

static void checking_throw(module_t *m, ast_throw_stmt_t *throw_stmt) {
    checking_right_expr(m, &throw_stmt->error, type_kind_new(TYPE_STRING));
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static type_t checking_tuple_destr(module_t *m, ast_tuple_destr_t *destr) {
    type_t t = type_kind_new(TYPE_TUPLE);
    t.tuple = NEW(type_tuple_t);
    t.tuple->elements = ct_list_new(sizeof(type_t));
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(destr->elements, i);
        type_t item_type = checking_left_expr(m, expr);
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
static void checking_var_tuple_destr(module_t *m, ast_tuple_destr_t *destr, type_t t) {
    type_tuple_t *tuple_type = t.tuple;
    CHECKING_ASSERTF(destr->elements->length == tuple_type->elements->length, "tuple destr length != tuple operand length");

    // 挨个对比
    for (int i = 0; i < destr->elements->length; ++i) {
        type_t *actual_type = ct_list_value(tuple_type->elements, i);
        CHECKING_ASSERTF(type_confirmed(*actual_type), "tuple operand index=%d type unknown");

        ast_expr_t *expr = ct_list_value(destr->elements, i);

        expr->type = *actual_type;// value is var_decl 不需要进行推导了
        if (expr->assert_type == AST_VAR_DECL) {
            // 直接推到出具体类型并回写到 operand 的 var_decl 中
            ast_var_decl_t *var_decl = expr->value;
            var_decl->type = *actual_type;
            rewrite_var_decl(m, var_decl);
        } else {
            CHECKING_ASSERTF(expr->assert_type == AST_EXPR_TUPLE_DESTR, "var tuple destr must var/tuple_destr");
            checking_var_tuple_destr(m, expr->value, *actual_type);
        }
    }
}

static void checking_var_tuple_def(module_t *m, ast_var_tuple_def_stmt_t *stmt) {
    // tuple 目前仅支持 var 形式的声明，所以此处和类型推导的形式一致
    type_t t = checking_right_expr(m, &stmt->right, type_kind_new(TYPE_UNKNOWN));

    CHECKING_ASSERTF(t.kind == TYPE_TUPLE, "cannot assign type '%s' to tuple", type_format(t));

    checking_var_tuple_destr(m, stmt->tuple_destr, t);
}

/**
 * @param m
 * @param tuple_new
 * @return
 */
static type_t checking_tuple_new(module_t *m, ast_tuple_new_t *tuple_new, type_t target_type) {
    type_t t = type_kind_new(TYPE_TUPLE);
    type_tuple_t *tuple_type = NEW(type_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(type_t));
    t.tuple = tuple_type;
    CHECKING_ASSERTF(tuple_new->elements->length > 0, "tuple elements empty");
    for (int i = 0; i < tuple_new->elements->length; ++i) {
        type_t element_target_type = type_kind_new(TYPE_UNKNOWN);
        if (target_type.kind == TYPE_TUPLE) {
            type_t *temp = ct_list_value(target_type.tuple->elements, i);
            element_target_type = *temp;
        }

        ast_expr_t *expr = ct_list_value(tuple_new->elements, i);
        type_t expr_type = checking_right_expr(m, expr, element_target_type);

        CHECKING_ASSERTF(type_confirmed(expr_type), "tuple element type type cannot confirmed");

        ct_list_push(tuple_type->elements, &expr_type);
    }

    return t;
}

static void checking_stmt(module_t *m, ast_stmt_t *stmt) {
    SET_LINE_COLUMN(stmt);

    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return checking_var_decl(m, stmt->value);
        }
        case AST_STMT_VARDEF: {
            return checking_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return checking_var_tuple_def(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return checking_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            break;
        }
        case AST_CALL: {
            checking_call(m, stmt->value, type_kind_new(TYPE_VOID));
            break;
        }
        case AST_CATCH: {
            checking_catch(m, stmt->value);
            break;
        }
        case AST_STMT_IF: {
            return checking_if(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return checking_for_cond_stmt(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return checking_for_iterator(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return checking_for_tradition(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return checking_throw(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return checking_return(m, stmt->value);
        }
        case AST_STMT_CONTINUE: {
            return checking_continue(m, stmt->value);
        }
        case AST_STMT_TYPE_ALIAS: {
            return checking_type_alias_stmt(m, stmt->value);
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
static type_t checking_left_expr(module_t *m, ast_expr_t *expr) {
    SET_LINE_COLUMN(expr);

    type_t type;
    switch (expr->assert_type) {
        case AST_EXPR_IDENT: {
            type = checking_ident(m, expr->value);
            break;
        }
        case AST_EXPR_TUPLE_DESTR: {
            type = checking_tuple_destr(m, expr->value);
            break;
        }
        case AST_EXPR_ACCESS: {
            type = checking_access(m, expr);
            break;
        }
        case AST_EXPR_SELECT: {
            type = checking_select(m, expr);
            break;
        }
        case AST_EXPR_ENV_ACCESS: {
            type = checking_env_access(m, expr->value);
            break;
        }
        case AST_CALL: {
            type = checking_call(m, expr->value, type_kind_new(TYPE_UNKNOWN));
            break;
        }
        default: {
            CHECKING_ASSERTF(false, "operand assert=%d cannot used in left", expr->assert_type);
            exit(0);
        }
    }

    expr->type = type;
    return type;
}

/**
 * 通过 target_type 对进行约束，但是不会强制进行比较
 * @return
 */
static type_t checking_expr(module_t *m, ast_expr_t *expr, type_t target_type) {
    SET_LINE_COLUMN(expr);
    if (expr->type.kind > 0) {
        return expr->type;
    }
    switch (expr->assert_type) {
        case AST_EXPR_AS: {
            return checking_as_expr(m, expr);
        }
        case AST_CATCH: {
            return checking_catch(m, expr->value);
        }
        case AST_EXPR_IS: {
            return checking_is_expr(m, expr->value);
        }
        case AST_EXPR_SIZEOF: {
            return checking_sizeof_expr(m, expr->value);
        }
        case AST_EXPR_NEW: {
            return checking_new_expr(m, expr->value);
        }
        case AST_EXPR_BINARY: {
            return checking_binary(m, expr->value);
        }
        case AST_EXPR_UNARY: {
            return checking_unary(m, expr->value);
        }
        case AST_EXPR_IDENT: {
            return checking_ident(m, expr->value);
        }
        case AST_EXPR_VEC_NEW: {
            return checking_vec_new(m, expr, target_type);
        }
        case AST_EXPR_EMPTY_CURLY_NEW: {
            return checking_empty_curly_new(m, expr, target_type);
        }
        case AST_EXPR_MAP_NEW: {
            return checking_map_new(m, expr->value, target_type);
        }
        case AST_EXPR_SET_NEW: {
            return checking_set_new(m, expr->value, target_type);
        }
        case AST_EXPR_TUPLE_NEW: {
            return checking_tuple_new(m, expr->value, target_type);
        }
        case AST_EXPR_STRUCT_NEW: {
            return checking_struct_new(m, expr);
        }
        case AST_EXPR_ACCESS: {
            // 这里需要做类型改写，确定具体的访问类型所以传递整个表达式
            return checking_access(m, expr);
        }
        case AST_EXPR_SELECT: {
            return checking_select(m, expr);
        }
        case AST_CALL: {
            return checking_call(m, expr->value, target_type);
        }
        case AST_EXPR_TRY: {
            return checking_try(m, expr->value);
        }
        case AST_GO: {
            return checking_go(m, expr->value);
        }
        case AST_FNDEF: {
            return infer_fn_decl(m, expr->value);
        }
        case AST_EXPR_LITERAL: {
            return checking_literal(m, expr->value);
        }
        case AST_EXPR_ENV_ACCESS: {
            return checking_env_access(m, expr->value);
        }
        default: {
            CHECKING_ASSERTF(false, "unknown operand %d", expr->assert_type);
            exit(EXIT_FAILURE);
        }
    }
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
static type_t checking_right_expr(module_t *m, ast_expr_t *expr, type_t target_type) {
    SET_LINE_COLUMN(expr);

    // 表达式已经 checking 过了就不要重复 checking 了
    if (expr->type.kind > 0) {
        return expr->type;
    }

    type_t type = checking_expr(m, expr, target_type);

    target_type = reduction_type(m, target_type);
    expr->type = reduction_type(m, type);
    expr->target_type = target_type;

    // TYPE_UNKNOWN 表示需要进行类型推断，此时什么都不做，交给调用者进行判断
    if (target_type.kind == TYPE_UNKNOWN) {
        return expr->type;
    }

    // - 对一些特殊类型进行预处理再进入 type_compare
    // 如果 target_type 是 number, 并且 expr->assert_type 是字面量值，则进行编译时的字面量值判断与类型转换
    // 避免出现如 i8 foo = 1 as i8 这样的重复的在编译时就可以识别出来的转换
    if ((is_integer(target_type.kind) || target_type.kind == TYPE_CPTR) && expr->assert_type == AST_EXPR_LITERAL) {
        literal_integer_casting(m, expr, target_type);
    }

    if (is_float(target_type.kind) && expr->assert_type == AST_EXPR_LITERAL) {
        literal_float_casting(m, expr, target_type);
    }

    // single type to union type (必须保留)
    if (target_type.kind == TYPE_UNION && can_assign_to_union(expr->type)) {
        CHECKING_ASSERTF(union_type_contains(target_type, expr->type), "union type not contains '%s'", type_format(expr->type));

        *expr = ast_type_as(*expr, target_type);
    }

    if (!type_compare(target_type, expr->type, NULL)) {
        CHECKING_ASSERTF(false, "type inconsistency, expect=%s, actual=%s", type_format(target_type), type_format(expr->type));
    }
    return expr->type;
}

static type_t reduction_struct(module_t *m, type_t t) {
    CHECKING_ASSERTF(t.kind == TYPE_STRUCT, "type kind=%s unexpect", type_format(t));

    type_struct_t *s = t.struct_;
    int align = 0;

    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        if (p->type.kind != TYPE_UNKNOWN) {
            p->type = reduction_type(m, p->type);
        }

        // 包含默认值
        if (p->right) {
            // 推断右值表达式类型(默认值推导)
            type_t right_type = checking_right_expr(m, p->right, p->type);

            if (p->type.kind == TYPE_UNKNOWN) {
                CHECKING_ASSERTF(type_confirmed(right_type), "struct property=%s type cannot confirmed", p->key);
                p->type = right_type;
            }
        }

        int item_align = type_alignof(p->type);

        if (item_align > align) {
            align = item_align;
        }

        CHECKING_ASSERTF(type_confirmed(p->type), "struct property=%s type cannot confirmed", p->key);
        // 至此左值已经都是固定类型了, 如果存在 self 则 self 类型保持不变,self 不需要在这里处理
    }
    t.struct_->align = align;

    // 将已经还原完成的 struct 赋值给 fn 使用
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);
        if (!p->right) {
            continue;
        }

        ast_expr_t *expr = p->right;
        if (expr->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = expr->value;
            fndef->self_struct_ptr = NEW(type_t);
            *fndef->self_struct_ptr = type_ptrof(type_new(TYPE_STRUCT, t.struct_));
        }
    }

    return t;
}

static type_t reduction_complex_type(module_t *m, type_t t) {
    if (t.kind == TYPE_PTR || t.kind == TYPE_NPTR) {
        type_pointer_t *type_pointer = t.pointer;
        type_pointer->value_type = reduction_type(m, type_pointer->value_type);
        return t;
    }

    if (t.kind == TYPE_VEC) {
        type_vec_t *type_vec = t.vec;
        type_vec->element_type = reduction_type(m, type_vec->element_type);
        return t;
    }

    if (t.kind == TYPE_ARR) {
        type_array_t *type_array = t.array;
        type_array->element_type = reduction_type(m, type_array->element_type);
        return t;
    }

    if (t.kind == TYPE_MAP) {
        t.map->key_type = reduction_type(m, t.map->key_type);
        t.map->value_type = reduction_type(m, t.map->value_type);

        CHECKING_ASSERTF(is_number(t.map->key_type.kind) || t.map->key_type.kind == TYPE_STRING || t.map->key_type.kind == TYPE_ALL_T,
                         "map key only support number/string");
        return t;
    }

    if (t.kind == TYPE_SET) {
        t.set->element_type = reduction_type(m, t.set->element_type);
        CHECKING_ASSERTF(is_number(t.map->key_type.kind) || t.map->key_type.kind == TYPE_STRING || t.map->key_type.kind == TYPE_ALL_T,
                         "set element only support number/string");
        return t;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        CHECKING_ASSERTF(tuple->elements->length > 0, "tuple element empty");
        int align = 0;
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *element = ct_list_value(tuple->elements, i);
            *element = reduction_type(m, *element);
            int element_align = type_alignof(*element);
            if (element_align > align) {
                align = element_align;
            }
        }
        tuple->align = align;
        return t;
    }

    // self 就 self 了,这里就不动 self 了,定义动时候也需要定义成 self!
    // 不能随便动名字
    if (t.kind == TYPE_FN) {
        type_fn_t *fn = t.fn;
        fn->return_type = reduction_type(m, fn->return_type);
        for (int i = 0; i < fn->param_types->length; ++i) {
            type_t *formal_type = ct_list_value(fn->param_types, i);
            *formal_type = reduction_type(m, *formal_type);
        }

        return t;
    }

    if (t.kind == TYPE_STRUCT) {
        return reduction_struct(m, t);
    }

    CHECKING_ASSERTF(false, "unknown type=%s", type_format(t));
    exit(1);
}

/**
 * 泛型特化
 * @param m
 * @param ident
 * @param t
 */
static type_t generic_specialization(module_t *m, char *ident) {
    CHECKING_ASSERTF(m->current_fn->generic_assign, "generic assign failed, use type gen must in global fn");
    type_t *assign = table_get(m->current_fn->generic_assign, ident);
    assert(assign);
    return reduction_type(m, *assign);
}

static type_t type_param_special(module_t *m, type_t t, table_t *arg_table) {
    assert(t.kind == TYPE_PARAM);

    // 实参可以没有 reduction
    type_t *arg_type = table_get(arg_table, t.param->ident);
    return reduction_type(m, *arg_type);
}

static type_t reduction_type_alias_vec(module_t *m, type_alias_t *alias) {
    CHECKING_ASSERTF(alias->args->length == 1, "vec must have one type arg");

    type_t *element_type = ct_list_value(alias->args, 0);
    *element_type = reduction_type(m, *element_type);

    type_t result = type_kind_new(TYPE_VEC);
    result.vec = NEW(type_vec_t);
    result.vec->element_type = *element_type;

    return result;
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

    // if (str_equal(alias->ident, VEC_TYPE_IDENT)) {
    //     return reduction_type_alias_vec(m, alias);
    // }

    symbol_t *symbol = symbol_table_get(alias->ident);
    CHECKING_ASSERTF(symbol, "type alias '%s' not found", alias->ident);
    CHECKING_ASSERTF(symbol->type == SYMBOL_TYPE_ALIAS, "'%s' is not a type", symbol->ident);

    // 此时的 symbol 可能是其他 module 中声明的符号
    ast_type_alias_stmt_t *type_alias_stmt = symbol->ast_value;

    // 判断是否包含 args, 如果包含 args 则需要每一次 reduction 都进行处理
    // 此时的 type alias 相当于重新定义了一个新的类型，所以不会存在 reduction 冲突问题
    if (type_alias_stmt->params) {
        assert(t.alias->args);// alias 有 param, 则实例化时必须携带 args
        assert(t.alias->args->length == type_alias_stmt->params->length);

        // 此时只是使用 module 作为一个 context 使用，实际上 type_alias_stmt->params 和 当前 module 并不是同一个文件中的
        // 实参注册
        table_t *args_table = table_new();
        for (int i = 0; i < t.alias->args->length; ++i) {
            type_t *arg = ct_list_value(t.alias->args, i);
            ast_ident *param = ct_list_value(type_alias_stmt->params, i);
            table_set(args_table, param->literal, arg);
        }

        // 对右值 copy 后再进行 reduction, 假如右侧值是一个 struct, 则其中的 struct fn 也需要 copy
        type_t alias_value_type = type_copy(m, type_alias_stmt->type);
        stack_push(m->checking_type_args_stack, args_table);

        // reduction 部分的 struct 的 right expr 如果是 struct，也只会进行到 checking_fn_decl 而不会处理 fn body 部分
        // 所以 fn body 部分还是包含 type_param, 如果此时将 type_param_table 置空，会导致后续 checking_fndef 时解析 param 异常
        // 更加正确的做法应该是将 type_param_table 赋值给相应的 ast_fndef
        alias_value_type = reduction_type(m, alias_value_type);

        stack_pop(m->checking_type_args_stack);

        return alias_value_type;
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

    type_alias_stmt->type.status = REDUCTION_STATUS_DOING;// 打上正在进行的标记,避免进入死循环
    type_alias_stmt->type = reduction_type(m, type_alias_stmt->type);

    return type_alias_stmt->type;
}

static type_t reduction_union_type(module_t *m, type_t t) {
    type_union_t *type_union = t.union_;
    if (type_union->any) {
        return t;
    }

    for (int i = 0; i < t.union_->elements->length; ++i) {
        type_t *temp = ct_list_value(t.union_->elements, i);
        *temp = reduction_type(m, *temp);
    }

    return t;
}


static type_t reduction_type(module_t *m, type_t t) {
    assert(t.kind > 0);

    char *origin_ident = t.origin_ident;
    char *impl_ident = t.impl_ident;

    if (t.kind == TYPE_UNKNOWN) {
        return t;
    }

    if (t.kind == TYPE_SELF) {
        t.status = REDUCTION_STATUS_DONE;
        t.in_heap = true;
        return t;
    }

    // 跳过已经完成 reduction 的 type
    if (t.status == REDUCTION_STATUS_DONE) {
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_ALIAS) {
        t = reduction_type_alias(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_PARAM) {
        assert(m->checking_type_args_stack);
        table_t *arg_table = stack_top(m->checking_type_args_stack);
        assert(arg_table);
        t = type_param_special(m, t, arg_table);
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

    // checking complex type
    if (is_reduction_type(t)) {
        t = reduction_complex_type(m, t);
        goto STATUS_DONE;
    }

    CHECKING_ASSERTF(false, "cannot parser type %s", type_format(t));
STATUS_DONE:
    t.status = REDUCTION_STATUS_DONE;
    t.in_heap = kind_in_heap(t.kind);
    t.origin_ident = origin_ident;// reduction dump error ident
    t.impl_ident = impl_ident;    // type impl
    if (t.kind == TYPE_INT || t.kind == TYPE_UINT || t.kind == TYPE_FLOAT) {
        t.kind = cross_kind_trans(t.kind);
    }

    // 计算 reflect type
    ct_reflect_type(t);
    return t;
}

/**
 * 对参数和返回值进行了类型推导，其中 self 对应的 struct 还没有 reduction 还没有处理完成
 * 所以无法对 self 进行 struct 还原。
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
    f->tpl = fndef->is_tpl;
    f->param_types = ct_list_new(sizeof(type_t));
    f->return_type = reduction_type(m, fndef->return_type);
    fndef->return_type = f->return_type;

    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);
        // TODO param 不需要进行 rewrite 吗？
        param->type = reduction_type(m, param->type);
        ct_list_push(f->param_types, &param->type);
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
    assert(fndef->type.kind == TYPE_FN && fndef->type.status == REDUCTION_STATUS_DONE);
    m->current_fn = fndef;
    m->current_line = fndef->line;
    m->current_column = fndef->column;

    type_t t = fndef->type;

    // rewrite_formals ident
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->params, i);
        rewrite_var_decl(m, var_decl);
    }

    if (fndef->closure_name) {
        symbol_t *symbol = symbol_table_get(fndef->closure_name);
        CHECKING_ASSERTF(symbol, "fn var ident %s not found", fndef->closure_name);
        CHECKING_ASSERTF(symbol->type == SYMBOL_VAR, "symbol type not expected");

        ast_var_decl_t *var_decl = symbol->ast_value;
        var_decl->type = t;
    }

    // env 表达式类型还原
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr_t *env_expr = ct_list_value(fndef->capture_exprs, i);
        checking_left_expr(m, env_expr);
    }

    // body checking
    checking_body(m, fndef->body);
}


/**
 * 对左右的 fn param 进行初步还原，这样 call gen fn 时才能正确的匹配类型
 * @param m
 */
void pre_checking(module_t *m) {
    // - 全局变量中也包含类型信息需要进行还原处理与类型推导
    for (int j = 0; j < m->global_vardef->count; ++j) {
        ast_vardef_stmt_t *vardef = m->global_vardef->take[j];

        // 内部已经完成了对类型的还原，并修改了 smt->vardecl 的值
        // 这会直接影响到全局符号表中的 vardecl
        checking_global_vardef(m, vardef);
    }

    // - 遍历所有 fndef 进行处理, 包含 global 和 local fn
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];

        assert(!fndef->is_local);

        if (fndef->is_generics) {
            continue;
        }

        // fndef 可能是，依旧不影响进行 reduction, type_param 跳过即可
        infer_fn_decl(m, fndef);
    }
}

void post_checking(module_t *m) {
    // 平铺处理，以及泛型 checking 处理
    slice_t *fndefs = slice_new();
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *tpl_fn = m->ast_fndefs->take[i];

        if (!tpl_fn->is_generics) {
            slice_push(fndefs, tpl_fn);
            for (int j = 0; j < tpl_fn->local_children->count; ++j) {
                slice_push(fndefs, tpl_fn->local_children->take[j]);
            }

            continue;
        }


        if (tpl_fn->generics_special_fns == NULL) {
            continue;
        }

        for (int j = 0; j < tpl_fn->generics_special_fns->count; ++j) {
            // 泛型特化 checking
            ast_fndef_t *special_fn = tpl_fn->generics_special_fns->take[j];
            assert(special_fn->generics_args_hash);
            assert(special_fn->generics_args_table);

            // rewrite_fndef
            for (int k = 0; k < special_fn->local_children->count; ++k) {
                // special copy 的时候，child 也顺便 copy 完成了
                ast_fndef_t *child = tpl_fn->local_children->take[k];
                // rewrite 主要是继承 generics_args_hash 给到 child, 并重写了 child 的 symbol name 并注册到符号表
                rewrite_local_fndef(m, child);
            }

            // 将 arg_table 赋值到 module 进行 infer
            stack_push(m->checking_type_args_stack, special_fn->generics_args_table);
            infer_fndef(m, special_fn);

            for (int k = 0; k < special_fn->local_children->count; ++k) {
                ast_fndef_t *child = special_fn->local_children->take[k];

                infer_fndef(m, child);
            }

            stack_pop(m->checking_type_args_stack);
        }
    }

    m->ast_fndefs = fndefs;
}

void checking(module_t *m) {
    m->current_fn = NULL;
    m->current_line = 0;
    m->current_column = 0;
    m->checking_type_args_stack = stack_new();

    // - 遍历所有 fndef 进行处理, 包含 global 和 local fn
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fn = m->ast_fndefs->take[i];
        if (fn->is_generics) {
            continue;
        }

        assert(fn->type.kind == TYPE_FN);

        infer_fndef(m, fn);

        for (int j = 0; j < fn->local_children->count; ++j) {
            infer_fndef(m, fn->local_children->take[j]);
        }
    }

    post_checking(m);
}
