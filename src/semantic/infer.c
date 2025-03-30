#include "infer.h"

#include <string.h>

#include "analyzer.h"
#include "src/debug/debug.h"
#include "src/error.h"

static void
check_typedef_impl(module_t *m, type_t *impl_interface, char *typedef_ident, ast_typedef_stmt_t *typedef_stmt);

static type_fn_t *infer_call_left(module_t *m, ast_call_t *call, type_t target_type);

static list_t *infer_struct_properties(module_t *m, type_struct_t *type_struct, list_t *properties);

static type_t infer_call(module_t *m, ast_call_t *call, type_t target_type, bool check_errable);

static void infer_call_args(module_t *m, ast_call_t *call, type_fn_t *target_type_fn);

static void infer_fndef(module_t *m, ast_fndef_t *fn);

static type_t infer_impl_fn_decl(module_t *m, ast_fndef_t *fndef);

static bool union_type_contains(type_union_t *union_type, type_t sub);

static void infer_break(module_t *m, ast_break_t *stmt);

bool generics_constraints_compare(ast_generics_constraints *left, ast_generics_constraints *right) {
    if (left->any) {
        return true;
    }

    if (right->any && !left->any) {
        return false;
    }

    if (right->and &&!left->and) {
        return false;
    }

    if (right->or &&!left->or) {
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

static void generics_constraints_check(module_t *m, ast_generics_constraints *constraints, type_t *src) {
    // 对 constraints 进行类型还原
    for (int j = 0; j < constraints->elements->length; ++j) {
        type_t *constraint = ct_list_value(constraints->elements, j);
        *constraint = reduction_type(m, *constraint);

        if (constraints->elements->length == 1) {
            if (constraint->kind == TYPE_INTERFACE) {
                constraints->and = true;
            } else if (constraint->kind != TYPE_UNION) {// or 表示单个类型约束？那 union 呢？怎么 check, 怎么约束？
                constraints->or = true;
            }
        }

        if (constraints->and) {
            // must interface
            INFER_ASSERTF(constraint->kind == TYPE_INTERFACE, "only type 'interface' can be combination with '&'");
        } else if (constraints->or) {
            // cannot interface and union
            INFER_ASSERTF(constraint->kind != TYPE_UNION, "cannot use type 'union' combination")
            INFER_ASSERTF(constraint->kind != TYPE_INTERFACE, "cannot use type 'interface' combination")
        }
    }

    if (constraints->any) {
        return;
    }

    // check src
    if (constraints->or) {
        for (int i = 0; i < constraints->elements->length; ++i) {
            type_t *t = ct_list_value(constraints->elements, i);
            if (type_compare(*t, *src, NULL)) {
                return;// 验证成功
            }
        }

        // type alias '%s' param constraint not match
        INFER_ASSERTF(false, "type '%s' does not match any of the constraints", type_format(*src));
        return;
    }

    // check interface
    if (constraints->and) {
        for (int i = 0; i < constraints->elements->length; ++i) {
            type_t *interface_type = ct_list_value(constraints->elements, i);
            assert(interface_type->ident_kind == TYPE_IDENT_INTERFACE);
            assert(interface_type->kind == TYPE_INTERFACE);

            type_t temp_target_type = *src;
            if (src->kind == TYPE_PTR || src->kind == TYPE_RAWPTR) {
                temp_target_type = src->ptr->value_type;
            }

            assert(temp_target_type.ident);

            symbol_t *s = symbol_table_get(temp_target_type.ident);
            if (!s) {// maybe builtin type
                if (interface_type->interface->elements->length > 0) {
                    INFER_ASSERTF(false, "type '%s' not impl '%s' interface", temp_target_type.ident,
                                  interface_type->ident);
                }

                // empty interface
                continue;
            }

            assert(s && s->type == SYMBOL_TYPE);
            ast_typedef_stmt_t *typedef_stmt = s->ast_value;
            bool found = false;
            if (typedef_stmt->impl_interfaces) {
                for (int j = 0; j < typedef_stmt->impl_interfaces->length; ++j) {
                    type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, j);
                    assert(impl_interface->ident_kind == TYPE_IDENT_INTERFACE);
                    if (str_equal(impl_interface->ident, interface_type->ident)) {
                        found = true;
                    }
                }
            }
            // 禁止鸭子类型
            INFER_ASSERTF(found, "type '%s' not impl '%s' interface", temp_target_type.ident, interface_type->ident);
            check_typedef_impl(m, interface_type, temp_target_type.ident, typedef_stmt);
        }

        return;
    }

    // union check
    assert(constraints->elements->length == 1);
    type_t *union_constraint = ct_list_value(constraints->elements, 0);
    assert(union_constraint->kind == TYPE_UNION);

    // type alias '%s' param constraint not match
    INFER_ASSERTF(union_type_contains(union_constraint->union_, *src),
                  "type '%s' does not match any of the constraints", type_format(*src));
}

static type_t *select_fn_param(type_fn_t *type_fn, uint8_t index, bool is_spread) {
    assert(type_fn);
    if (type_fn->is_rest && index >= type_fn->param_types->length - 1) {
        // is_rest handle
        type_t *last_param_type = ct_list_value(type_fn->param_types, type_fn->param_types->length - 1);

        // is_rest 最后一个参数的 type 不是 list 可以直接报错了, 而不是返回 NULL
        assert(last_param_type->kind == TYPE_VEC);

        // call(arg1, arg2, ...[]) -> fn call(int arg1, int arg2, ...[int] arg3)
        if (is_spread) {
            return last_param_type;
        }

        return &last_param_type->vec->element_type;
    }

    if (index >= type_fn->param_types->length) {
        return NULL;
    }

    return ct_list_value(type_fn->param_types, index);
}

static type_t *select_generics_fn_param(ast_fndef_t *temp_fn, uint8_t index, bool is_spread) {
    assert(temp_fn->is_generics);
    if (temp_fn->rest_param && index >= temp_fn->params->length - 1) {
        ast_var_decl_t *last_param = ct_list_value(temp_fn->params, temp_fn->params->length - 1);
        //        type_t *last_param_type = last_param->type;

        // is_rest 最后一个参数的 type 不是 list 可以直接报错了, 而不是返回 NULL
        assert(last_param->type.kind == TYPE_VEC);

        // call(arg1, arg2, ...[]) -> fn call(int arg1, int arg2, ...[int] arg3)
        if (is_spread) {
            return &last_param->type;
        }

        return &last_param->type.vec->element_type;
    }

    if (index >= temp_fn->params->length) {
        return NULL;
    }

    ast_var_decl_t *param = ct_list_value(temp_fn->params, index);

    return &param->type;
}


static bool union_type_contains(type_union_t *union_type, type_t sub) {
    assert(sub.kind != TYPE_UNION);

    if (union_type->any) {
        return true;
    }

    for (int i = 0; i < union_type->elements->length; ++i) {
        type_t *t = ct_list_value(union_type->elements, i);
        if (type_compare(*t, sub, NULL)) {
            return true;
        }
    }

    return false;
}

bool type_union_compare(type_union_t *left, type_union_t *right) {
    // any 可以匹配任何类型，包括两个 any 之间相互赋值
    if (left->any) {
        return true;
    }

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
 * reduction 阶段就应该完成 cross left kind 的定位.
 * @param dst
 * @param src
 * @return
 */
bool type_compare(type_t dst, type_t src, table_t *generics_param_table) {
    if (!ident_is_param(&dst)) {
        assertf(dst.status == REDUCTION_STATUS_DONE, "type '%s' not reduction", type_format(dst));
    }
    if (!ident_is_param(&src)) {
        assertf(src.status == REDUCTION_STATUS_DONE, "type '%s' not reduction", type_format(src));
    }

    assertf(dst.kind != TYPE_UNKNOWN && src.kind != TYPE_UNKNOWN, "type unknown cannot infer");

    // all_t 可以匹配任何类型
    if (dst.ident_kind == TYPE_IDENT_BUILTIN && str_equal(dst.ident, ALL_T_IDENT)) {
        return true;
    }

    // fn_t 可以匹配任意 fn 类型
    if (dst.ident_kind == TYPE_IDENT_BUILTIN && str_equal(dst.ident, FN_T_IDENT) && src.kind == TYPE_FN) {
        return true;
    }

    // integer_t 可以匹配任意 int 类型
    if (dst.ident_kind == TYPE_IDENT_BUILTIN && str_equal(dst.ident, INTEGER_T_IDENT) && is_integer(src.kind)) {
        return true;
    }

    if (dst.ident_kind == TYPE_IDENT_BUILTIN && str_equal(dst.ident, FLOATER_T_IDENT) && is_float(src.kind)) {
        return true;
    }

    // rawptr<t> 可以赋值为 null 以及 ptr<t> 两种类型的值
    // 但是不能通过 as 转换为 ptr<T> 和 null
    if (dst.kind == TYPE_RAWPTR) {
        if (src.kind == TYPE_NULL) {
            return true;
        }

        if (src.kind == TYPE_PTR) {
            type_t dst_ptr = dst.ptr->value_type;
            type_t src_ptr = src.ptr->value_type;
            return type_compare(dst_ptr, src_ptr, generics_param_table);
        }
    }

    // type param special, 基于 src 计算 dst param, reduction 完成后 dst.kind 会被修改
    if (ident_is_param(&dst)) {
        assert(src.status == REDUCTION_STATUS_DONE);
        assert(generics_param_table);

        char *param_ident = dst.ident;
        type_t *target_type = table_get(generics_param_table, param_ident);
        if (target_type) {
            dst = *target_type;
        } else {
            target_type = NEW(type_t);
            memmove(target_type, &src, sizeof(type_t));
            table_set(generics_param_table, param_ident, target_type);
            dst = *target_type;
        }

        return true;
    }

    if (dst.kind != src.kind) {
        return false;
    }

    // 即使 reduction 后，也需要通过 ident kind 进行判断, 其中 union_type 不受 type def 限制, TODO 无法区分这是 type_compare 还是 generics param handle!
    if (dst.ident_kind == TYPE_IDENT_DEF && dst.kind != TYPE_UNION) {
        assert(dst.ident != NULL);
        if (src.ident == NULL) {
            return false;
        }
        if (!str_equal(dst.ident, src.ident)) {
            return false;
        }

        // 相同时应该继续推导，当前函数后续需要进行 generics param 推导
    }

    if (src.ident_kind == TYPE_IDENT_DEF && src.kind != TYPE_UNION) {
        assert(src.ident != NULL);
        if (dst.ident == NULL) {
            return false;
        }
        if (!str_equal(dst.ident, src.ident)) {
            return false;
        }
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

        if (!type_compare(left_map_decl->key_type, right_map_decl->key_type, generics_param_table)) {
            return false;
        }

        if (!type_compare(left_map_decl->value_type, right_map_decl->value_type, generics_param_table)) {
            return false;
        }

        return true;
    }

    if (dst.kind == TYPE_SET) {
        type_set_t *left_decl = dst.set;
        type_set_t *right_decl = src.set;

        if (!type_compare(left_decl->element_type, right_decl->element_type, generics_param_table)) {
            return false;
        }

        return true;
    }

    if (dst.kind == TYPE_CHAN) {
        type_chan_t *left_decl = dst.chan;
        type_chan_t *right_decl = src.chan;
        return type_compare(left_decl->element_type, right_decl->element_type, generics_param_table);
    }

    if (dst.kind == TYPE_VEC) {
        type_vec_t *left_list_decl = dst.vec;
        type_vec_t *right_list_decl = src.vec;
        return type_compare(left_list_decl->element_type, right_list_decl->element_type, generics_param_table);
    }

    if (dst.kind == TYPE_ARR) {
        type_array_t *left_array_decl = dst.array;
        type_array_t *right_array_decl = src.array;
        if (left_array_decl->length != right_array_decl->length) {
            return false;
        }
        return type_compare(left_array_decl->element_type, right_array_decl->element_type, generics_param_table);
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
            if (!type_compare(*left_item, *right_item, generics_param_table)) {
                return false;
            }
        }
        return true;
    }

    if (dst.kind == TYPE_FN) {
        type_fn_t *left_type_fn = dst.fn;
        type_fn_t *right_type_fn = src.fn;
        if (!type_compare(left_type_fn->return_type, right_type_fn->return_type, generics_param_table)) {
            return false;
        }

        if (left_type_fn->param_types->length != right_type_fn->param_types->length) {
            return false;
        }

        if (left_type_fn->is_rest != right_type_fn->is_rest) {
            return false;
        }

        if (left_type_fn->is_errable != right_type_fn->is_errable) {
            return false;
        }

        for (int i = 0; i < left_type_fn->param_types->length; ++i) {
            type_t *left_formal_type = ct_list_value(left_type_fn->param_types, i);
            type_t *right_formal_type = ct_list_value(right_type_fn->param_types, i);
            if (!type_compare(*left_formal_type, *right_formal_type, generics_param_table)) {
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
            if (!type_compare(left_property->type, right_property->type, generics_param_table)) {
                return false;
            }
        }

        return true;
    }

    if (dst.kind == TYPE_PTR || dst.kind == TYPE_RAWPTR) {
        type_t left_pointer = dst.ptr->value_type;
        type_t right_pointer = src.ptr->value_type;
        return type_compare(left_pointer, right_pointer, generics_param_table);
    }

    return true;
}

/**
 * foo(a) -> fn foo<T, U>
 * table_t 响应
 * @param m
 * @param temp_fn
 * @param call
 * @param return_target_type
 * @return
 */
static table_t *generics_args_table(module_t *m, ast_fndef_t *temp_fn, ast_call_t *call, type_t return_target_type) {
    assert(temp_fn->is_generics);
    if (temp_fn->impl_type.kind > 0) {
        // 类型扩展必定存在 generics
        assert(call->generics_args);
    }

    // 进行参数解析，可以顺便使用 type_compare 了！
    table_t *generics_args_table = table_new();
    // call-generics_args 为 null 说明 caller 没有传递泛型 args, 此时需要根据调用参数进行自动泛型推导
    if (call->generics_args == NULL) {
        // 避免脏数据污染 is_tpl 导致后续的 copy 异常
        ct_stack_t *stash_stack = m->infer_type_args_stack;
        m->infer_type_args_stack = NULL;

        for (int i = 0; i < call->args->length; i++) {
            bool is_spread = call->spread && (i == call->args->length - 1);

            ast_expr_t *arg = ct_list_value(call->args, i);
            type_t arg_type = infer_right_expr(m, arg, type_kind_new(TYPE_UNKNOWN));

            // 形参 type reduction
            type_t *formal_type = select_generics_fn_param(temp_fn, i, is_spread);
            INFER_ASSERTF(formal_type, "too many arguments");

            // copy 出来专用于 infer, 避免对 is_tpl 造成污染(is_tpl.status = done, 会导致后续的 copy 异常)
            type_t temp_type = type_copy(*formal_type);
            temp_type = reduction_type(m, temp_type);

            bool compare = type_compare(temp_type, arg_type, generics_args_table);
            INFER_ASSERTF(compare, "cannot infer generics type from %s to %s", type_format(arg_type),
                          type_format(temp_type));
        }

        // 下面几个 target 类型对推导没有任何之之实质性帮助，所以直接跳过
        if (return_target_type.kind != TYPE_UNKNOWN &&
            return_target_type.kind != TYPE_VOID &&
            return_target_type.kind != TYPE_UNION &&
            return_target_type.kind != TYPE_NULL) {
            type_t temp_type = type_copy(temp_fn->return_type);
            temp_type = reduction_type(m, temp_type);
            bool compare = type_compare(temp_type, return_target_type, generics_args_table);
            INFER_ASSERTF(compare, "cannot infer generics type %s", type_format(temp_type));
        }

        // 判断泛型参数是否全部推断完成
        for (int i = 0; i < temp_fn->generics_params->length; i++) {
            ast_generics_param_t *param = ct_list_value(temp_fn->generics_params, i);
            type_t *arg_type = table_get(generics_args_table, param->ident);
            generics_constraints_check(m, &param->constraints, arg_type);
            INFER_ASSERTF(table_exist(generics_args_table, param->ident), "cannot infer generics param '%s'",
                          param->ident);
        }
        m->infer_type_args_stack = stash_stack;
    } else {
        // 按顺序生成 param
        for (int i = 0; i < call->generics_args->length; i++) {
            type_t *arg_type = ct_list_value(call->generics_args, i);
            *arg_type = reduction_type(m, *arg_type);
            ast_generics_param_t *param = ct_list_value(temp_fn->generics_params, i);
            table_set(generics_args_table, param->ident, arg_type);
        }
    }

    return generics_args_table;
}

static bool can_assign_to_interface(type_t t) {
    if (t.kind == TYPE_INTERFACE) {
        return false;
    }

    // void 在泛型约束中可以赋值给 any
    if (t.kind == TYPE_VOID || t.kind == TYPE_NULL) {
        return false;
    }

    return true;
}

static bool can_assign_to_union(type_t t) {
    if (t.kind == TYPE_UNION) {
        return false;
    }

    // void 在泛型约束中可以赋值给 any
    if (t.kind == TYPE_VOID) {
        return false;
    }

    return true;
}

/**
 * integer literal 可以转换为 float 类型，不做检查
 * float literal 可以转换为 integer 类型，不做检查
 * float literal 可以转换为 float 类型，不做检查
 *
 * integer literal 之间的转换需要进行 fit 检查
 */
static bool literal_as_check(module_t *m, ast_literal_t *literal, type_t target_type) {
    assert(is_number(target_type.kind));

    type_kind literal_kind = cross_kind_trans(literal->kind);
    type_kind target_kind = cross_kind_trans(target_type.kind);

    if (is_integer(literal->kind) && is_integer(target_type.kind)) {
        int64_t i = atoll(literal->value);
        return integer_range_check(target_kind, i);
    }

    return true;
}

/**
 * 直接根据 arg 进行 hash
 * @param args
 * @return
 */
static char *generics_impl_args_hash(module_t *m, list_t *args) {
    char *str = itoa(TYPE_FN);
    for (int i = 0; i < args->length; ++i) {
        type_t *arg_type = ct_list_value(args, i);
        *arg_type = reduction_type(m, *arg_type);
        rtype_t r = ct_reflect_type(*arg_type);
        str = str_connect(str, itoa((int64_t) r.hash));
    }

    return itoa(hash_string(str));
}

static char *generics_args_hash(list_t *generics_params, table_t *arg_table) {
    char *str = itoa(TYPE_FN);
    for (int i = 0; i < generics_params->length; ++i) {
        ast_generics_param_t *param = ct_list_value(generics_params, i);
        type_t *t = table_get(arg_table, param->ident);
        assert(t);
        rtype_t r = ct_reflect_type(*t);
        str = str_connect(str, itoa((int64_t) r.hash));
    }

    return itoa(hash_string(str));
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
    if (fndef->jit_closure_name) {
        fndef->jit_closure_name = str_connect_by(fndef->jit_closure_name, fndef->generics_args_hash,
                                                 GEN_REWRITE_SEPARATOR);

        ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
        var_decl->ident = fndef->jit_closure_name;
        var_decl->type = fndef->type;
        symbol_table_set(fndef->jit_closure_name, SYMBOL_VAR, var_decl, true);
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

static void rewrite_type_alias(module_t *m, ast_typedef_stmt_t *stmt) {
    assert(m->current_fn);

    // 如果不存在 params_hash 表示当前 fndef 不存在基于泛型的重写，所以 alias 也不需要进行重写
    if (!m->current_fn->generics_args_hash) {
        return;
    }

    stmt->ident = str_connect_by(stmt->ident, m->current_fn->generics_args_hash, GEN_REWRITE_SEPARATOR);
    symbol_table_set(stmt->ident, SYMBOL_TYPE, stmt, true);
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
static type_t infer_binary(module_t *m, ast_binary_expr_t *expr, type_t target_type) {
    assert(target_type.kind != TYPE_UNION);

    type_t left_target_type = type_kind_new(TYPE_UNKNOWN);
    if (ast_is_arithmetic_op(expr->operator)) {
        left_target_type = target_type;
    }

    // +/-/*/ ，由做表达式的类型决定, 并且如果左右表达式类型不一致，则抛出异常
    type_t left_type = infer_right_expr(m, &expr->left, left_target_type);
    INFER_ASSERTF(left_type.kind != TYPE_UNKNOWN, "unknown binary expr left type");

    type_t right_target_type = type_kind_new(TYPE_UNKNOWN);
    if (left_type.kind != TYPE_UNION) {
        right_target_type = left_type;
    }
    type_t right_type = infer_right_expr(m, &expr->right, right_target_type);

    // left type 和 right type 必须相同
    if (!type_compare(left_type, right_type, NULL)) {
        INFER_ASSERTF(false, "binary type inconsistency, left is '%s', right is '%s'", type_origin_format(left_type),
                      type_origin_format(right_type));
    }

    // 目前 binary 的两侧符号只支持 int 和 float
    if (is_number(left_type.kind)) {
        // 右值也必须是 number
        INFER_ASSERTF(is_number(right_type.kind),
                      "binary operator '%s' only support number operand, actual '%s %s %s'",
                      ast_expr_op_str[expr->operator],
                      type_format(left_type),
                      ast_expr_op_str[expr->operator],
                      type_format(right_type));
    }

    if (left_type.kind == TYPE_STRING) {
        // 右值必须是 string
        INFER_ASSERTF(right_type.kind == TYPE_STRING,
                      "binary operator '%s' only support string operand, actual '%s %s %s'",
                      ast_expr_op_str[expr->operator],
                      type_format(left_type),
                      ast_expr_op_str[expr->operator],
                      type_format(right_type));
    }

    if (is_bool_operand_operator(expr->operator)) {
        INFER_ASSERTF(left_type.kind == TYPE_BOOL && right_type.kind == TYPE_BOOL,
                      "binary operator '%s' only bool operand",
                      ast_expr_op_str[expr->operator]);
    }

    // 位运算只能支持整形
    if (is_integer_operator(expr->operator)) {
        INFER_ASSERTF(is_integer(left_type.kind) && is_integer(right_type.kind),
                      "binary operator '%s' only integer operand",
                      ast_expr_op_str[expr->operator]);
    }

    if (ast_is_arithmetic_op(expr->operator)) {
        return left_type;
    }

    if (ast_is_logic_op(expr->operator)) {
        return type_kind_new(TYPE_BOOL);
    }

    INFER_ASSERTF(false, "unknown operator '%s'", ast_expr_op_str[expr->operator]);
    exit(1);
}

/**
 *
 * 支持 integer/floater 类型之间相互进行转换
 * 支持 string 与 `[u8]` 类型之间相互转换
 * 支持 anyptr 与任意类型(除了 floater)之间相互转换
 * 后续联合类型断言也使用 as 语法
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

    type_t src_type = infer_expr(m, &as_expr->src, type_kind_new(TYPE_UNKNOWN));
    INFER_ASSERTF(src_type.kind != TYPE_UNKNOWN, "unknown as source type");
    as_expr->src.type = src_type;

    // anyptr 可以 as 为任意类型
    if (as_expr->src.type.kind == TYPE_ANYPTR && !is_float(target_type.kind)) {
        return target_type;
    }

    // 任意类型都可以 as anyptr
    if (!is_float(as_expr->src.type.kind) && target_type.kind == TYPE_ANYPTR) {
        return target_type;
    }

    // union(any) 可以 as 为任意类型
    if (as_expr->src.type.kind == TYPE_UNION) {
        INFER_ASSERTF(target_type.kind != TYPE_UNION, "cannot convert union type to union type");

        // target_type 必须包含再 union 中
        INFER_ASSERTF(union_type_contains(as_expr->src.type.union_, target_type),
                      "type '%s' not contains in union type",
                      type_format(target_type));

        return target_type;
    }

    // interface 可以 as 为任意类型(前提是实现了 interface)
    if (as_expr->src.type.kind == TYPE_INTERFACE) {
        type_t interface_type = as_expr->src.type;
        type_t temp_target_type = target_type;
        if (target_type.kind == TYPE_PTR || target_type.kind == TYPE_RAWPTR) {
            temp_target_type = target_type.ptr->value_type;
        }

        INFER_ASSERTF(temp_target_type.ident && temp_target_type.ident_kind == TYPE_IDENT_DEF,
                      "type '%s' not impl interface '%s'",
                      type_format(temp_target_type), type_format(interface_type));

        // 检测 target type 是否实现了 src interface type，并且编译时需要进行特殊处理
        symbol_t *s = symbol_table_get(temp_target_type.ident);
        assert(s && s->type == SYMBOL_TYPE);
        ast_typedef_stmt_t *typedef_stmt = s->ast_value;
        bool found = false;
        if (typedef_stmt->impl_interfaces) {
            for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
                type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
                assert(impl_interface->ident_kind == TYPE_IDENT_INTERFACE);
                if (str_equal(impl_interface->ident, interface_type.ident)) {
                    found = true;
                }
            }
        }
        // 禁止鸭子类型
        INFER_ASSERTF(found, "type '%s' not impl '%s' interface", src_type.ident, interface_type.ident);
        check_typedef_impl(m, &interface_type, temp_target_type.ident, typedef_stmt);

        return target_type;
    }

    // 特殊类型转换 string -> [u8]
    if (as_expr->src.type.kind == TYPE_STRING && is_list_u8(target_type)) {
        return target_type;
    }

    if (is_list_u8(as_expr->src.type) && target_type.kind == TYPE_STRING) {
        return target_type;
    }

    // number 之间可以相互进行类型转换, 但是需要进行 literal check
    if (is_number(as_expr->src.type.kind) && is_number(target_type.kind)) {
        // literal check, 避免不合适的类型转换
        if (as_expr->src.assert_type == AST_EXPR_LITERAL) {
            ast_literal_t *literal = as_expr->src.value;
            INFER_ASSERTF(literal_as_check(m, as_expr->src.value, target_type), "literal %s out of '%s' range",
                          literal->value, type_origin_format(target_type));
        }

        return target_type;
    }

    // 任意类型都可以转换为 typedef ident 类型，只要原始 kind 类型匹配即可.
    if (target_type.ident_kind == TYPE_IDENT_DEF) {
        char *ident = target_type.ident;
        type_ident_kind ident_kind = target_type.ident_kind;
        list_t *args = target_type.args;

        as_expr->src.type.ident = target_type.ident;
        as_expr->src.type.ident_kind = target_type.ident_kind;
        as_expr->src.type.args = target_type.args;

        // compare check
        if (type_compare(as_expr->src.type, target_type, NULL)) {
            as_expr->src.type.ident = ident;
            as_expr->src.type.ident_kind = ident_kind;
            as_expr->src.type.args = args;
            return target_type;
        }

        INFER_ASSERTF(false, "cannot casting to '%s'", type_format(target_type));
        //            as_expr->src.type.ident = ident;
        //            as_expr->src.type.ident_kind = ident_kind;
        //            as_expr->src.type.args = args;
    }

    // ident 可以转换为 builtin 类型, 从而继承 builtin 的 impl fn, 在 self arg 传参时需要处理
    if (as_expr->src.type.ident_kind == TYPE_IDENT_DEF && target_type.ident_kind == TYPE_IDENT_BUILTIN) {
        // 受到 ident 影响无法通过 compare, 则无法判断是否相同。
        type_t src_temp_type = as_expr->src.type;
        src_temp_type.ident = target_type.ident;
        src_temp_type.ident_kind = target_type.ident_kind;
        if (type_compare(target_type, src_temp_type, NULL)) {
            as_expr->src.type = src_temp_type;
            return target_type;
        }
    }

    // interface as to ident def must can check
    //    if (as_expr.src.type)

    INFER_ASSERTF(false, "cannot casting to '%s'", type_format(target_type));
    return target_type;
}

static type_t infer_new_expr(module_t *m, ast_new_expr_t *new_expr) {
    new_expr->type = reduction_type(m, new_expr->type);

    if (new_expr->type.kind == TYPE_STRUCT) {
        type_struct_t *type_struct = new_expr->type.struct_;
        if (new_expr->properties) {
            new_expr->properties = infer_struct_properties(m, type_struct, new_expr->properties);
        } else {
            // set default properties
            new_expr->properties = infer_struct_properties(m, type_struct, ct_list_new(sizeof(struct_property_t)));
        }
    } else {
        // only scalar type can use new
        INFER_ASSERTF(is_scala_type(new_expr->type) || new_expr->type.kind == TYPE_ARR,
                      "'new' operator can only be used with scalar types (number/boolean/struct/array)");
        if (new_expr->default_expr) {
            infer_right_expr(m, new_expr->default_expr, new_expr->type);
        }
    }


    return reduction_type(m, type_ptrof(new_expr->type));
}


/**
 * change to block expr
 * @param m
 * @param pVoid
 * @return
 */
static type_t infer_async(module_t *m, ast_expr_t *expr, type_t target_type) {
    ast_macro_async_t *co_expr = expr->value;

    type_fn_t *type_fn = infer_call_left(m, co_expr->origin_call, type_kind_new(TYPE_UNKNOWN));

    for (int i = 0; i < co_expr->args_copy_stmts->count; ++i) {
        ast_stmt_t *stmt = co_expr->args_copy_stmts->take[i];
        assert(stmt->assert_type == AST_STMT_VARDEF);
        ast_vardef_stmt_t *vardef = stmt->value;
        bool is_spread = co_expr->origin_call->spread && (i == co_expr->origin_call->args->length - 1);
        type_t *arg_type = select_fn_param(type_fn, i, is_spread);
        vardef->var_decl.type = *arg_type;
    }

    infer_body(m, co_expr->args_copy_stmts);

    if (co_expr->flag_expr == NULL) {
        co_expr->flag_expr = ast_int_expr(expr->line, expr->column, 0);
    }

    infer_right_expr(m, co_expr->flag_expr, type_kind_new(TYPE_INT));

    // check infer call
    infer_call(m, co_expr->origin_call, type_kind_new(TYPE_UNKNOWN), false);

    // check origin
    type_t fn_type = co_expr->origin_call->left.type;

    assert(fn_type.kind == TYPE_FN);
    co_expr->return_type = fn_type.fn->return_type;

    // -------------------------------------------- 消除 ast_async_t, 直接改造成 call async----------------------------------------------------------
    ast_expr_t first_arg = {0};

    // 如果原有的 go xxx() 没有参数和返回值，则直接使用原有版本的 origin call 传递给 async, 不需要包裹
    if (co_expr->return_type.kind == TYPE_VOID && co_expr->origin_call->args->length == 0) {
        first_arg = co_expr->origin_call->left;// 直接使用 left call fn ident, 而不是使用闭包
        first_arg.type.fn->is_errable = true;

        // 清空两个 fn body, 避免 infer void 异常
        co_expr->closure_fn->body = slice_new();
        co_expr->closure_fn_void->body = slice_new();
    } else {
        infer_fn_decl(m, co_expr->closure_fn, type_kind_new(TYPE_UNKNOWN));
        infer_fn_decl(m, co_expr->closure_fn_void, type_kind_new(TYPE_UNKNOWN));

        first_arg = (ast_expr_t){
                .line = expr->line,
                .column = expr->column,
                .assert_type = AST_FNDEF,
                .type = co_expr->closure_fn->type,
                .target_type = co_expr->closure_fn->type,
        };

        if (co_expr->return_type.kind == TYPE_VOID) {
            first_arg.value = co_expr->closure_fn_void;

            // 清空 closure_fn body, 避免 infer 异常, linear 才会剔除多余的 closure
            co_expr->closure_fn->body = slice_new();
        } else {
            first_arg.value = co_expr->closure_fn;
        }
    }

    ast_call_t *call_expr = NEW(ast_call_t);
    call_expr->left = *ast_ident_expr(expr->line, expr->column, BUILTIN_CALL_ASYNC);

    call_expr->args = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(call_expr->args, &first_arg);// ast_fndef
    ct_list_push(call_expr->args, co_expr->flag_expr);

    // 泛型参数
    call_expr->generics_args = ct_list_new(sizeof(type_t));
    ct_list_push(call_expr->generics_args, &co_expr->return_type);

    ast_expr_t *new_call_expr = NEW(ast_expr_t);
    new_call_expr->line = expr->line;
    new_call_expr->column = expr->column;
    new_call_expr->assert_type = AST_CALL;
    new_call_expr->value = call_expr;

    type_t result_type = infer_right_expr(m, new_call_expr, type_kind_new(TYPE_UNKNOWN));

    // change to block
    ast_block_expr_t *block_expr = NEW(ast_block_expr_t);

    block_expr->body = co_expr->args_copy_stmts;

    // add break expr
    ast_stmt_t *break_stmt = NEW(ast_stmt_t);
    break_stmt->line = new_call_expr->line;
    break_stmt->column = new_call_expr->column;
    ast_break_t *_break = NEW(ast_break_t);
    _break->expr = new_call_expr;
    break_stmt->assert_type = AST_STMT_BREAK;
    break_stmt->value = _break;

    stack_push(m->current_fn->break_target_types, &target_type);
    infer_break(m, _break);
    stack_pop(m->current_fn->break_target_types);

    slice_push(block_expr->body, break_stmt);

    expr->assert_type = AST_EXPR_BLOCK;
    expr->value = block_expr;

    return call_expr->return_type;
}

/**
 * @param m
 * @param match
 * @param target_type
 * @return
 */
static type_t infer_match(module_t *m, ast_match_t *match, type_t target_type) {
    // target 约束了 exec 的返回类型
    // subject 约束 case 的类型

    // 不存在 subject_type 时，case 的类型必须是 bool，否则和 subject_type 一致，其中 is xxx 特殊处理
    type_t subject_type = type_kind_new(TYPE_BOOL);
    if (match->subject) {
        subject_type = infer_right_expr(m, match->subject, type_kind_new(TYPE_UNKNOWN));
        INFER_ASSERTF(type_confirmed(subject_type), "match subject type not confirm");
    }

    stack_push(m->current_fn->break_target_types, &target_type);

    table_t *union_table = table_new();// key is hash

    bool has_default = false;

    // 类型一致
    SLICE_FOR(match->cases) {
        ast_match_case_t *match_case = SLICE_VALUE(match->cases);
        if (match_case->is_default) {
            has_default = true;
            goto DEFAULT_HANDLE;
        }

        for (int i = 0; i < match_case->cond_list->length; ++i) {
            ast_expr_t *cond_expr = ct_list_value(match_case->cond_list, i);

            // union 只能使用 is 匹配类型, 而不能进行值匹配
            if (subject_type.kind == TYPE_UNION) {
                INFER_ASSERTF(cond_expr->assert_type == AST_EXPR_MATCH_IS,
                              "match 'union type' only support 'is' assert");
            }

            if (cond_expr->assert_type == AST_EXPR_MATCH_IS) {
                INFER_ASSERTF(subject_type.kind == TYPE_UNION || subject_type.kind == TYPE_INTERFACE,
                              "%s cannot use 'is' operator",
                              type_format(subject_type));
                infer_right_expr(m, cond_expr, type_kind_new(TYPE_UNKNOWN));
                assert(cond_expr->type.kind == TYPE_BOOL);

                ast_match_is_expr_t *match_is_expr = cond_expr->value;
                rtype_t rtype = ct_reflect_type(match_is_expr->target_type);

                table_set(union_table, itoa(rtype.hash), cond_expr);
            } else {
                infer_right_expr(m, cond_expr, subject_type);
            }
        }

    DEFAULT_HANDLE:
        infer_body(m, match_case->handle_body);
    }

    // default check
    if (!has_default) {
        do {
            if (subject_type.kind == TYPE_UNION && !subject_type.union_->any) {
                for (int i = 0; i < subject_type.union_->elements->length; i++) {
                    type_t *element_type = ct_list_value(subject_type.union_->elements, i);
                    rtype_t rtype = ct_reflect_type(*element_type);
                    if (!table_exist(union_table, itoa(rtype.hash))) {
                        ANALYZER_ASSERTF(has_default,
                                         "match expression lacks a default case '_' and union element type lacks, for example 'is %s'",
                                         type_origin_format(*element_type))
                    }
                }

                break;// union completed
            }

            ANALYZER_ASSERTF(has_default, "match expression lacks a default case '_'")
        } while (0);
    }

    stack_pop(m->current_fn->break_target_types);
    return target_type;
}

static void infer_try_catch_stmt(module_t *m, ast_try_catch_stmt_t *try_stmt) {
    m->be_caught += 1;
    infer_body(m, try_stmt->try_body);
    m->be_caught -= 1;

    try_stmt->catch_err.type = interface_throwable();

    rewrite_var_decl(m, &try_stmt->catch_err);

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->break_target_types, &t);
    infer_body(m, try_stmt->catch_body);
    stack_pop(m->current_fn->break_target_types);
}

static type_t infer_catch(module_t *m, ast_catch_t *catch_expr) {
    m->be_caught += 1;
    type_t t = infer_right_expr(m, &catch_expr->try_expr, type_kind_new(TYPE_UNKNOWN));
    m->be_caught -= 1;

    type_t interface_error = interface_throwable();
    catch_expr->catch_err.type = interface_error;

    rewrite_var_decl(m, &catch_expr->catch_err);

    stack_push(m->current_fn->break_target_types, &t);
    infer_body(m, catch_expr->catch_body);
    stack_pop(m->current_fn->break_target_types);

    return t;
}

static type_t infer_match_is_expr(module_t *m, ast_match_is_expr_t *is_expr) {
    is_expr->target_type = reduction_type(m, is_expr->target_type);
    return type_kind_new(TYPE_BOOL);
}

static type_t infer_is_expr(module_t *m, ast_is_expr_t *is_expr) {
    type_t t = infer_right_expr(m, &is_expr->src, type_kind_new(TYPE_UNKNOWN));
    is_expr->target_type = reduction_type(m, is_expr->target_type);
    INFER_ASSERTF(t.kind == TYPE_UNION || t.kind == TYPE_INTERFACE, "%s cannot use 'is' operator", type_format(t));

    return type_kind_new(TYPE_BOOL);
}

static type_t infer_sizeof_expr(module_t *m, ast_macro_sizeof_expr_t *sizeof_expr, type_t target_type) {
    sizeof_expr->target_type = reduction_type(m, sizeof_expr->target_type);

    // literal 自动检测
    if (is_integer(target_type.kind) || target_type.kind == TYPE_ANYPTR) {
        return target_type;
    }

    return type_kind_new(TYPE_INT);
}

static type_t infer_ula_expr(module_t *m, ast_macro_ula_expr_t *ula_expr) {
    type_t t = infer_right_expr(m, &ula_expr->src, type_kind_new(TYPE_UNKNOWN));
    return type_ptrof(t);
}

static type_t infer_reflect_hash_expr(module_t *m, ast_macro_reflect_hash_expr_t *reflect_expr) {
    reflect_expr->target_type = reduction_type(m, reflect_expr->target_type);
    return type_kind_new(TYPE_INT);
}

static type_t infer_type_eq_expr(module_t *m, ast_expr_t *expr) {
    ast_macro_type_eq_expr_t *type_eq = expr->value;

    type_eq->left_type = reduction_type(m, type_eq->left_type);
    type_eq->right_type = reduction_type(m, type_eq->right_type);

    bool eq = type_compare(type_eq->left_type, type_eq->right_type, NULL);
    ast_expr_t *bool_expr = ast_bool_expr(expr->line, expr->column, eq);

    // 直接改写 literal 表达式
    expr->value = bool_expr->value;
    expr->assert_type = bool_expr->assert_type;

    return type_kind_new(TYPE_BOOL);
}

/**
 * unary
 * @param expr
 * @return
 */
static type_t infer_unary(module_t *m, ast_unary_expr_t *expr, type_t target_type) {
    if (expr->operator== AST_OP_NOT) {
        // bool 支持各种类型的 implicit type convert
        return infer_right_expr(m, &expr->operand, type_kind_new(TYPE_BOOL));
    }

    type_t operand_target_type = type_kind_new(TYPE_UNKNOWN);
    if (expr->operator== AST_OP_NEG) {
        operand_target_type = target_type;
    }

    type_t type = infer_right_expr(m, &expr->operand, operand_target_type);

    if ((expr->operator== AST_OP_NEG) && !is_number(type.kind)) {
        INFER_ASSERTF(false, "neg(-) must use in number，actual '%s'", type_format(type));
    }

    // &var
    if (expr->operator== AST_OP_LA) {
        INFER_ASSERTF(expr->operand.assert_type != AST_EXPR_LITERAL && expr->operand.assert_type != AST_CALL,
                      "cannot load address of an literal or call");

        INFER_ASSERTF(type.kind != TYPE_UNION, "cannot load address of an union type");


        return type_rawptrof(type);
    }

    // @unsafe_la(var)
    if (expr->operator== AST_OP_UNSAFE_LA) {
        INFER_ASSERTF(expr->operand.assert_type != AST_EXPR_LITERAL && expr->operand.assert_type != AST_CALL,
                      "cannot safe load address of an literal or call");

        INFER_ASSERTF(type.kind != TYPE_UNION, "cannot safe load address of an union type");


        return type_ptrof(type);
    }

    // *var
    if (expr->operator== AST_OP_IA) {
        INFER_ASSERTF(type.kind == TYPE_PTR || type.kind == TYPE_RAWPTR,
                      "cannot dereference non-pointer type '%s'", type_format(type));

        return type.ptr->value_type;
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
        ast_var_decl_t *symbol_var = symbol->ast_value;

        symbol_var->type = reduction_type(m, symbol_var->type);// 对全局符号表中的类型进行类型还原
        return symbol_var->type;
    }

    // 比如 print 和 println 都已经注册在了符号表中
    if (symbol->type == SYMBOL_FN) {
        ast_fndef_t *fndef = symbol->ast_value;
        return infer_fn_decl(m, fndef, type_kind_new(TYPE_UNKNOWN));
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
static type_t infer_vec_new(module_t *m, ast_expr_t *expr, type_t target_type) {
    ast_vec_new_t *vec_new = expr->value;

    if (target_type.kind == TYPE_ARR) {
        expr->assert_type = AST_EXPR_ARRAY_NEW;// 直接进行表达式类型的改写(vec_new 和 array_new 同构,所以可以这么做)

        // 严格限定类型为 array
        type_t result = type_kind_new(TYPE_ARR);
        result.status = REDUCTION_STATUS_UNDO;

        if (target_type.kind != TYPE_UNKNOWN) {
            result.ident = target_type.ident;// literal new ident 直接继承
            result.ident_kind = target_type.ident_kind;
            result.args = target_type.args;
        }

        type_array_t *type_array = NEW(type_array_t);

        type_array->element_type = target_type.array->element_type;
        type_array->length = target_type.array->length;

        result.array = type_array;
        if (vec_new->elements->length == 0) {
            return reduction_type(m, result);
        }

        for (int i = 0; i < vec_new->elements->length; ++i) {
            ast_expr_t *item_expr = ct_list_value(vec_new->elements, i);
            infer_right_expr(m, item_expr, type_array->element_type);
        }

        return reduction_type(m, result);
    }

    type_t result = type_kind_new(TYPE_VEC);
    result.status = REDUCTION_STATUS_UNDO;
    if (target_type.kind != TYPE_UNKNOWN) {
        result.ident = target_type.ident;// literal new ident 直接继承
        result.ident_kind = target_type.ident_kind;
        result.args = target_type.args;
    }

    type_vec_t *type_vec = NEW(type_vec_t);

    // 初始化时类型未知
    type_vec->element_type = type_kind_new(TYPE_UNKNOWN);

    if (target_type.kind == TYPE_VEC) {
        // 如果 target 强制约定了类型则直接使用 target 的类型, 否则就自己推断
        // 考虑到 list_new 可能为空的情况，所以这里默认赋值一次
        type_vec->element_type = target_type.vec->element_type;
    }

    result.vec = type_vec;
    if (vec_new->elements->length == 0) {
        INFER_ASSERTF(type_confirmed(type_vec->element_type), "vec element type not confirm");
        return reduction_type(m, result);
    }

    // target 类型不确定时，则按 list 首个元素类型进行推导
    if (type_vec->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(vec_new->elements, 0);
        type_vec->element_type = infer_right_expr(m, item_expr, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < vec_new->elements->length; ++i) {
        ast_expr_t *item_expr = ct_list_value(vec_new->elements, i);
        infer_right_expr(m, item_expr, type_vec->element_type);
    }

    return reduction_type(m, result);
}

static type_t infer_empty_curly_new(module_t *m, ast_expr_t *expr, type_t target_type) {
    if (target_type.kind == TYPE_UNKNOWN) {
        return target_type;
    }

    // 必须要 target 引导，才能确定具体的类型
    INFER_ASSERTF(target_type.kind > 0, "map key/value type cannot confirm");

    INFER_ASSERTF(target_type.kind == TYPE_MAP || target_type.kind == TYPE_SET, "{} cannot ref type %s",
                  type_format(target_type));

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
static type_t infer_map_new(module_t *m, ast_map_new_t *map_new, type_t target_type) {
    type_t result = type_kind_new(TYPE_MAP);
    result.status = REDUCTION_STATUS_UNDO;

    if (target_type.kind != TYPE_UNKNOWN) {
        result.ident = target_type.ident;// literal new ident 直接继承
        result.ident_kind = target_type.ident_kind;
        result.args = target_type.args;
    }


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
        INFER_ASSERTF(type_confirmed(type_map->key_type), "map key type not confirm");
        INFER_ASSERTF(type_confirmed(type_map->value_type), "map value type not confirm");
        return reduction_type(m, result);
    }

    // 基于首个元素进行类型推断
    if (type_map->key_type.kind == TYPE_UNKNOWN) {
        ast_map_element_t *item = ct_list_value(map_new->elements, 0);
        type_map->key_type = infer_right_expr(m, &item->key, type_kind_new(TYPE_UNKNOWN));
        type_map->value_type = infer_right_expr(m, &item->value, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < map_new->elements->length; ++i) {
        ast_map_element_t *item = ct_list_value(map_new->elements, i);
        infer_right_expr(m, &item->key, type_map->key_type);
        infer_right_expr(m, &item->value, type_map->value_type);
    }

    return reduction_type(m, result);
}

/**
 * {1, 2, a.b,value[1]}
 * @param set_new
 * @return
 */
static type_t infer_set_new(module_t *m, ast_set_new_t *set_new, type_t target_type) {
    type_t result = type_kind_new(TYPE_SET);
    result.status = REDUCTION_STATUS_UNDO;
    if (target_type.kind != TYPE_UNKNOWN) {
        result.ident = target_type.ident;// literal new ident 直接继承
        result.ident_kind = target_type.ident_kind;
        result.args = target_type.args;
    }

    type_set_t *type_set = NEW(type_set_t);
    type_set->element_type = type_kind_new(TYPE_UNKNOWN);

    // 右值如果有推荐的类型，则基于推荐类型做 infer, 此时可能会触发类型转换
    if (target_type.kind == TYPE_SET) {
        type_set->element_type = target_type.set->element_type;
    }

    result.set = type_set;
    if (set_new->elements->length == 0) {
        INFER_ASSERTF(type_confirmed(type_set->element_type), "set element type not confirm");
        return reduction_type(m, result);
    }
    // target 类型不确定则按首个元素类型进行推导
    if (type_set->element_type.kind == TYPE_UNKNOWN) {
        ast_expr_t *item_expr = ct_list_value(set_new->elements, 0);
        type_set->element_type = infer_right_expr(m, item_expr, type_kind_new(TYPE_UNKNOWN));
    }

    for (int i = 0; i < set_new->elements->length; ++i) {
        ast_expr_t *expr = ct_list_value(set_new->elements, i);
        infer_right_expr(m, expr, type_set->element_type);
    }

    return reduction_type(m, result);
}

static list_t *infer_struct_properties(module_t *m, type_struct_t *type_struct, list_t *properties) {
    table_t *exists = table_new();
    for (int i = 0; i < properties->length; ++i) {
        struct_property_t *struct_property = ct_list_value(properties, i);
        struct_property_t *expect_property = type_struct_property(type_struct, struct_property->key);

        INFER_ASSERTF(expect_property, "not found property '%s'", struct_property->key);

        table_set(exists, struct_property->key, struct_property);

        // struct_decl 已经是被还原过的类型了
        infer_right_expr(m, struct_property->right, expect_property->type);

        // type 冗余,方便计算 size (不能用来计算 offset)
        struct_property->type = expect_property->type;
    }


    // struct 默认参数处理, 默认参数的 infer 已经提前完成了，所以这里不需要二次重复进行 infer_right_expr
    list_t *default_properties = type_struct->properties;
    for (int i = 0; i < default_properties->length; ++i) {
        struct_property_t *d = ct_list_value(default_properties, i);
        // 右值复制
        if (!d->right || table_exist(exists, d->key)) {
            continue;
        }
        table_set(exists, d->key, d);

        ct_list_push(properties, d);
    }

    // check has default values.
    for (int i = 0; i < type_struct->properties->length; ++i) {
        struct_property_t *property = ct_list_value(type_struct->properties, i);
        if (table_exist(exists, property->key)) {
            continue;
        }

        // check can default value
        if (must_assign_value(property->type)) {
            INFER_ASSERTF(false, "struct filed '%s' must be assigned default value", property->key,
                          type_origin_format(property->type));
        }
    }


    return properties;
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
static type_t infer_struct_new(module_t *m, ast_expr_t *expr) {
    ast_struct_new_t *ast = expr->value;

    // 对类型进行了实例化 alias<int> -> struct{int}
    ast->type = reduction_type(m, ast->type);

    INFER_ASSERTF(ast->type.kind == TYPE_STRUCT, "'%s' not struct, cannot struct new", type_format(ast->type));

    // exists 记录已经存在的参数用来辅助 struct 默认参数
    list_t *properties = ast->properties;
    type_struct_t *type_struct = ast->type.struct_;
    ast->properties = infer_struct_properties(m, type_struct, properties);


    return ast->type;
}

/**
 * a[b]  list/map/tuple 都将通过中括号的方式进行访问
 * @param expr
 * @return
 */
static type_t infer_access_expr(module_t *m, ast_expr_t *expr) {
    ast_access_t *access = expr->value;
    type_t left_type = infer_right_expr(m, &access->left, type_kind_new(TYPE_UNKNOWN));

    if (left_type.kind == TYPE_PTR || left_type.kind == TYPE_RAWPTR) {
        if (left_type.ptr->value_type.kind == TYPE_ARR) {
            left_type = left_type.ptr->value_type;
        }
    }

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

    if (left_type.kind == TYPE_VEC || left_type.kind == TYPE_STRING) {
        type_t key_type = infer_right_expr(m, &access->key, type_integer_t_new());

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
        type_t key_type = infer_right_expr(m, &access->key, type_kind_new(TYPE_INT));

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
        type_t key_type = infer_right_expr(m, &access->key, type_kind_new(TYPE_INT));

        INFER_ASSERTF(access->key.assert_type = AST_EXPR_LITERAL, "tuple index field type must immediate value");

        type_tuple_t *type_tuple = left_type.tuple;

        ast_literal_t *index_literal = access->key.value;// 读取 index 的值
        INFER_ASSERTF(is_integer(index_literal->kind), "tuple index field must int immediate value");
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

    INFER_ASSERTF(false, "access only support must map/list/tuple, cannot '%s'", type_format(left_type));
    exit(1);
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
static type_t infer_select_expr(module_t *m, ast_expr_t *expr) {
    ast_expr_select_t *select = expr->value;

    infer_right_expr(m, &select->left, type_kind_new(TYPE_UNKNOWN));

    // infer 自动解引用
    // 不能直接改写 select->instance!
    type_t left_type = select->left.type;
    if (left_type.kind == TYPE_PTR | left_type.kind == TYPE_RAWPTR) {
        type_t value_type = select->left.type.ptr->value_type;
        if (value_type.kind == TYPE_STRUCT) {
            left_type = value_type;
        }
    }

    // TODO 在本次版本中，尝试不进行限制 raw ptr 的 . 语法，从而达到和 c 语言一样的效果
    //    INFER_ASSERTF(left_type.kind != TYPE_RAWPTR,
    //                  "%s cannot use select '.' operator", type_format(left_type));

    // ast_access to ast_struct_access
    if (left_type.kind == TYPE_STRUCT) {
        // 经过上面对 infer_right_expr, 这里对 type 一定是 reduction 的
        type_struct_t *type_struct = left_type.struct_;
        struct_property_t *p = type_struct_property(type_struct, select->key);
        INFER_ASSERTF(p, "type %s no property '%s'", type_format(left_type), select->key);

        p->type = reduction_type(m, p->type);

        // 改写
        ast_struct_select_t *struct_select = NEW(ast_struct_select_t);
        struct_select->instance = select->left;// 可能是 ptr<struct>/ rawptr<struct> / struct
        struct_select->key = select->key;
        struct_select->property = p;
        expr->assert_type = AST_EXPR_STRUCT_SELECT;
        expr->value = struct_select;


        return p->type;
    }

    INFER_ASSERTF(false, "no field named '%s' found in type '%s'", select->key, type_format(select->left.type));
    exit(1);
}

static void infer_call_args(module_t *m, ast_call_t *call, type_fn_t *target_type_fn) {
    // 由于支持 fndef is_rest 语言，所以实参的数量大于等于形参的数量
    if (!target_type_fn->is_rest && call->args->length > target_type_fn->param_types->length) {
        INFER_ASSERTF(false, "too many args");
    }
    if (!target_type_fn->is_rest && call->args->length < target_type_fn->param_types->length) {
        INFER_ASSERTF(false, "not enough args");
    }

    for (int i = 0; i < call->args->length; ++i) {
        bool is_spread = call->spread && (i == call->args->length - 1);

        // first param from formal
        type_t *formal_type = select_fn_param(target_type_fn, i, is_spread);

        ast_expr_t *arg = ct_list_value(call->args, i);

        infer_right_expr(m, arg, *formal_type);
    }
}

static ast_fndef_t *generics_special_fn(module_t *m, ast_call_t *call, type_t target_type, ast_fndef_t *temp_fn) {
    assert(!temp_fn->is_local);
    assert(temp_fn->is_generics);
    assert(temp_fn->generics_params->length > 0);

    // 一般泛型函数需要进行类型推断
    table_t *args_table = generics_args_table(m, temp_fn, call, target_type);

    // 根据具体参数计算 args_hash
    char *args_hash = generics_args_hash(temp_fn->generics_params, args_table);
    char *symbol_name = str_connect_by(temp_fn->symbol_name, args_hash, GEN_REWRITE_SEPARATOR);

    // 在没有基于类型约束产生重载的情况下，temp_fn 就是 tpl_fn
    // 在存在重载的情况下, 应该基于 arg_hash 查找具体类型的 tpl_fn
    // is_singleton_tpl 应该废弃，无论什么情况都进行 tpl_fn 的 clone, 避免泛型约束展开时的冲突问题
    ast_fndef_t *tpl_fn = temp_fn;
    //    bool is_singleton_tpl = false;
    symbol_t *symbol = symbol_table_get(symbol_name);
    if (symbol) {
        tpl_fn = symbol->ast_value;
        //        is_singleton_tpl = true;
    }

    if (tpl_fn->generics_hash_table == NULL) {
        tpl_fn->generics_hash_table = table_new();
    }

    ast_fndef_t *special_fn = table_get(tpl_fn->generics_hash_table, args_hash);
    if (special_fn) {
        return special_fn;
    }

    // local_children 关系也已经重新构建, analyzer_global 用于辅助 local_children 重建
    m->analyzer_global = NULL;

    special_fn = ast_fndef_copy(tpl_fn);
    special_fn->impl_type = tpl_fn->impl_type;

    // 分配泛型参数，此时泛型函数中的 type_param 还没有进行特化处理
    table_set(tpl_fn->generics_hash_table, args_hash, special_fn);

    special_fn->generics_args_hash = args_hash;
    special_fn->generics_args_table = args_table;

    // rewrite special fn
    special_fn->symbol_name = symbol_name;

    // 注册到全局符号表(还未基于 args_hash infer + reduction)
    assert(!special_fn->is_local);
    symbol_table_set(special_fn->symbol_name, SYMBOL_FN, special_fn, special_fn->is_local);
    // 下面的 infer_fn_decl 会进行 special_fn->type 的类型推导
    special_fn->type.status = REDUCTION_STATUS_UNDO;

    // 基于 args_table 进行类型特化，包含 special_fn 和 child
    stack_push(m->infer_type_args_stack, special_fn->generics_args_table);
    infer_fn_decl(m, special_fn, type_kind_new(TYPE_UNKNOWN));
    linked_push(m->temp_worklist, special_fn);

    for (int k = 0; k < special_fn->local_children->count; ++k) {
        ast_fndef_t *child = special_fn->local_children->take[k];
        rewrite_local_fndef(m, child);
        infer_fn_decl(m, child, type_kind_new(TYPE_UNKNOWN));
    }

    stack_pop(m->infer_type_args_stack);

    return special_fn;
}

static bool marking_heap_alloc(ast_expr_t *expr) {
    if (expr->assert_type == AST_EXPR_IDENT) {
        ast_ident *ident = expr->value;
        symbol_t *s = symbol_table_get(ident->literal);
        assert(s->type == SYMBOL_VAR);
        ast_var_decl_t *var = s->ast_value;
        var->type.in_heap = true;
        return true;
    }

    if (expr->assert_type == AST_EXPR_STRUCT_NEW) {
        ast_struct_new_t *ast = expr->value;
        expr->type.in_heap = true;
        ast->type.in_heap = true;

        return true;
    }

    if (expr->assert_type == AST_EXPR_ARRAY_NEW) {
        ast_array_new_t *ast = expr->value;
        expr->type.in_heap = true;

        return true;
    }

    // int/float/bool/string
    if (expr->assert_type == AST_EXPR_LITERAL && is_stack_alloc_type(expr->type)) {
        ast_literal_t *literal = expr->value;

        expr->type.in_heap = true;
        return true;
    }

    if (expr->assert_type == AST_EXPR_ARRAY_ACCESS) {
        ast_array_access_t *ast = expr->value;
        return marking_heap_alloc(&ast->left);
    }

    if (expr->assert_type == AST_EXPR_STRUCT_SELECT) {
        ast_struct_select_t *ast = expr->value;
        return marking_heap_alloc(&ast->instance);
    }

    // ([1, 3, 5] as foo_t).len()
    if (expr->assert_type == AST_EXPR_AS) {
        ast_as_expr_t *ast = expr->value;
        return marking_heap_alloc(&ast->src);
    }

    // 无需进行堆分配，可能是 vec_access/map_access 这种类型。
    return false;
}

// fn person_t<>.test() -> fn person_tx_test()
static bool infer_select_call_rewrite(module_t *m, ast_call_t *call) {
    ast_expr_select_t *select = call->left.value;

    // 这里已经对 left 进行了类型推导，所以后续不需要在进行类型推导了
    type_t select_left_type = infer_right_expr(m, &select->left, type_kind_new(TYPE_UNKNOWN));

    type_t destr_type = select_left_type;
    if (select_left_type.kind == TYPE_PTR || select_left_type.kind == TYPE_RAWPTR) {
        destr_type = select_left_type.ptr->value_type;
    }

    // struct key call 不需要改写，可以直接通过 struct(进行简单的解构操作) 直接定位到
    if (destr_type.kind == TYPE_STRUCT && type_struct_property(destr_type.struct_, select->key)) {
        return false;
    }

    // 比如 int? 转换为 union 时就不存在 ident
    if (!destr_type.ident) {
        return false;
    }

    // must alloc in heap
    //    INFER_ASSERTF(can_use_impl(select_left_type.kind), "%s cannot use impl call, must ptr<...>/vec/map...",
    //                  type_format(select_left_type), select->key);


    // call 继承 select_left_type 的 args
    char *impl_ident = destr_type.ident;
    char *impl_symbol_name = str_connect_by(impl_ident, select->key, "_");
    list_t *impl_args = destr_type.args;
    symbol_t *s;
    ast_expr_t *self_arg = &select->left;
    if (impl_args) {
        char *arg_hash = generics_impl_args_hash(m, impl_args);

        // 优先精确匹配，然后是通用匹配
        char *impl_symbol_name_with_hash = str_connect_by(impl_symbol_name, arg_hash, GEN_REWRITE_SEPARATOR);
        s = symbol_table_get(impl_symbol_name_with_hash);
        if (s) {
            impl_symbol_name = impl_symbol_name_with_hash;
        } else {
            s = symbol_table_get(impl_symbol_name);
        }

        INFER_ASSERTF(s, "type '%s' not impl '%s' fn", type_format(destr_type), select->key);
    } else {
        s = symbol_table_get(impl_symbol_name);
        if (!s) {
            // ident to default kind(need args)
            type_t temp_type = type_copy(destr_type);
            temp_type.ident = NULL;
            temp_type.ident_kind = 0;
            temp_type.args = NULL;
            temp_type.status = REDUCTION_STATUS_UNDO;
            temp_type = reduction_type(m, temp_type);
            // new impl
            if (temp_type.ident && temp_type.ident_kind == TYPE_IDENT_BUILTIN) {
                impl_ident = temp_type.ident;
                impl_args = temp_type.args;
                impl_symbol_name = str_connect_by(impl_ident, select->key, "_");
                if (impl_args) {
                    char *arg_hash = generics_impl_args_hash(m, impl_args);

                    // 优先精确匹配，然后是通用匹配
                    char *impl_symbol_name_with_hash = str_connect_by(impl_symbol_name, arg_hash,
                                                                      GEN_REWRITE_SEPARATOR);
                    s = symbol_table_get(impl_symbol_name_with_hash);
                    if (s) {
                        impl_symbol_name = impl_symbol_name_with_hash;
                    } else {
                        s = symbol_table_get(impl_symbol_name);
                    }
                } else {
                    impl_ident = destr_type.ident;
                    s = symbol_table_get(impl_symbol_name);
                }

                // if symbol name is builtin, change self arg type
            }

            INFER_ASSERTF(s, "type '%s' not impl '%s' fn", type_format(destr_type), select->key);
            if (select_left_type.kind == TYPE_PTR || select_left_type.kind == TYPE_RAWPTR) {
                self_arg->type.ptr->value_type = temp_type;
            } else {
                self_arg->type = temp_type;
            }
        }
    }

    call->left = *ast_ident_expr(call->left.line, call->left.column, impl_symbol_name);

    list_t *args = ct_list_new(sizeof(ast_expr_t));


    // self arg 可能在栈上分配，在 call fn 中会导致栈变量读取异常，所以需要确保 self arg 在堆上分配
    marking_heap_alloc(self_arg);
    if (is_stack_impl(self_arg->type.kind)) {// 基于原始类型计算是否需要 load ptr
        self_arg = ast_load_addr(self_arg);  // safe_load_addr
    }

    ct_list_push(args, self_arg);

    for (int i = 0; i < call->args->length; ++i) {
        ct_list_push(args, ct_list_value(call->args, i));
    }
    call->args = args;

    call->generics_args = impl_args;
    return true;
}

static type_fn_t *infer_call_left(module_t *m, ast_call_t *call, type_t target_type) {
    // --------------------------------------------interface call handle----------------------------------------------------------
    if (call->left.assert_type == AST_EXPR_SELECT) {
        do {
            ast_expr_select_t *select = call->left.value;
            type_t select_left_type = infer_right_expr(m, &select->left, type_kind_new(TYPE_UNKNOWN));
            if (select_left_type.ident_kind != TYPE_IDENT_INTERFACE) {
                break;
            }

            type_interface_t *interface_type = select_left_type.interface;

            // check have select key, and check params and return type
            type_t *temp = NULL;
            for (int i = 0; i < interface_type->elements->length; ++i) {
                type_t *element = ct_list_value(interface_type->elements, i);
                assert(element->kind == TYPE_FN);
                if (str_equal(element->fn->fn_name, select->key)) {
                    temp = element;
                    break;
                }
            }

            INFER_ASSERTF(temp, "interface '%s' not declare '%s' fn", select_left_type.ident, select->key);

            return temp->fn;
        } while (0);
    }

    // --------------------------------------------impl type handle----------------------------------------------------------
    // fn person_t<>.test() -> fn person_tx_test()
    if (call->left.assert_type == AST_EXPR_SELECT) {
        bool is_rewrite = infer_select_call_rewrite(m, call);
    }


    // analyzer 阶段已经对 ident 进行了 resolve/module ident with
    // --------------------------------------------generics handle----------------------------------------------------------
    if (call->left.assert_type == AST_EXPR_IDENT) {
        do {
            ast_ident *ident = call->left.value;
            symbol_t *s = symbol_table_get(ident->literal);
            INFER_ASSERTF(s, "symbol '%s' not found", ident->literal);

            // 可能是 local 闭包函数，此时 type 是一个 var
            if (s->is_local) {
                break;
            }

            // 可能是全局维度的闭包函数
            // INFER_ASSERTF(s->type == SYMBOL_FN, "ident '%s' call non-fn", ident->literal);
            if (s->type != SYMBOL_FN) {
                break;
            }

            ast_fndef_t *temp_fndef = s->ast_value;
            if (!temp_fndef->is_generics) {
                break;
            }

            // 由于存在函数重载，所以需要进行多次匹配找到最合适的 is_tpl 函数, 如果没有重载 temp_fndef 就是 tpl_fndef
            ast_fndef_t *special_fn = generics_special_fn(m, call, target_type, temp_fndef);
            assert(special_fn->type.status == REDUCTION_STATUS_DONE);

            // call ident 重写, 从而能够正确的从符号表中检索到 special_fn
            ident->literal = special_fn->symbol_name;

            // infer call args
            type_t left_type = infer_left_expr(m, &call->left);
            assert(left_type.kind == TYPE_FN);
            return left_type.fn;
        } while (0);
    }

    type_t left_type = infer_right_expr(m, &call->left, type_kind_new(TYPE_UNKNOWN));
    INFER_ASSERTF(left_type.kind == TYPE_FN, "cannot call non-fn");

    return left_type.fn;
}

/**
 * self.foo()
 * 由于重载的存在，对参数的 compare 变成了基于 type 的 search 的过程
 * 如果找不到目标函数则说明 key 不存在或者没有找到匹配的函数类型
 * @param call
 * @return
 */
static type_t infer_call(module_t *m, ast_call_t *call, type_t target_type, bool check_errable) {
    type_fn_t *type_fn = infer_call_left(m, call, target_type);
    infer_call_args(m, call, type_fn);

    call->return_type = type_fn->return_type;

    // catch 语句中可以包含多条 call 语句, 都统一处理了
    if (type_fn->is_errable && check_errable) {
        INFER_ASSERTF(m->current_fn->is_errable || m->be_caught > 0,
                      "calling an errable! fn `%s` requires the current `fn %s` errable! as well or be caught.",
                      type_fn->fn_name ? type_fn->fn_name : "lambda",
                      m->current_fn->fn_name);
    }

    return type_fn->return_type;
}

void infer_expr_fake(module_t *m, ast_expr_fake_stmt_t *stmt) {
    infer_right_expr(m, &stmt->expr, type_kind_new(TYPE_UNKNOWN));
    if (stmt->expr.assert_type == AST_MATCH && stmt->expr.type.kind == TYPE_UNKNOWN) {
        stmt->expr.type = type_kind_new(TYPE_VOID);
        stmt->expr.target_type = type_kind_new(TYPE_VOID);
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

    if (stmt->var_decl.type.ident_kind != TYPE_IDENT_PARAM) {
        INFER_ASSERTF(stmt->var_decl.type.kind != TYPE_VOID, "cannot assign to void");
    }

    type_t right_type = infer_right_expr(m, &stmt->right, stmt->var_decl.type);
    INFER_ASSERTF(right_type.kind != TYPE_VOID, "cannot assign void to var")

    // 需要进行类型推断
    if (stmt->var_decl.type.kind == TYPE_UNKNOWN) {
        INFER_ASSERTF(type_confirmed(right_type), "type inference error, right type not confirmed");

        stmt->var_decl.type = right_type;
        return;
    }
}

static void infer_global_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    stmt->var_decl.type = reduction_type(m, stmt->var_decl.type);
    type_t right_type = infer_right_expr(m, &stmt->right, stmt->var_decl.type);

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
    if (left_type.ident_kind != TYPE_IDENT_PARAM) {
        INFER_ASSERTF(left_type.kind != TYPE_VOID, "cannot assign to void");
    }

    infer_right_expr(m, &stmt->right, left_type);
}


static void infer_select(module_t *m, ast_select_stmt_t *stmt) {
    SLICE_FOR(stmt->cases) {
        ast_select_case_t *select_case = SLICE_VALUE(stmt->cases);

        if (select_case->on_call) {
            // call 改写 xxx.xxx.xxx.on_recv() -> chan_on_recv(xxx.xxx.xxx)
            infer_call(m, select_case->on_call, type_kind_new(TYPE_UNKNOWN), true);
        }

        if (select_case->recv_var) {
            // must recv
            select_case->recv_var->type = reduction_type(m, select_case->on_call->return_type);
            type_t type = select_case->recv_var->type;
            if (type.kind == TYPE_UNKNOWN || type.kind == TYPE_VOID || type.kind == TYPE_NULL) {
                INFER_ASSERTF(false, "variable declaration cannot use type '%s'", type_format(type));
            }

            rewrite_var_decl(m, select_case->recv_var);
        }

        infer_body(m, select_case->handle_body);
    }
}

static void infer_if(module_t *m, ast_if_stmt_t *stmt) {
    infer_right_expr(m, &stmt->condition, type_kind_new(TYPE_BOOL));

    infer_body(m, stmt->consequent);
    infer_body(m, stmt->alternate);
}

static void infer_for_cond_stmt(module_t *m, ast_for_cond_stmt_t *stmt) {
    infer_right_expr(m, &stmt->condition, type_kind_new(TYPE_BOOL));

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->break_target_types, &t);
    infer_body(m, stmt->body);
    stack_pop(m->current_fn->break_target_types);
}

/**
 * 仅 list 和 map 类型支持 iterate
 * @param stmt
 */
static void infer_for_iterator(module_t *m, ast_for_iterator_stmt_t *stmt) {
    // 经过 infer_right_expr 的类型一定是已经被还原过的
    type_t iterate_type = infer_right_expr(m, &stmt->iterate, type_kind_new(TYPE_UNKNOWN));
    INFER_ASSERTF(
            iterate_type.kind == TYPE_MAP || iterate_type.kind == TYPE_VEC || iterate_type.kind == TYPE_STRING ||
                    iterate_type.kind == TYPE_CHAN,
            "for in iterate type must be map/list/string/chan, actual=%s", type_format(iterate_type));

    rewrite_var_decl(m, &stmt->first);

    if (stmt->second) {
        rewrite_var_decl(m, stmt->second);
    }

    // 类型推断 (value 可选)
    ast_var_decl_t *first = &stmt->first;
    ast_var_decl_t *second = stmt->second;

    if (iterate_type.kind == TYPE_CHAN) {
        INFER_ASSERTF(second == NULL, "for chan only have one receive parameter");
    }

    // 为 key_decl 添加 type
    if (iterate_type.kind == TYPE_MAP) {
        type_map_t *type_map = iterate_type.map;
        first->type = type_map->key_type;
    } else if (iterate_type.kind == TYPE_CHAN) {
        type_chan_t *chan_decl = iterate_type.chan;
        first->type = chan_decl->element_type;
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
    stack_push(m->current_fn->break_target_types, &t);
    infer_body(m, stmt->body);
    stack_pop(m->current_fn->break_target_types);
}

static void infer_for_tradition(module_t *m, ast_for_tradition_stmt_t *stmt) {
    infer_stmt(m, stmt->init);
    infer_right_expr(m, &stmt->cond, type_kind_new(TYPE_BOOL));
    infer_stmt(m, stmt->update);

    type_t t = type_kind_new(TYPE_VOID);
    stack_push(m->current_fn->break_target_types, &t);
    infer_body(m, stmt->body);
    stack_pop(m->current_fn->break_target_types);
}

/**
 * type nullable<t> = null|t
 * @param m
 * @param stmt
 */
static void infer_type_alias_stmt(module_t *m, ast_typedef_stmt_t *stmt) {
    rewrite_type_alias(m, stmt);
}

/**
 * 但是我又怎么知道自己当前在哪个 closure_t 里面？
 * @param stmt
 */
static void infer_return(module_t *m, ast_return_stmt_t *stmt) {
    type_t expect_type = m->current_fn->return_type;
    if (stmt->expr != NULL) {
        infer_right_expr(m, stmt->expr, expect_type);
    } else {
        INFER_ASSERTF(expect_type.kind == TYPE_VOID, "fn expect return type: %s, but got void",
                      type_format(expect_type));
    }
}

static void infer_break(module_t *m, ast_break_t *stmt) {
    type_t *expect_type = stack_top(m->current_fn->break_target_types);
    assert(expect_type);
    if (stmt->expr != NULL) {
        type_t new_handle_type = infer_right_expr(m, stmt->expr, *expect_type);
        if (expect_type->kind == TYPE_UNKNOWN) {
            *expect_type = new_handle_type;
        }
    } else {
        INFER_ASSERTF(expect_type->kind == TYPE_VOID, "break missing value expr");
    }
}

static type_t infer_literal(module_t *m, ast_expr_t *expr, type_t target_type) {
    ast_literal_t *literal = expr->value;
    type_t literal_type = reduction_type(m, type_kind_new(literal->kind));
    literal->kind = literal_type.kind;

    type_kind target_kind = target_type.kind;
    if (target_kind == TYPE_ANYPTR) {
        target_kind = TYPE_UINT;
    }
    target_kind = cross_kind_trans(target_kind);

    if (is_float(literal_type.kind) && is_float(target_kind)) {
        literal->kind = target_kind;
        return target_type;
    }

    // int literal 自动转换为 float 类型, 并更新 literal value
    // int 转换为 float 类型
    if (is_integer(literal_type.kind) && is_float(target_kind)) {
        literal->kind = target_kind;
        return target_type;
    }

    // float 转换为 int 会导致数据丢失，所以不进行自动转换, 请手动使用 as 转换

    if (is_integer(literal_type.kind) && is_integer(target_kind)) {
        int64_t i = atoll(literal->value);
        if (integer_range_check(target_kind, i)) {// range 匹配直接返回，否则应该直接报错
            literal->kind = target_kind;
            return target_type;
        }

        INFER_ASSERTF(false, "literal '%s' out of range for type '%s'", literal->value, type_format(target_type));
    }

    return literal_type;
}

static type_t infer_env_access(module_t *m, ast_env_access_t *expr) {
    ast_ident ident = {
            .literal = expr->unique_ident,
    };
    return infer_ident(m, &ident);
}

static void infer_throw(module_t *m, ast_throw_stmt_t *throw_stmt) {
    INFER_ASSERTF(m->current_fn->is_errable,
                  "can't use throw stmt in a fn without an errable! declaration. example: fn %s(...):%s!",
                  m->current_fn->fn_name, type_origin_format(m->current_fn->return_type));

    infer_right_expr(m, &throw_stmt->error, interface_throwable());
}

/**
 * (a.b, b, (c[0], d[1])) = call()
 * 这里主要是推到左值部分的 type 并组装成一个完整的 tuple 类型返回
 * @param destr
 * @return
 */
static type_t infer_tuple_destr(module_t *m, ast_tuple_destr_t *destr) {
    type_t t = type_kind_new(TYPE_TUPLE);
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

        expr->type = *actual_type;// value is var_decl 不需要进行推导了
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
    type_t t = infer_right_expr(m, &stmt->right, type_kind_new(TYPE_UNKNOWN));

    INFER_ASSERTF(t.kind == TYPE_TUPLE, "cannot assign type '%s' to tuple", type_format(t));

    infer_var_tuple_destr(m, stmt->tuple_destr, t);
}

/**
 * @param m
 * @param tuple_new
 * @return
 */
static type_t infer_tuple_new(module_t *m, ast_tuple_new_t *tuple_new, type_t target_type) {
    type_t result = type_kind_new(TYPE_TUPLE);

    if (target_type.kind != TYPE_UNKNOWN) {
        result.ident = target_type.ident;// literal new ident 直接继承
        result.ident_kind = target_type.ident_kind;
        result.args = target_type.args;
    }

    type_tuple_t *tuple_type = NEW(type_tuple_t);
    tuple_type->elements = ct_list_new(sizeof(type_t));
    result.tuple = tuple_type;
    INFER_ASSERTF(tuple_new->elements->length > 0, "tuple elements empty");
    for (int i = 0; i < tuple_new->elements->length; ++i) {
        type_t element_target_type = type_kind_new(TYPE_UNKNOWN);
        if (target_type.kind == TYPE_TUPLE) {
            type_t *temp = ct_list_value(target_type.tuple->elements, i);
            element_target_type = *temp;
        }

        ast_expr_t *expr = ct_list_value(tuple_new->elements, i);
        type_t expr_type = infer_right_expr(m, expr, element_target_type);

        INFER_ASSERTF(type_confirmed(expr_type), "tuple element type type cannot confirmed");

        ct_list_push(tuple_type->elements, &expr_type);
    }

    return result;
}

static void infer_stmt(module_t *m, ast_stmt_t *stmt) {
    SET_LINE_COLUMN(stmt);

    switch (stmt->assert_type) {
        case AST_STMT_EXPR_FAKE: {
            return infer_expr_fake(m, stmt->value);
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
            infer_call(m, stmt->value, type_kind_new(TYPE_VOID), true);
            break;
        }
        case AST_CATCH: {
            infer_catch(m, stmt->value);
            break;
        }
        case AST_STMT_TRY_CATCH: {
            return infer_try_catch_stmt(m, stmt->value);
        }
        case AST_STMT_SELECT: {
            return infer_select(m, stmt->value);
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
        case AST_STMT_BREAK: {
            return infer_break(m, stmt->value);
        }
        case AST_STMT_TYPEDEF: {
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
    SET_LINE_COLUMN(expr);

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
            type = infer_access_expr(m, expr);
            break;
        }
        case AST_EXPR_SELECT: {
            type = infer_select_expr(m, expr);
            break;
        }
        case AST_EXPR_ENV_ACCESS: {
            type = infer_env_access(m, expr->value);
            break;
        }
        case AST_CALL: {
            type = infer_call(m, expr->value, type_kind_new(TYPE_UNKNOWN), true);
            break;
        }
        case AST_EXPR_UNARY: {
            // only support star
            ast_unary_expr_t *unary_expr = expr->value;
            if (unary_expr->operator== AST_OP_IA) {
                type = infer_unary(m, unary_expr, type_kind_new(TYPE_UNKNOWN));
            } else {
                INFER_ASSERTF(false, "unary operand cannot used in left");
            }
            break;
        }
        default: {
            INFER_ASSERTF(false, "operand cannot used in left");
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
static type_t infer_expr(module_t *m, ast_expr_t *expr, type_t target_type) {
    SET_LINE_COLUMN(expr);
    if (expr->type.kind > 0) {
        return expr->type;
    }
    switch (expr->assert_type) {
        case AST_EXPR_AS: {
            return infer_as_expr(m, expr);
        }
        case AST_CATCH: {
            return infer_catch(m, expr->value);
        }
        case AST_MATCH: {
            return infer_match(m, expr->value, target_type);
        }
        case AST_EXPR_MATCH_IS: {
            return infer_match_is_expr(m, expr->value);
        }
        case AST_EXPR_IS: {
            return infer_is_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_SIZEOF: {
            return infer_sizeof_expr(m, expr->value, target_type);
        }
        case AST_MACRO_EXPR_REFLECT_HASH: {
            return infer_reflect_hash_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_ULA: {
            return infer_ula_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_DEFAULT: {
            return target_type;
        }
        case AST_MACRO_EXPR_TYPE_EQ: {
            return infer_type_eq_expr(m, expr);
        }
        case AST_EXPR_NEW: {
            return infer_new_expr(m, expr->value);
        }
        case AST_EXPR_BINARY: {
            return infer_binary(m, expr->value, target_type);
        }
        case AST_EXPR_UNARY: {
            return infer_unary(m, expr->value, target_type);
        }
        case AST_EXPR_IDENT: {
            return infer_ident(m, expr->value);
        }
        case AST_EXPR_VEC_NEW: {// literal casting
            return infer_vec_new(m, expr, target_type);
        }
        case AST_EXPR_EMPTY_CURLY_NEW: {// literal casting
            return infer_empty_curly_new(m, expr, target_type);
        }
        case AST_EXPR_MAP_NEW: {// literal casting
            return infer_map_new(m, expr->value, target_type);
        }
        case AST_EXPR_SET_NEW: {// literal casting
            return infer_set_new(m, expr->value, target_type);
        }
        case AST_FNDEF: {// literal casting
            return infer_fn_decl(m, expr->value, target_type);
        }
        case AST_EXPR_LITERAL: {// literal casting
            return infer_literal(m, expr, target_type);
        }
        case AST_EXPR_TUPLE_NEW: {
            return infer_tuple_new(m, expr->value, target_type);
        }
        case AST_EXPR_STRUCT_NEW: {
            return infer_struct_new(m, expr);
        }
        case AST_EXPR_ACCESS: {
            // 这里需要做类型改写，确定具体的访问类型所以传递整个表达式
            return infer_access_expr(m, expr);
        }
        case AST_EXPR_SELECT: {
            return infer_select_expr(m, expr);
        }
        case AST_CALL: {
            return infer_call(m, expr->value, target_type, true);
        }
        case AST_MACRO_ASYNC: {
            return infer_async(m, expr, target_type);
        }
        case AST_EXPR_ENV_ACCESS: {
            return infer_env_access(m, expr->value);
        }
        default: {
            INFER_ASSERTF(false, "unknown operand %d", expr->assert_type);
            exit(EXIT_FAILURE);
        }
    }
}

static bool is_nullable_interface(type_t target_type) {
    if (target_type.kind != TYPE_UNION) {
        return false;
    }

    type_union_t *union_type = target_type.union_;
    if (!union_type->nullable) {
        return false;
    }

    assert(union_type->elements->length == 2);
    type_t *first_type = ct_list_value(union_type->elements, 0);
    return first_type->kind == TYPE_INTERFACE;
}

static ast_expr_t as_to_interface(module_t *m, ast_expr_t *expr, type_t interface_type) {
    assert(interface_type.ident_kind == TYPE_IDENT_INTERFACE);
    assert(expr->type.status == REDUCTION_STATUS_DONE);
    assert(interface_type.status == REDUCTION_STATUS_DONE);

    // auto destr ptr
    type_t src_type = expr->type;
    if (expr->type.kind == TYPE_PTR || expr->type.kind == TYPE_RAWPTR) {
        src_type = expr->type.ptr->value_type;
    }

    INFER_ASSERTF(src_type.ident_kind > 0 && src_type.ident_kind == TYPE_IDENT_DEF,
                  "type '%s' cannot casting to interface '%s'",
                  type_format(src_type), interface_type.ident);

    symbol_t *s = symbol_table_get(src_type.ident);
    assert(s && s->type == SYMBOL_TYPE);
    ast_typedef_stmt_t *typedef_stmt = s->ast_value;

    // check actual_type impl 是否包含 target_type.ident
    bool found = false;
    if (typedef_stmt->impl_interfaces) {
        for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
            type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
            assert(impl_interface->ident_kind == TYPE_IDENT_INTERFACE);
            if (str_equal(impl_interface->ident, interface_type.ident)) {
                found = true;
            }
        }
    }
    // 禁止鸭子类型
    INFER_ASSERTF(found, "type '%s' not impl '%s' interface", src_type.ident, interface_type.ident);

    check_typedef_impl(m, &interface_type, src_type.ident, typedef_stmt);

    // auto as to interface, expr as interface_union, will handle in linear
    return ast_type_as(*expr, interface_type);
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
    SET_LINE_COLUMN(expr);

    // 避免重复 reduction
    if (expr->type.kind == 0) {
        type_t type = infer_expr(m, expr, target_type.kind != TYPE_UNION ? target_type : type_kind_new(TYPE_UNKNOWN));

        target_type = reduction_type(m, target_type);
        expr->type = reduction_type(m, type);
        expr->target_type = target_type;
    }

    // TYPE_UNKNOWN 表示需要进行类型推断，此时什么都不做，交给调用者进行判断
    if (target_type.kind == TYPE_UNKNOWN) {
        return expr->type;
    }

    // handle interface
    if (target_type.kind == TYPE_INTERFACE && can_assign_to_interface(expr->type)) {
        // auto as to interface, expr as interface_union, will handle in linear
        *expr = as_to_interface(m, expr, target_type);
    }

    if (is_nullable_interface(target_type) && can_assign_to_interface(expr->type)) {
        type_union_t *union_type = target_type.union_;
        assert(union_type->nullable);
        assert(union_type->elements->length == 2);
        type_t *interface_type = ct_list_value(union_type->elements, 0);
        assert(interface_type->ident_kind == TYPE_IDENT_INTERFACE);

        // first casting to interface
        *expr = as_to_interface(m, expr, *interface_type);
    }

    if (target_type.kind == TYPE_UNION && can_assign_to_union(expr->type)) {
        // auto as，为了后续的 linear, 需要将 signal type 转换为 union type
        INFER_ASSERTF(union_type_contains(target_type.union_, expr->type), "union type not contains '%s'",
                      type_format(expr->type));

        // linear 检测到 signal_type as union_type 会调用 RT_CALL_UNION_CASTING 进行转换
        *expr = ast_type_as(*expr, target_type);
    }

    if (!type_compare(target_type, expr->type, NULL)) {
        INFER_ASSERTF(false, "type inconsistency, expect=%s, actual=%s", type_format(target_type),
                      type_format(expr->type));
    }
    return expr->type;
}

static type_t reduction_struct(module_t *m, type_t t) {
    INFER_ASSERTF(t.kind == TYPE_STRUCT, "type kind=%s unexpect", type_format(t));

    type_struct_t *s = t.struct_;

    int max_align = 0;
    for (int i = 0; i < s->properties->length; ++i) {
        struct_property_t *p = ct_list_value(s->properties, i);

        if (p->type.kind != TYPE_UNKNOWN) {
            p->type = reduction_type(m, p->type);
        }

        if (p->right) {
            type_t right_type = infer_right_expr(m, p->right, p->type);
            if (p->type.kind == TYPE_UNKNOWN) {
                INFER_ASSERTF(type_confirmed(right_type), "struct property '%s' type not confirmed", p->key);
                p->type = right_type;
            }
        }

        int item_align = type_alignof(p->type);
        if (item_align > max_align) {
            max_align = item_align;
        }

        INFER_ASSERTF(type_confirmed(p->type), "struct property '%s' type not confirmed", p->key);
    }
    t.struct_->align = max_align;
    return t;
}

static type_t reduction_complex_type(module_t *m, type_t t) {
    if (t.kind == TYPE_PTR || t.kind == TYPE_RAWPTR) {
        type_ptr_t *type_pointer = t.ptr;
        type_pointer->value_type = reduction_type(m, type_pointer->value_type);
        return t;
    }

    if (t.kind == TYPE_ARR) {
        type_array_t *type_array = t.array;
        type_array->element_type = reduction_type(m, type_array->element_type);

        // not impl

        return t;
    }

    if (t.kind == TYPE_CHAN) {
        type_chan_t *type_chan = t.chan;
        type_chan->element_type = reduction_type(m, type_chan->element_type);


        t.ident_kind = TYPE_IDENT_BUILTIN;
        t.ident = type_kind_str[TYPE_CHAN];
        t.args = ct_list_new(sizeof(type_t));
        ct_list_push(t.args, &type_chan->element_type);

        return t;
    }

    if (t.kind == TYPE_VEC) {
        type_vec_t *type_vec = t.vec;
        type_vec->element_type = reduction_type(m, type_vec->element_type);

        // vec.len 都是 impl fn 实现，但是如果是 type my_vec = [int] 这样的实现，impl_ident 应该使用默认的 ident。
        t.ident_kind = TYPE_IDENT_BUILTIN;
        t.ident = type_kind_str[TYPE_VEC];
        t.args = ct_list_new(sizeof(type_t));
        ct_list_push(t.args, &type_vec->element_type);

        return t;
    }

    if (t.kind == TYPE_MAP) {
        t.map->key_type = reduction_type(m, t.map->key_type);
        t.map->value_type = reduction_type(m, t.map->value_type);

        INFER_ASSERTF(is_map_set_key_type(t.map->key_type.kind),
                      "type '%s' not support as map key", type_format(t.map->key_type));

        t.ident_kind = TYPE_IDENT_BUILTIN;
        t.ident = type_kind_str[TYPE_MAP];
        t.args = ct_list_new(sizeof(type_t));
        ct_list_push(t.args, &t.map->key_type);
        ct_list_push(t.args, &t.map->value_type);


        return t;
    }

    if (t.kind == TYPE_SET) {
        t.set->element_type = reduction_type(m, t.set->element_type);
        INFER_ASSERTF(is_map_set_key_type(t.set->element_type.kind),
                      "type '%s' not support as set element", type_format(t.set->element_type));

        t.ident_kind = TYPE_IDENT_BUILTIN;
        t.ident = type_kind_str[TYPE_SET];
        t.args = ct_list_new(sizeof(type_t));
        ct_list_push(t.args, &t.set->element_type);

        return t;
    }

    if (t.kind == TYPE_TUPLE) {
        type_tuple_t *tuple = t.tuple;
        INFER_ASSERTF(tuple->elements->length > 0, "tuple element empty");
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

    INFER_ASSERTF(false, "unknown type=%s", type_format(t));
    exit(1);
}

static type_t type_param_special(module_t *m, type_t t, table_t *arg_table) {
    assert(t.kind == TYPE_IDENT);
    assert(t.ident_kind == TYPE_IDENT_PARAM);

    // 实参可以没有 reduction
    type_t *arg_type = table_get(arg_table, t.ident);
    return reduction_type(m, *arg_type);
}

static void
check_typedef_impl(module_t *m, type_t *impl_interface, char *typedef_ident, ast_typedef_stmt_t *typedef_stmt) {
    assert(impl_interface->ident_kind == TYPE_IDENT_INTERFACE);
    assert(impl_interface->status == REDUCTION_STATUS_DONE);
    assert(impl_interface->kind == TYPE_INTERFACE);
    type_interface_t *interface_type = impl_interface->interface;

    // check impl
    for (int j = 0; j < interface_type->elements->length; ++j) {
        type_t *expect_type = ct_list_value(interface_type->elements, j);
        assert(expect_type->status == REDUCTION_STATUS_DONE);
        assert(expect_type->kind == TYPE_FN);
        type_fn_t *interface_fn_type = expect_type->fn;
        assert(interface_fn_type->fn_name);

        // find fn from symbol table
        char *fn_ident = str_connect_by(typedef_ident, interface_fn_type->fn_name, "_");

        // find ast_fn by teypdef_stmt.sc_map
        ast_fndef_t *ast_fndef = sc_map_get_sv(&typedef_stmt->method_table, fn_ident);

        INFER_ASSERTF(ast_fndef, "type '%s' not impl fn '%s' for interface '%s'",
                      typedef_ident,
                      interface_fn_type->fn_name,
                      impl_interface->ident);

        type_t actual_type = infer_impl_fn_decl(m, ast_fndef);

        // actual_type 由于进行了 rewrite, 所以 param 包含了 self, 而 expect type 没有进行这样的处理。
        // compare
        INFER_ASSERTF(type_compare(*expect_type, actual_type, NULL),
                      "the fn '%s' of type '%s' mismatch interface '%s'", ast_fndef->fn_name, typedef_ident,
                      impl_interface->ident);
    }
}

static void combination_interface(module_t *m, ast_typedef_stmt_t *typedef_stmt) {
    assert(typedef_stmt->type_expr.status == REDUCTION_STATUS_DONE);
    assert(typedef_stmt->type_expr.kind == TYPE_INTERFACE);
    type_interface_t *origin_interface = typedef_stmt->type_expr.interface;
    struct sc_map_sv exists = {0};
    sc_map_init_sv(&exists, 0, 0);// value is type_fn_t

    for (int i = 0; i < origin_interface->elements->length; ++i) {
        type_t *temp = ct_list_value(origin_interface->elements, i);
        assert(temp->kind == TYPE_FN);
        sc_map_put_sv(&exists, temp->fn->fn_name, temp);
    }

    // combination
    for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
        type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
        *impl_interface = reduction_type(m, *impl_interface);
        assert(impl_interface->kind == TYPE_INTERFACE);
        for (int j = 0; j < impl_interface->interface->elements->length; ++j) {
            type_t *temp = ct_list_value(impl_interface->interface->elements, j);
            // check exists
            type_t *exist_type = sc_map_get_sv(&exists, temp->fn->fn_name);
            if (exist_type) {
                // compare
                INFER_ASSERTF(type_compare(*exist_type, *temp, NULL), "duplicate method '%s'",
                              temp->fn->fn_name);

                continue;
            }

            sc_map_put_sv(&exists, temp->fn->fn_name, temp);
            ct_list_push(origin_interface->elements, temp);
        }
    }
}

/**
 * custom_type a = ...
 * custom_type 此时就是一个 type_alias
 * @param m
 * @param t
 * @return
 */
static type_t reduction_typedef_or_alias(module_t *m, type_t t) {
    symbol_t *symbol = symbol_table_get(t.ident);
    INFER_ASSERTF(symbol, "typedef '%s' not found", t.ident);
    INFER_ASSERTF(symbol->type == SYMBOL_TYPE, "'%s' is not a type", symbol->ident);

    // 此时的 symbol 可能是其他 module 中声明的符号
    ast_typedef_stmt_t *typedef_stmt = symbol->ast_value;

    // 判断是否包含 args, 如果包含 args 则需要每一次 reduction 都进行处理
    // 此时的 type alias 相当于重新定义了一个新的类型，并进行了 type_copy, 所以不会存在 reduction 冲突问题, 可以直接返回
    if (typedef_stmt->params) {
        INFER_ASSERTF(t.args, "typedef '%s' need param", t.ident);
        INFER_ASSERTF(t.args->length == typedef_stmt->params->length, "typedef '%s' params mismatch",
                      t.ident);

        // 对 arg 进行 reduction 并校验类型归属
        for (int i = 0; i < t.args->length; ++i) {
            type_t *arg = ct_list_value(t.args, i);
            *arg = reduction_type(m, *arg);

            ast_generics_param_t *param = ct_list_value(typedef_stmt->params, i);

            generics_constraints_check(m, &param->constraints, arg);
        }

        // 此时只是使用 module 作为一个 context 使用，实际上 type_alias_stmt->params 和 当前 module 并不是同一个文件中的
        if (m->infer_type_args_stack) {
            table_t *args_table = table_new();

            for (int i = 0; i < t.args->length; ++i) {
                type_t *arg = ct_list_value(t.args, i);
                *arg = reduction_type(m, *arg);

                ast_generics_param_t *param = ct_list_value(typedef_stmt->params, i);
                table_set(args_table, param->ident, arg);
            }

            // 添加 arg_table 进行后续的 right type handle
            stack_push(m->infer_type_args_stack, args_table);
        }

        // 进行 impl 校验， 如果存在泛型，还需要进行泛型展开
        if (typedef_stmt->impl_interfaces) {
            if (typedef_stmt->is_interface) {
                assert(typedef_stmt->type_expr.status == REDUCTION_STATUS_DONE);
                assert(typedef_stmt->type_expr.kind == TYPE_INTERFACE);
                combination_interface(m, typedef_stmt);
            } else {
                for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
                    type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
                    *impl_interface = reduction_type(m, *impl_interface);
                    check_typedef_impl(m, impl_interface, t.ident, typedef_stmt);
                }
            }
        }

        // 对右值进行 copy, 因为泛型会对 type_expr 进行多次展开, 所以需要进行 copy
        type_t right_type_expr = type_copy(typedef_stmt->type_expr);

        // reduction 部分的 struct 的 right expr 如果是 struct，也只会进行到 infer_fn_decl 而不会处理 fn body 部分
        // 所以 fn body 部分还是包含 type_param, 如果此时将 type_param_table 置空，会导致后续 infer_fndef 时解析 param 异常
        // 更加正确的做法应该是将 type_param_table 赋值给相应的 ast_fndef
        right_type_expr = reduction_type(m, right_type_expr);

        if (m->infer_type_args_stack) {
            stack_pop(m->infer_type_args_stack);
        }

        t.kind = right_type_expr.kind;
        t.value = right_type_expr.value;
        t.in_heap = right_type_expr.in_heap;
        t.status = right_type_expr.status;
        return t;
    } else {
        INFER_ASSERTF(t.args == NULL, "typedef '%s' args mismatch",
                      t.ident);
    }


    // 检查右值是否 reduce 完成
    if (typedef_stmt->type_expr.status == REDUCTION_STATUS_DONE) {
        t.kind = typedef_stmt->type_expr.kind;
        t.value = typedef_stmt->type_expr.value;
        t.in_heap = typedef_stmt->type_expr.in_heap;
        t.status = typedef_stmt->type_expr.status;
        return t;
    }

    // 当前 ident 对应的 type 正在 reduction, 出现这种情况可能的原因是嵌套使用了 ident
    // 此时直接将 ident 丢回去就可以了
    if (typedef_stmt->type_expr.status == REDUCTION_STATUS_DOING) {
        return t;
    }

    // 打上正在进行的标记,避免进入死循环
    typedef_stmt->type_expr.status = REDUCTION_STATUS_DOING;

    // interface 需要通过 ident 进行区分，所以这里将 ident 赋值给 type_expr
    if (typedef_stmt->is_interface) {
        typedef_stmt->type_expr.ident_kind = TYPE_IDENT_INTERFACE;
        typedef_stmt->type_expr.ident = t.ident;
    }

    typedef_stmt->type_expr = reduction_type(m, typedef_stmt->type_expr);

    // check impl
    if (typedef_stmt->impl_interfaces) {
        if (typedef_stmt->is_interface) {
            assert(typedef_stmt->type_expr.status == REDUCTION_STATUS_DONE);
            assert(typedef_stmt->type_expr.kind == TYPE_INTERFACE);
            combination_interface(m, typedef_stmt);
        } else {
            for (int i = 0; i < typedef_stmt->impl_interfaces->length; ++i) {
                type_t *impl_interface = ct_list_value(typedef_stmt->impl_interfaces, i);
                *impl_interface = reduction_type(m, *impl_interface);
                check_typedef_impl(m, impl_interface, t.ident, typedef_stmt);
            }
        }
    }

    // combination impl interfaces

    // reduction_type 已经返回，现在这里可以直接修改为 done, 或者在外部的 reduction_type 中配置为 done

    t.kind = typedef_stmt->type_expr.kind;
    t.value = typedef_stmt->type_expr.value;
    t.in_heap = typedef_stmt->type_expr.in_heap;
    t.status = typedef_stmt->type_expr.status;
    return t;
}

static type_t reduction_union_type(module_t *m, type_t t) {
    type_union_t *type_union = t.union_;
    if (type_union->elements->length == 0) {
        return t;
    }

    for (int i = 0; i < t.union_->elements->length; ++i) {
        type_t *temp = ct_list_value(t.union_->elements, i);
        *temp = reduction_type(m, *temp);
    }

    return t;
}

static type_t reduction_interface_type(module_t *m, type_t t) {
    type_interface_t *type_interface = t.interface;
    if (type_interface->elements->length == 0) {
        return t;
    }

    for (int i = 0; i < type_interface->elements->length; ++i) {
        type_t *temp = ct_list_value(type_interface->elements, i);
        *temp = reduction_type(m, *temp);
    }

    return t;
}

static type_t reduction_type(module_t *m, type_t t) {
    assert(t.kind > 0);

    char *ident = t.ident;
    type_ident_kind ident_kind = t.ident_kind;
    list_t *args = t.args;
    bool in_heap = t.in_heap;

    if (t.kind == TYPE_UNKNOWN) {
        return t;
    }

    // 跳过已经完成 reduction 的 type
    if (t.status == REDUCTION_STATUS_DONE) {
        goto STATUS_DONE;
    }


    if (ident_is_def_or_alias(&t)) {
        t = reduction_typedef_or_alias(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_IDENT && t.ident_kind == TYPE_IDENT_INTERFACE) {
        // use interface
        t = reduction_typedef_or_alias(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_IDENT && t.ident_kind == TYPE_IDENT_PARAM) {
        if (!m->infer_type_args_stack) {
            goto STATUS_DONE;
        }

        table_t *arg_table = stack_top(m->infer_type_args_stack);
        assert(arg_table);
        t = type_param_special(m, t, arg_table);

        ident = t.ident;
        ident_kind = t.ident_kind;
        args = t.args;

        goto STATUS_DONE;
    }

    if (t.kind == TYPE_UNION) {
        t = reduction_union_type(m, t);
        goto STATUS_DONE;
    }

    if (t.kind == TYPE_INTERFACE) {
        t = reduction_interface_type(m, t);
        goto STATUS_DONE;
    }

    // 只有 typedef ident 才有中间状态的说法
    if (is_origin_type(t)) {
        t.status = REDUCTION_STATUS_DONE;
        t.ident = type_kind_str[t.kind];
        t.ident_kind = TYPE_IDENT_BUILTIN;
        t.args = NULL;
        goto STATUS_DONE;
    }

    // infer complex type
    if (is_complex_type(t)) {
        t = reduction_complex_type(m, t);
        goto STATUS_DONE;
    }

    INFER_ASSERTF(false, "cannot reduction type %s", type_format(t));
STATUS_DONE:
    t.status = REDUCTION_STATUS_DONE;
    t.in_heap = kind_in_heap(t.kind);
    if (in_heap == true) {
        t.in_heap = true;
    }

    // ident 继承, 而不是基于 builtin 生成
    if (ident_kind > 0) {
        t.ident_kind = ident_kind;
        t.ident = ident;
    }

    if (args) {
        t.args = args;
    }

    t.kind = cross_kind_trans(t.kind);

    // 计算 reflect type
    ct_reflect_type(t);
    return t;
}

static void infer_generics_param_constraints(module_t *m, type_t *impl_type, list_t *generics_params) {
    // 定位 type_alias
    assert(impl_type->kind == TYPE_IDENT);
    assert(impl_type->ident_kind == TYPE_IDENT_DEF);

    char *impl_ident = impl_type->ident;
    symbol_t *symbol = symbol_table_get(impl_ident);

    INFER_ASSERTF(symbol, "type alias '%s' not found", impl_ident);
    INFER_ASSERTF(symbol->type == SYMBOL_TYPE, "'%s' is not a type alias", symbol->ident);
    ast_typedef_stmt_t *ast_stmt = symbol->ast_value;
    list_t *params = ast_stmt->params;

    INFER_ASSERTF(params->length == generics_params->length, "type alias '%s' param not match", impl_ident);
    if (generics_params->length == 0) {
        return;
    }

    // generics_params 定义的约束应该包含在 impl_type 定义的 params 中
    for (int i = 0; i < params->length; ++i) {
        ast_generics_param_t *type_generics_param = ct_list_value(params, i);

        for (int j = 0; j < type_generics_param->constraints.elements->length; ++j) {
            type_t *constraint = ct_list_value(type_generics_param->constraints.elements, j);
            *constraint = reduction_type(m, *constraint);

            if (type_generics_param->constraints.elements->length == 1) {
                if (constraint->kind == TYPE_INTERFACE) {
                    type_generics_param->constraints.and = true;
                } else if (constraint->kind != TYPE_UNION) {// or 表示单个类型约束？那 union 呢？怎么 check, 怎么约束？
                    type_generics_param->constraints.or = true;
                }
            }

            if (type_generics_param->constraints.and) {
                // must interface
                INFER_ASSERTF(constraint->kind == TYPE_INTERFACE, "only type 'interface' can be combination with '&'");
            } else if (type_generics_param->constraints.or) {
                INFER_ASSERTF(constraint->kind != TYPE_UNION, "cannot use type 'union' combination")
                INFER_ASSERTF(constraint->kind != TYPE_INTERFACE, "cannot use type 'interface' combination")
            }
        }

        ast_generics_param_t *impl_generics_param = ct_list_value(generics_params, i);

        bool compare = generics_constraints_compare(&type_generics_param->constraints,
                                                    &impl_generics_param->constraints);
        INFER_ASSERTF(compare, "type alias '%s' param constraint not match", impl_ident);
    }
}

/**
 * 为了能够和 impl 中声明的 fn 进行 compare, 需要将 fn 将 self 参数暂时去除, 并且不改变 fndef 中的 ident/return_type/type 等
 */
static type_t infer_impl_fn_decl(module_t *m, ast_fndef_t *fndef) {
    assert(fndef->impl_type.kind > 0);

    // 对 fndef 进行类型还原
    type_fn_t *f = NEW(type_fn_t);
    f->fn_name = fndef->fn_name;
    f->is_tpl = fndef->is_tpl;
    f->is_errable = fndef->is_errable;
    f->param_types = ct_list_new(sizeof(type_t));
    f->return_type = reduction_type(m, fndef->return_type);

    // 跳过 self
    for (int i = 1; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);

        param->type = reduction_type(m, param->type);
        ct_list_push(f->param_types, &param->type);
    }

    f->is_rest = fndef->rest_param;
    type_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_DONE;

    return result;
}


/**
 * 对参数和返回值进行了类型推导，其中 self 对应的 struct 还没有 reduction 还没有处理完成
 * 所以无法对 self 进行 struct 还原。
 * @param m
 * @param fndef
 */
static type_t infer_fn_decl(module_t *m, ast_fndef_t *fndef, type_t target_type) {
    if (fndef->type.status == REDUCTION_STATUS_DONE) {
        // 在 pre_infer 虽然已经对所有的 fn 进行了类型推导，但是后续 infer_right_expr 再次调用时，原则上希望可以继承 type def ident
        // 也就是 literal fn decl 自动类型转换
        if (target_type.kind != TYPE_UNKNOWN) {
            fndef->type.ident = target_type.ident;// literal new ident 直接继承
            fndef->type.ident_kind = target_type.ident_kind;
            fndef->type.args = target_type.args;
        }
        return fndef->type;
    }

    // 对 fndef 进行类型还原
    type_fn_t *f = NEW(type_fn_t);

    f->fn_name = fndef->fn_name;
    f->is_tpl = fndef->is_tpl;
    f->is_errable = fndef->is_errable;
    f->param_types = ct_list_new(sizeof(type_t));
    f->return_type = reduction_type(m, fndef->return_type);

    fndef->return_type = f->return_type;

    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);

        param->type = reduction_type(m, param->type);

        // 为什么要在这里进行 ptr of, 只有在 infer 之后才能确定 alias 的具体类型，从而进一步判断是否需要 ptrof
        if (fndef->impl_type.kind > 0 && i == 0 && is_stack_impl(param->type.kind)) {
            // struct to ptr
            param->type = type_ptrof(param->type);
        }

        ct_list_push(f->param_types, &param->type);
    }

    f->is_rest = fndef->rest_param;
    type_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_DONE;

    if (target_type.kind != TYPE_UNKNOWN) {
        result.ident = target_type.ident;// literal new ident 直接继承
        result.ident_kind = target_type.ident_kind;
        result.args = target_type.args;
    }

    // 冗余一份，方便计算使用
    fndef->type = result;

    return result;
}

/**
 * 包含 body
 * @param m
 * @param fn
 */
static void infer_fndef(module_t *m, ast_fndef_t *fn) {
    assert(m);
    assert(fn->type.kind == TYPE_FN && fn->type.status == REDUCTION_STATUS_DONE);
    m->current_fn = fn;
    m->current_line = fn->line;
    m->current_column = fn->column;

    type_t t = fn->type;

    // rewrite_formals ident
    for (int i = 0; i < fn->params->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fn->params, i);
        rewrite_var_decl(m, var_decl);
    }

    if (fn->jit_closure_name) {
        symbol_t *symbol = symbol_table_get(fn->jit_closure_name);
        INFER_ASSERTF(symbol, "fn var ident %s not found", fn->jit_closure_name);
        INFER_ASSERTF(symbol->type == SYMBOL_VAR, "symbol type not expected");

        ast_var_decl_t *var_decl = symbol->ast_value;
        var_decl->type = t;
    }

    // env 表达式类型还原
    for (int i = 0; i < fn->capture_exprs->length; ++i) {
        ast_expr_t *env_expr = ct_list_value(fn->capture_exprs, i);
        infer_left_expr(m, env_expr);

        // 闭包引用变量全都在栈里面分配
        if (env_expr->assert_type == AST_EXPR_IDENT) {
            ast_ident *ident = env_expr->value;
            symbol_t *s = symbol_table_get(ident->literal);
            if (s->type == SYMBOL_VAR) {
                ast_var_decl_t *symbol_var = s->ast_value;
                symbol_var->type.in_heap = true;
            }
        }
    }

    // body infer
    if (fn->body) {
        infer_body(m, fn->body);
    }
}

/**
 * union type 才能进行 product
 */
void cartesian_product(list_t *generics_params, int depth, type_t **temp_product, slice_t *result) {
    if (depth == generics_params->length) {
        // 从新申请空间， copy 到 result 中
        table_t *arg_table = table_new();
        for (int i = 0; i < generics_params->length; ++i) {
            ast_generics_param_t *param = ct_list_value(generics_params, i);
            // 直接引用到了 constraints 中
            table_set(arg_table, param->ident, temp_product[i]);
        }

        slice_push(result, arg_table);
    } else {
        ast_generics_param_t *param = ct_list_value(generics_params, depth);
        for (int i = 0; i < param->constraints.elements->length; ++i) {
            type_t *type = ct_list_value(param->constraints.elements, i);
            assert(type->status == REDUCTION_STATUS_DONE);
            temp_product[depth] = type;
            cartesian_product(generics_params, depth + 1, temp_product, result);
        }
    }
}

/**
 * 排列组合计算 hash
 * @param generics_params
 * @return
 */
static slice_t *generics_constraints_product(module_t *m, type_t *impl_type, list_t *generics_params) {
    slice_t *hash_list = slice_new();

    // 要么全部是 any， 要么不能全部是 any, 避免复杂的匹配情况
    int any_count = 0;
    for (int i = 0; i < generics_params->length; ++i) {
        ast_generics_param_t *param = ct_list_value(generics_params, i);
        for (int j = 0; j < param->constraints.elements->length; ++j) {
            type_t *temp = ct_list_value(param->constraints.elements, j);
            *temp = reduction_type(m, *temp);

            if (param->constraints.elements->length == 1) {
                if (temp->kind == TYPE_INTERFACE) {
                    param->constraints.and = true;
                } else if (temp->kind != TYPE_UNION) {// or 表示单个类型约束？那 union 呢？怎么 check, 怎么约束？
                    param->constraints.or = true;
                }
            }


            if (param->constraints.and) {
                // must interface
                INFER_ASSERTF(temp->ident_kind == TYPE_IDENT_INTERFACE,
                              "only type 'interface' can be combination with '&'");
            } else if (param->constraints.or) {
                // cannot interface and union
                INFER_ASSERTF(temp->kind != TYPE_UNION, "cannot use type 'union' combination")
                INFER_ASSERTF(temp->kind != TYPE_INTERFACE, "cannot use type 'interface' combination")
            }
        }

        if (param->constraints.any) {
            return hash_list;// 存在 any 则无法进行具体数量的组合约束
        }

        if (param->constraints.and) {
            return hash_list;// and 表示这是 interface, 同样无法进行类型约束
        }
    }

    // 要么全都是 any 要么全都不是 any
    INFER_ASSERTF(any_count == 0 || any_count == generics_params->length,
                  "all generics params must have constraints or all none");

    if (ident_is_def_or_alias(impl_type)) {// type ident 才会存在 args
        infer_generics_param_constraints(m, impl_type, generics_params);
    }


    slice_t *product_list = slice_new();
    // 对 param 的 param->constraints 进行笛卡尔积计算, 也就是全量的排列组合, 比如 fn person_t<T:int|float, U:string> 的类型排列组合有  int+string 和 float+string 种可能
    void *current_product = mallocz(sizeof(void *) * generics_params->length);
    cartesian_product(generics_params, 0, current_product, product_list);

    SLICE_FOR(product_list) {
        table_t *arg_table = SLICE_VALUE(product_list);
        char *arg_hash = generics_args_hash(generics_params, arg_table);
        slice_push(hash_list, arg_hash);
    }

    return hash_list;
}


/**
 * 对左右的 fn def 中的 param 进行初步还原，这样 call gen fn 时才能正确的匹配类型
 * @param m
 */
void pre_infer(module_t *m) {
    // - Global variables also contain type information, which needs to be restored and derived
    for (int j = 0; j < m->global_vardef->count; ++j) {
        ast_vardef_stmt_t *vardef = m->global_vardef->take[j];

        infer_global_vardef(m, vardef);
    }

    // - 遍历所有 global fndef 进行处理
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        assert(!fndef->is_local);

        m->current_line = fndef->line;
        m->current_column = fndef->column;

        if (!fndef->is_generics) {
            // fndef 可能是，依旧不影响进行 reduction, type_param 跳过即可
            infer_fn_decl(m, fndef, type_kind_new(TYPE_UNKNOWN));

            // infer child
            for (int j = 0; j < fndef->local_children->count; ++j) {
                infer_fn_decl(m, fndef->local_children->take[j], type_kind_new(TYPE_UNKNOWN));
            }

            continue;
        }

        // generics 的 generic_param+limit
        assert(fndef->generics_params);

        // 对泛型约束进行排列组合进行生成 ast_fndef 并注册到符号表中 但不进行具体函数生成, 因为 all_t 可以生成无数类型的函数，
        // 仅仅通过 call 匹配出具体类型时才进行具体的函数类型生成, 所以仅仅是注册到符号表时的 key 有所不同
        // 如果泛型类型为 any， 则返回的 generics_args 为 null
        slice_t *generics_args = generics_constraints_product(m, &fndef->impl_type, fndef->generics_params);
        if (generics_args->count == 0) {
            // 基础名称覆盖注册
            symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, false);
        } else {
            // 基于类型约束进行泛型展开
            SLICE_FOR(generics_args) {
                char *arg_hash = SLICE_VALUE(generics_args);

                // 重新进行符号表注册, 不需要修改 fn->symbol_name, 此处只是用来协助匹配
                char *symbol_name = fndef->symbol_name;
                symbol_name = str_connect_by(symbol_name, arg_hash, GEN_REWRITE_SEPARATOR);
                symbol_t *s = symbol_table_set(symbol_name, SYMBOL_FN, fndef, false);
                ANALYZER_ASSERTF(s, "generics fn '%s' param constraint redeclared", fndef->fn_name);
            }
        }
    }
}

void infer(module_t *m) {
    m->current_fn = NULL;
    m->current_line = 0;
    m->current_column = 0;

    /**
     * gen fn 作为模版函数, 存在类型 T 等泛型，不能直接进入到 infer_worklist 中进行 infer
     * 当 call 时，gen fn  指定了具体类型，此时可以基于 is_tpl fn 生成 special fn，此时 special fn 可以推送到 fn->module->temp_worklist
     */
    linked_t *infer_worklist = linked_new();

    // infer_worklist
    // - 遍历所有 fndef 进行处理, 包含 global 和 local fn
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fn = m->ast_fndefs->take[i];
        if (fn->is_generics) {
            continue;
        }

        // 已经进行了 pre infer 了
        assert(fn->type.kind == TYPE_FN);

        linked_push(infer_worklist, fn);
    }

    m->ast_fndefs = slice_new();

    // 开始进行 infer, worklist 必须是当前的 infer_worklist
    // infer 过程中如果调用的函数是一个泛型的函数，则需要根据泛型参数进行泛型函数生成，此时的目标泛型函数可能是其他 module 的函数
    // 所以使用 fn-> 对应的 module 进行 infer，而不是使用固定的 module
    while (infer_worklist->count > 0) {
        ast_fndef_t *fn = linked_pop(infer_worklist);

        if (fn->generics_args_table) {
            stack_push(fn->module->infer_type_args_stack, fn->generics_args_table);
        }

        fn->module->temp_worklist = infer_worklist;// temp_worklist 和 infer_worklist 都是指针指向同一片数据，这也是 worklist 存在的意义
        infer_fndef(fn->module, fn);
        slice_push(m->ast_fndefs, fn);

        for (int j = 0; j < fn->local_children->count; ++j) {
            ast_fndef_t *child = fn->local_children->take[j];
            child->module->temp_worklist = infer_worklist;
            infer_fndef(child->module, child);

            slice_push(m->ast_fndefs, child);
        }

        if (fn->generics_args_table) {
            stack_pop(fn->module->infer_type_args_stack);
        }
    }
}
