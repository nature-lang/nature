#include <string.h>
#include <stdio.h>
#include "linear.h"
#include "src/debug/debug.h"

lir_opcode_t ast_op_convert[] = {
        [AST_OP_ADD] = LIR_OPCODE_ADD,
        [AST_OP_SUB] = LIR_OPCODE_SUB,
        [AST_OP_MUL] = LIR_OPCODE_MUL,
        [AST_OP_DIV] = LIR_OPCODE_DIV,
        [AST_OP_REM] = LIR_OPCODE_REM,

        [AST_OP_LSHIFT] = LIR_OPCODE_SHL,
        [AST_OP_RSHIFT] = LIR_OPCODE_SHR,
        [AST_OP_AND] = LIR_OPCODE_AND,
        [AST_OP_OR] = LIR_OPCODE_OR,
        [AST_OP_XOR] = LIR_OPCODE_XOR,

        [AST_OP_LT] = LIR_OPCODE_SLT,
        [AST_OP_LE] = LIR_OPCODE_SLE,
        [AST_OP_GT] = LIR_OPCODE_SGT,
        [AST_OP_GE] = LIR_OPCODE_SGE,
        [AST_OP_EE] = LIR_OPCODE_SEE,
        [AST_OP_NE] = LIR_OPCODE_SNE,

        [AST_OP_BNOT] = LIR_OPCODE_NOT,
        [AST_OP_NEG] = LIR_OPCODE_NEG,
};

static lir_operand_t *linear_zero_string(module_t *m, type_t t) {
    lir_operand_t *result = temp_var_operand(m, t);
    OP_PUSH(rt_call(RT_CALL_STRING_NEW, result, 2, string_operand(""), int_operand(0)));
    return result;
}


static lir_operand_t *linear_zero_list(module_t *m, type_t t) {
    lir_operand_t *result = temp_var_operand(m, t);
    lir_operand_t *rtype_hash = int_operand(ct_find_rtype_hash(t));
    lir_operand_t *element_index = int_operand(ct_find_rtype_hash(t.list->element_type));
    OP_PUSH(rt_call(RT_CALL_LIST_NEW, result, 4,
                    rtype_hash, element_index, int_operand(t.list->length), int_operand(0)));
    return result;
}

static lir_operand_t *linear_zero_map(module_t *m, type_t t) {
    uint64_t map_rtype_hash = ct_find_rtype_hash(t);
    uint64_t key_index = ct_find_rtype_hash(t.map->key_type);
    uint64_t value_index = ct_find_rtype_hash(t.map->value_type);

    lir_operand_t *result = temp_var_operand(m, t);
    lir_op_t *call_op = rt_call(RT_CALL_MAP_NEW, result,
                                3,
                                int_operand(map_rtype_hash),
                                int_operand(key_index),
                                int_operand(value_index));
    OP_PUSH(call_op);

    return result;
}

static lir_operand_t *linear_zero_set(module_t *m, type_t t) {
    uint64_t rtype_hash = ct_find_rtype_hash(t);
    uint64_t key_index = ct_find_rtype_hash(t.map->key_type);

    lir_operand_t *result = temp_var_operand(m, t);
    lir_op_t *call_op = rt_call(RT_CALL_SET_NEW, result,
                                2, int_operand(rtype_hash), int_operand(key_index));
    OP_PUSH(call_op);
    return result;
}

/**
 * throw 一个错误的 fn
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_zero_fn(module_t *m, type_t t) {
    lir_operand_t *result = temp_var_operand(m, t);
    lir_operand_t *zero_fn_operand = label_operand(RT_CALL_ZERO_FN, false);

    OP_PUSH(lir_op_lea(result, zero_fn_operand));
    return result;
}


/**
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_zero_struct(module_t *m, type_t t) {
    lir_operand_t *result = temp_var_operand(m, t);
    uint64_t rtype_hash = ct_find_rtype_hash(t);
    OP_PUSH(rt_call(RT_CALL_STRUCT_NEW, result, 1, int_operand(rtype_hash)));

    // 相关属性全都赋予 zero 值
    for (int i = 0; i < t.struct_->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t.struct_->properties, i);
        uint64_t offset = type_struct_offset(t.struct_, p->key);
        uint64_t item_size = type_sizeof(p->type);

        lir_operand_t *zero_operand = linear_zero_operand(m, p->type);
        lir_operand_t *zero_operand_ref = lea_operand_pointer(m, zero_operand);
        OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        5,
                        result,
                        int_operand(offset),
                        zero_operand_ref,
                        int_operand(0),
                        int_operand(item_size)));
    }

    return result;
}

/**
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_zero_tuple(module_t *m, type_t t) {
    lir_operand_t *result = temp_var_operand(m, t);
    uint64_t rtype_hash = ct_find_rtype_hash(t);
    OP_PUSH(rt_call(RT_CALL_TUPLE_NEW, result, 1, int_operand(rtype_hash)));

    uint64_t offset = 0;
    for (int i = 0; i < t.tuple->elements->length; ++i) {
        type_t *element_t = ct_list_value(t.tuple->elements, i);

        uint64_t item_size = type_sizeof(*element_t);
        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align((int64_t) offset, (int64_t) item_size);

        lir_operand_t *zero_operand = linear_zero_operand(m, *element_t);
        lir_operand_t *zero_operand_ref = lea_operand_pointer(m, zero_operand);
        OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        5,
                        result,
                        int_operand(offset),
                        zero_operand_ref,
                        int_operand(0),
                        int_operand(item_size)));
    }

    return result;
}

static lir_operand_t *linear_zero_operand(module_t *m, type_t t) {
    if (is_origin_type(t)) {
        lir_operand_t *result = temp_var_operand(m, t);
        OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, result));
        return result;
    }

    if (t.kind == TYPE_STRING) {
        return linear_zero_string(m, t);
    }

    if (t.kind == TYPE_LIST) {
        return linear_zero_list(m, t);
    }

    if (t.kind == TYPE_MAP) {
        return linear_zero_map(m, t);
    }

    if (t.kind == TYPE_SET) {
        return linear_zero_set(m, t);
    }

    if (t.kind == TYPE_FN) {
        return linear_zero_fn(m, t);
    }

    if (t.kind == TYPE_STRUCT) {
        return linear_zero_struct(m, t);
    }

    if (t.kind == TYPE_TUPLE) {
        return linear_zero_tuple(m, t);
    }

    assertf(1, "linear_zero_operand not support type=%s", type_kind_str[t.kind]);
    return NULL;
}

static lir_operand_t *global_fn_symbol(module_t *m, ast_expr_t expr) {
    if (expr.assert_type != AST_EXPR_IDENT) {
        return NULL;
    }

    ast_ident *ident = expr.value;
    symbol_t *s = symbol_table_get(ident->literal);
    assertf(s, "ident %s not declare");
    if (s->type != SYMBOL_FN) {
        return NULL;
    }
    return label_operand(ident->literal, s->is_local);
}

static void linear_error_handle(module_t *m) {
    char *error_target_label = m->linear_current->error_label;
    if (m->linear_current->catch_error_label) {
        error_target_label = m->linear_current->catch_error_label;
    }

    lir_operand_t *has_error = temp_var_operand(m, type_basic_new(TYPE_BOOL));
    OP_PUSH(rt_call(RT_CALL_PROCESSOR_HAS_ERRORT, has_error, 0));
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ,
                       bool_operand(true), has_error,
                       label_operand(error_target_label, true)));
}

static lir_operand_t *linear_temp_var_operand(module_t *m, type_t type) {
    assert(type.kind > 0);
    lir_operand_t *temp = temp_var_operand(m, type);
    OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, temp));
    return temp;
}

/**
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_ident(module_t *m, ast_expr_t expr) {
    ast_ident *ident = expr.value;
    symbol_t *s = symbol_table_get(ident->literal);
    assertf(s, "ident %s not declare");

    char *closure_name = m->linear_current->closure_name;
    if (closure_name && str_equal(s->ident, closure_name)) {
        // symbol 中的该符号已经改写成 closure var 了，该 closure var 通过 last param 丢了进来
        // 所以直接使用 fn 该 fn 就行了，该 fn 一定被赋值了，就放心好了
        assertf(s->type == SYMBOL_VAR, "closure symbol=%s not var");
        assertf(m->linear_current->fn_runtime_operand, "closure->fn_runtime_operand not init");
        lir_operand_t *operand = m->linear_current->fn_runtime_operand;
        lir_var_t *var = operand->value;
        assert(str_equal(var->ident, ident->literal));
    }

    if (s->type == SYMBOL_FN) {
        // 现在 symbol fn 是作为一个 type_nf 值进行传递，所以需要取出其 label 进行处理。
        // 即使是 global fn 也不例外, linear call symbol 已经进行了特殊处理，进不到这里来
        lir_operand_t *result = temp_var_operand(m, type_basic_new(TYPE_FN));
        OP_PUSH(lir_op_lea(result, symbol_label_operand(m, ident->literal)));
        return result;
    }

    if (s->type == SYMBOL_VAR) {
        ast_var_decl_t *var = s->ast_value;
        if (s->is_local) {
            return operand_new(LIR_OPERAND_VAR, lir_var_new(m, ident->literal));
        } else {
            lir_symbol_var_t *symbol = NEW(lir_symbol_var_t);
            symbol->ident = ident->literal;
            symbol->kind = var->type.kind;
            return operand_new(LIR_OPERAND_SYMBOL_VAR, symbol);
        }
    }
    assertf(false, "ident %s exception", ident);
    exit(1);
}

static void linear_list_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_list_access_t *list_access = stmt->left.value;
    lir_operand_t *list_target = linear_expr(m, list_access->left);
    lir_operand_t *index_target = linear_expr(m, list_access->index);

    // 取 value 栈指针,如果 value 不是 var， 会自动转换成 var
    lir_operand_t *value_ref = lea_operand_pointer(m, linear_expr(m, stmt->right));

    // mov $1, -4(%rbp) // 以 var 的形式入栈
    // mov -4(%rbp), rcx // 参数 1, move 将 -4(%rbp) 处的值穿递给了 rcx, 而不是 -4(%rbp) 这个栈地址
    OP_PUSH(rt_call(RT_CALL_LIST_ASSIGN, NULL,
                    3, list_target, index_target, value_ref));
}

static void linear_tuple_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_tuple_access_t *tuple_access = stmt->left.value;
    type_t tuple_type = tuple_access->left.type;
    lir_operand_t *tuple_target = linear_expr(m, tuple_access->left);

    uint64_t item_size = type_sizeof(tuple_access->element_type);
    uint64_t offset = type_tuple_offset(tuple_type.tuple, tuple_access->index);

    // 取 value 栈指针,如果 value 不是 var， 会自动转换成 var
    lir_operand_t *src_ref = lea_operand_pointer(m, linear_expr(m, stmt->right));

    OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                    5,
                    tuple_target, // dst
                    int_operand(offset), // dst offset
                    src_ref, // src
                    int_operand(0), // src offset
                    int_operand(item_size))); // size
}

/**
 * @param m
 * @param stmt
 */
static void linear_env_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_env_access_t *ast = stmt->left.value;
    lir_operand_t *index = int_operand(ast->index);

    lir_operand_t *src_ref = lea_operand_pointer(m, linear_expr(m, stmt->right));
    uint64_t size = type_sizeof(stmt->right.type);
    assertf(m->linear_current->fn_runtime_operand, "have env access, must have fn_runtime_operand");

    OP_PUSH(rt_call(RT_CALL_ENV_ASSIGN_REF, NULL, 4,
                    m->linear_current->fn_runtime_operand,
                    index, src_ref, int_operand(size)));
}


static void linear_map_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_map_access_t *map_access = stmt->left.value;
    lir_operand_t *map_target = linear_expr(m, map_access->left);
    lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, map_access->key));
    lir_operand_t *value_ref = lea_operand_pointer(m, linear_expr(m, stmt->right));
    lir_op_t *call_op = rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
    OP_PUSH(call_op);
}

static void linear_struct_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_struct_select_t *struct_access = stmt->left.value;
    type_t struct_type = struct_access->left.type;
    lir_operand_t *struct_target = linear_expr(m, struct_access->left);
    uint64_t offset = type_struct_offset(struct_type.struct_, struct_access->key);
    uint64_t item_size = type_sizeof(struct_access->property->type);

    lir_operand_t *src_ref = lea_operand_pointer(m, linear_expr(m, stmt->right));

    // move by item size
    OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                    5,
                    struct_target,
                    int_operand(offset),
                    src_ref,
                    int_operand(0),
                    int_operand(item_size)));
}

/**
 * ident = operand
 * @param c
 * @param stmt
 */
static void linear_ident_assign(module_t *m, ast_assign_stmt_t *stmt) {
    // 如果 left 是 var
    lir_operand_t *src = linear_expr(m, stmt->right);
    lir_operand_t *dst = linear_ident(m, stmt->left); // ident
    OP_PUSH(lir_op_move(dst, src));
}


//
//
/**
 * 将 tuple 按递归解析赋值给 tuple_destr 中声明的所有 var
 * 递归将导致优先从左侧进行展开, 需要注意的是，仅支持 left 表达式，且需要走 assign
 * @param m
 * @param destr
 * @param tuple_target
 */
static void linear_tuple_destr(module_t *m, ast_tuple_destr_t *destr, lir_operand_t *tuple_target) {
    uint64_t offset = 0;
    for (int i = 0; i < destr->elements->length; ++i) {
        ast_expr_t *element = ct_list_value(destr->elements, i);
        // tuple_operand 对应到当前 index 到值
        uint64_t item_size = type_sizeof(element->type);
        offset = align((int64_t) offset, (int64_t) item_size);

        lir_operand_t *temp = linear_temp_var_operand(m, element->type);
        lir_operand_t *dst_ref = lea_operand_pointer(m, temp);

        OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE,
                        NULL,
                        5,
                        dst_ref,
                        int_operand(0),
                        tuple_target,
                        int_operand(offset),
                        int_operand(item_size)));

        // temp 用与临时存储 tuple 中的值，下面则是真正的使用该临时值
        if (element->assert_type == AST_VAR_DECL) {
            // var_decl 独有
            ast_var_decl_t *var_decl = element->value;
            lir_operand_t *dst = var_operand(m, var_decl->ident);
            OP_PUSH(lir_op_move(dst, temp));

        } else if (can_assign(element->assert_type)) {
            assert(temp->assert_type == LIR_OPERAND_VAR);
            // element 是左值
            ast_assign_stmt_t *assign_stmt = NEW(ast_assign_stmt_t);
            assign_stmt->left = *element;
            // temp is ident， 把 ident 解析出来
            assign_stmt->right = *ast_ident_expr(((lir_var_t *) temp->value)->ident);
            linear_assign(m, assign_stmt);
        } else if (element->assert_type == AST_EXPR_TUPLE_DESTR) {
            linear_tuple_destr(m, element->value, temp);
        } else {
            assertf(false, "var tuple destr must var/tuple_destr");
        }
        offset += item_size;
    }
}

/**
 * (a, b, (c[0], d.b)) = operand
 * @param m
 * @param stmt
 */
static void linear_tuple_destr_stmt(module_t *m, ast_assign_stmt_t *stmt) {
    ast_tuple_destr_t *destr = stmt->left.value;
    lir_operand_t *tuple_target = linear_expr(m, stmt->right);
    linear_tuple_destr(m, destr, tuple_target);
}

/**
 * var (a, b, (c, d)) = operand
 * @param m
 * @param var_tuple_def
 * @return
 */
static void linear_var_tuple_def_stmt(module_t *m, ast_var_tuple_def_stmt_t *var_tuple_def) {
    // 理论上只需要不停的 move 就行了
    lir_operand_t *tuple_target = linear_expr(m, var_tuple_def->right);
    linear_tuple_destr(m, var_tuple_def->tuple_destr, tuple_target);
}

/**
 * 这里不包含如 var a = 1 这样的 assign
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
static void linear_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    lir_operand_t *src = linear_expr(m, stmt->right);
    lir_operand_t *dst = var_operand(m, stmt->var_decl.ident);

    OP_PUSH(lir_op_move(dst, src));
}

/**
 * a = 1 // left_target is lir_var_operand
 * a.b = 1 // left_target is lir_memory(base_address)
 * (a, b, (a.b, b[0])) = operand
 * @param c
 * @param stmt
 * @return
 */
static void linear_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_expr_t left = stmt->left;

    // map assign list[0] = 1
    if (left.assert_type == AST_EXPR_LIST_ACCESS) {

        return linear_list_assign(m, stmt);
    }

    // set assign m["a"] = 2
    if (left.assert_type == AST_EXPR_MAP_ACCESS) {
        return linear_map_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_ENV_ACCESS) {
        return linear_env_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_TUPLE_ACCESS) {
        return linear_tuple_assign(m, stmt);
    }

    // struct assign p.name = "wei"
    if (left.assert_type == AST_EXPR_STRUCT_SELECT) {
        return linear_struct_assign(m, stmt);
    }


    if (left.assert_type == AST_EXPR_TUPLE_DESTR) {
        return linear_tuple_destr_stmt(m, stmt);
    }

    // a = 1
    if (left.assert_type == AST_EXPR_IDENT) {
        return linear_ident_assign(m, stmt);
    }

    // set[0] = 1 x 不允许这么赋值，set 只能通过 add 来添加 key
    assertf(false, "dose not support assign to %d", left.assert_type);
}

/**
 * 类似这样仅做了声明没有立即赋值，这里进行空赋值,从而能够保障有内存空间分配.
 * int a;
 * float b;
 * @param c
 * @param var_decl
 * @return
 */
static lir_operand_t *linear_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    lir_operand_t *operand = var_operand(m, var_decl->ident);
    OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, operand));
    return operand;
}


/**
 * rt_call get count => count
 * for_iterator:
 *  cmp_goto count == 0 to end for
 *  rt_call get key => key
 *  rt_call get value => value // 可选
 *  ....
 *  sub count, 1 => count
 *  goto for:
 * end_for_iterator:
 * @param c
 * @param for_in_stmt
 * @return
 */
static void linear_for_iterator(module_t *m, ast_for_iterator_stmt_t *ast) {
    // map or list
    lir_operand_t *iterator_target = linear_expr(m, ast->iterate);

    uint64_t rtype_hash = ct_find_rtype_hash(ast->iterate.type);

    // cursor 初始值
    lir_operand_t *cursor_operand = custom_var_operand(m, type_basic_new(TYPE_INT), ITERATOR_CURSOR);
    OP_PUSH(lir_op_move(cursor_operand, int_operand(-1))); // cursor 初始值 = --

    // make label
    lir_op_t *for_start_label = lir_op_unique_label(m, FOR_ITERATOR_IDENT);
    lir_op_t *for_end_label = lir_op_unique_label(m, FOR_END_IDENT);

    stack_push(m->linear_current->for_start_labels, for_start_label->output);
    stack_push(m->linear_current->for_end_labels, for_end_label->output);

    // set label
    OP_PUSH(for_start_label);

    // key 和 value 需要进行一次初始化
    lir_operand_t *first_target = linear_var_decl(m, &ast->first);
    lir_operand_t *first_ref = lea_operand_pointer(m, first_target);

    // 单值遍历清空下, 对于 list 调用 next value,
    if (!ast->second && ast->iterate.type.kind == TYPE_LIST) {
        OP_PUSH(rt_call(
                RT_CALL_ITERATOR_NEXT_VALUE,
                cursor_operand,
                4,
                iterator_target,
                int_operand(rtype_hash),
                cursor_operand,
                first_ref));
    } else {
        OP_PUSH(rt_call(
                RT_CALL_ITERATOR_NEXT_KEY,
                cursor_operand,
                4,
                iterator_target,
                int_operand(rtype_hash),
                cursor_operand,
                first_ref));
    }

    // 基于 key 已经可以判断迭代是否还有了，下面的 next value 直接根据 cursor_operand 取值即可
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, int_operand(-1),
                       cursor_operand, lir_copy_label_operand(for_end_label->output)));

    // 添加 continue label
    OP_PUSH(lir_op_unique_label(m, FOR_CONTINUE_IDENT));

    // gen value
    if (ast->second) {
        lir_operand_t *value_target = linear_var_decl(m, ast->second);
        lir_operand_t *value_ref = lea_operand_pointer(m, value_target);

        OP_PUSH(rt_call(
                RT_CALL_ITERATOR_TAKE_VALUE, NULL, 4,
                iterator_target,
                int_operand(rtype_hash),
                cursor_operand, value_ref));

    }
    // block
    linear_body(m, ast->body);

    // goto for start
    OP_PUSH(lir_op_bal(for_start_label->output)); // 重新进行迭代的计算
    OP_PUSH(for_end_label);

    stack_pop(m->linear_current->for_start_labels);
    stack_pop(m->linear_current->for_end_labels);
}


/**
 *
 * @param c
 * @param ast
 */
static void linear_for_cond(module_t *m, ast_for_cond_stmt_t *ast) {
    lir_op_t *for_start = lir_op_unique_label(m, FOR_COND_IDENT);
    lir_operand_t *for_end_operand = label_operand(make_unique_ident(m, FOR_END_IDENT), true);
    stack_push(m->linear_current->for_start_labels, for_start->output);
    stack_push(m->linear_current->for_end_labels, for_end_operand);

    OP_PUSH(for_start);

    lir_operand_t *condition_target = linear_expr(m, ast->condition);
    lir_op_t *cmp_goto = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), condition_target, for_end_operand);

    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_unique_label(m, FOR_CONTINUE_IDENT));
    linear_body(m, ast->body);

    // bal => goto
    OP_PUSH(lir_op_bal(for_start->output));

    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));

    stack_pop(m->linear_current->for_start_labels);
    stack_pop(m->linear_current->for_end_labels);
}

static void linear_for_tradition(module_t *m, ast_for_tradition_stmt_t *ast) {
    // init
    linear_stmt(m, ast->init);

    lir_op_t *for_start = lir_op_unique_label(m, FOR_TRADITION_IDENT);
    lir_op_t *for_update = lir_op_unique_label(m, FOR_UPDATE_IDENT);
    lir_operand_t *for_end_operand = label_operand(make_unique_ident(m, FOR_END_IDENT), true);
    stack_push(m->linear_current->for_start_labels, for_update->output);
    stack_push(m->linear_current->for_end_labels, for_end_operand);

    // for_tradition
    OP_PUSH(for_start);

    // cond -> for_end
    lir_operand_t *cond_target = linear_expr(m, ast->cond);
    lir_op_t *cmp_goto = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), cond_target, for_end_operand);
    OP_PUSH(cmp_goto);

    // continue
    OP_PUSH(lir_op_unique_label(m, FOR_CONTINUE_IDENT));

    // block
    linear_body(m, ast->body);

    // update
    OP_PUSH(for_update);
    linear_stmt(m, ast->update);
    OP_PUSH(lir_op_bal(for_start->output));

    // label for_end
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));

    stack_pop(m->linear_current->for_start_labels);
    stack_pop(m->linear_current->for_end_labels);
}


static void linear_continue(module_t *m, ast_continue_t *stmt) {
    assert(m->linear_current->for_start_labels->count > 0);
    lir_operand_t *label = stack_pop(m->linear_current->for_start_labels);
    OP_PUSH(lir_op_bal(label));
}

static void linear_break(module_t *m, ast_break_t *stmt) {
    assert(m->linear_current->for_end_labels->count > 0);
    lir_operand_t *label = stack_pop(m->linear_current->for_end_labels);
    OP_PUSH(lir_op_bal(label));
}

static void linear_return(module_t *m, ast_return_stmt_t *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *src = linear_expr(m, *ast->expr);
        // return void_expr() 时, m->linear_current->return_operand 是 null
        if (m->linear_current->return_operand) {
            OP_PUSH(lir_op_move(m->linear_current->return_operand, src));
        }

        // 用来做可达分析
        OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL));
    }

    OP_PUSH(lir_op_bal(label_operand(m->linear_current->end_label, false)));
}

static void linear_if(module_t *m, ast_if_stmt_t *if_stmt) {
    // 编译 condition
    lir_operand_t *condition_target = linear_expr(m, if_stmt->condition);

    // 判断结果是否为 false, false 对应 else
    lir_operand_t *false_target = bool_operand(false);
    lir_operand_t *end_label_operand = label_operand(make_unique_ident(m, END_IF_IDENT), true);
    lir_operand_t *alternate_label_operand = label_operand(make_unique_ident(m, IF_ALTERNATE_IDENT), true);

    lir_op_t *cmp_goto;
    if (if_stmt->alternate->count == 0) {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(end_label_operand));
    } else {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(alternate_label_operand));
    }
    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_unique_label(m, IF_CONTINUE_IDENT));

    // 编译 consequent block
    linear_body(m, if_stmt->consequent);
    OP_PUSH(lir_op_bal(end_label_operand));

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, alternate_label_operand));
        linear_body(m, if_stmt->alternate);
    }

    // 追加 end_if 标签
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, end_label_operand));
}

/**
 * - 函数参数使用 param var 存储,按约定从左到右(code.result 为 param, code.first 为实参)
 * - code.operand 模仿 phi body 弄成列表的形式！
 * - 可能存在错误需要确认
 * @param c
 * @param expr
 * @return
 */
static lir_operand_t *linear_call(module_t *m, ast_expr_t expr) {
    ast_call_t *call = expr.value;

    // global ident call optimize to 'call symbol'
    lir_operand_t *base_target = global_fn_symbol(m, call->left);
    if (!base_target) {
        base_target = linear_expr(m, call->left);
    }

    lir_operand_t *return_target = NULL;

    slice_t *params = slice_new();
    type_fn_t *formal_fn = call->left.type.fn;
    assert(formal_fn);


    // TYPE_VOID 是否有返回值
    if (call->return_type.kind != TYPE_VOID) {
        return_target = temp_var_operand(m, call->return_type);
    }

    // call 所有的参数都丢到 params 变量中
    for (int i = 0; i < formal_fn->formal_types->length; ++i) {
        if (!formal_fn->rest || i < formal_fn->formal_types->length - 1) {
            ast_expr_t *actual_param = ct_list_value(call->actual_params, i);
            lir_operand_t *actual_param_operand = linear_expr(m, *actual_param);
            slice_push(params, actual_param_operand);
            continue;
        }

        type_t *rest_type = ct_list_value(formal_fn->formal_types, i);
        assertf(rest_type->kind == TYPE_LIST, "rest param must list type");

        // actual 剩余的所有参数进行 linear_expr 之后 都需要用一个数组收集起来，并写入到 target_operand 中
        lir_operand_t *rest_target = temp_var_operand(m, *rest_type);
        lir_operand_t *rtype_hash = int_operand(ct_find_rtype_hash(*rest_type));
        lir_operand_t *element_index = int_operand(ct_find_rtype_hash(rest_type->list->element_type));
        lir_operand_t *length = int_operand(0);
        lir_operand_t *capacity = int_operand(0);
        OP_PUSH(rt_call(RT_CALL_LIST_NEW, rest_target, 4, rtype_hash, element_index, length, capacity));

        for (int j = i; j < call->actual_params->length; ++j) {
            ast_expr_t *actual_param = ct_list_value(call->actual_params, j);
            lir_operand_t *rest_actual_param = linear_expr(m, *actual_param);

            // 将栈上的地址传递给 list 即可,不需要管栈中存储的值
            lir_operand_t *param_ref = lea_operand_pointer(m, rest_actual_param);
            OP_PUSH(rt_call(RT_CALL_LIST_PUSH, NULL, 2, rest_target, param_ref));
        }

        slice_push(params, rest_target);
        break;
    }

    // 使用一个 int_operand(0) 预留出 fn_runtime 所需的空间,这里不需要也不能判断出 target 是否有空间引用，所以统一预留
    // call 本身不需要做任何的调整
    slice_push(params, int_operand(0));


    // call base_target,params -> target
    lir_op_t *call_op = lir_op_new(LIR_OPCODE_CALL, base_target,
                                   operand_new(LIR_OPERAND_ACTUAL_PARAMS, params), return_target);

    // 触发 call 指令, 结果存储在 target 指令中
    OP_PUSH(call_op);

    // builtin call 不会抛出异常只是直接 panic， 所以不需要判断 has_error
    if (!is_builtin_call(formal_fn->name)) {
        linear_error_handle(m);
    }


    return return_target;
}


static lir_operand_t *linear_binary(module_t *m, ast_expr_t expr) {
    ast_binary_expr_t *binary_expr = expr.value;


    lir_operand_t *left_target = linear_expr(m, binary_expr->left);
    lir_operand_t *right_target = linear_expr(m, binary_expr->right);
    lir_operand_t *result_target = temp_var_operand(m, expr.type);
    lir_opcode_t operator = ast_op_convert[binary_expr->operator];

    if (binary_expr->left.type.kind == TYPE_STRING && binary_expr->right.type.kind == TYPE_STRING) {
        switch (operator) {
            case LIR_OPCODE_ADD: {
                OP_PUSH(rt_call(RT_CALL_STRING_CONCAT, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SEE: {
                OP_PUSH(rt_call(RT_CALL_STRING_EE, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SNE: {
                OP_PUSH(rt_call(RT_CALL_STRING_NE, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SGT: {
                OP_PUSH(rt_call(RT_CALL_STRING_GT, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SGE: {
                OP_PUSH(rt_call(RT_CALL_STRING_GE, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SLT: {
                OP_PUSH(rt_call(RT_CALL_STRING_LT, result_target, 2, left_target, right_target));
                break;
            }
            case LIR_OPCODE_SLE: {
                OP_PUSH(rt_call(RT_CALL_STRING_LE, result_target, 2, left_target, right_target));
                break;
            }
            default: {
                assertf(false, "not support string operator %d", ast_expr_op_str[binary_expr->operator]);
            }
        }

        return result_target;
    }


    OP_PUSH(lir_op_new(operator, left_target, right_target, result_target));

    return result_target;
}

/**
 * - (1 + 1)
 * NOT first_param => result_target
 * @param c
 * @param expr
 * @param result_target
 * @return
 */
static lir_operand_t *linear_unary(module_t *m, ast_expr_t expr) {
    ast_unary_expr_t *unary_expr = expr.value;
    lir_operand_t *target = temp_var_operand(m, expr.type);
    lir_operand_t *first = linear_expr(m, unary_expr->operand);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->operator == AST_OP_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        assertf(is_number(imm->kind), "only number can neg operate");
        if (imm->kind == TYPE_INT) {
            imm->int_value = 0 - imm->int_value;
        } else {
            imm->f64_value = 0 - imm->f64_value;
        }
        // move 操作即可
        OP_PUSH(lir_op_move(target, first));
        return target;
    }

    //
    if (unary_expr->operator == AST_OP_NOT) {
        assert(unary_expr->operand.type.kind == TYPE_BOOL);
        if (first->assert_type == LIR_OPERAND_IMM) {
            lir_imm_t *imm = first->value;
            imm->bool_value = !imm->bool_value;
            OP_PUSH(lir_op_move(target, first));
            return target;
        }

        // bool not to bit xor  !true = xor $1,true
        OP_PUSH(lir_op_new(LIR_OPCODE_XOR, first, bool_operand(true), target));
        return target;
    }

    // &var
    if (unary_expr->operator == AST_OP_LA) {
        return lea_operand_pointer(m, first);
    }


    // neg source -> target
    assertf(unary_expr->operator != AST_OP_IA, "not support IA op");
    lir_opcode_t type = ast_op_convert[unary_expr->operator];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    OP_PUSH(unary);

    return target;
}

/**
 * int a = list[0]
 * string s = list[1]
 */
static lir_operand_t *linear_list_access(module_t *m, ast_expr_t expr) {
    ast_list_access_t *ast = expr.value;

    lir_operand_t *list_target = linear_expr(m, ast->left);
    lir_operand_t *index_target = linear_expr(m, ast->index);

    lir_operand_t *result = linear_temp_var_operand(m, expr.type);
    // 读取 result 的指针地址，给到 access 进行写入
    lir_operand_t *result_ref = lea_operand_pointer(m, result);

    OP_PUSH(rt_call(RT_CALL_LIST_ACCESS, NULL,
                    3, list_target, index_target, result_ref));

    // 可能会存在数组越界的错误需要拦截处理
    linear_error_handle(m);

    return result;
}

/**
 * origin [1, foo, bar(), car.done]
 * call runtime.make_list => t1
 * move 1 => t1[0]
 * move foo => t1[1]
 * move bar() => t1[2]
 * move car.done => t1[3]
 * move t1 => target
 * @param c
 * @param new_list
 * @param target
 * @return
 */
static lir_operand_t *linear_list_new(module_t *m, ast_expr_t expr) {
    ast_list_new_t *ast = expr.value;

    lir_operand_t *list_target = linear_zero_list(m, expr.type);
    // 值初始化 assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_expr_t *item_expr = ct_list_value(ast->elements, i);
        lir_operand_t *value_ref = lea_operand_pointer(m, linear_expr(m, *item_expr));


        OP_PUSH(rt_call(RT_CALL_LIST_PUSH, NULL, 2, list_target, value_ref));
    }

    return list_target;
}

/**
 * 1. 根据 c->env_name 得到 base_target   call GET_ENV
 * var a = b + 3 // 其中 b 是外部环境变量,需要改写成 GET_ENV
 * b = 12 + c  // 类似这样对外部变量的重新赋值操作，此时 b 的访问直接改成了
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_env_access(module_t *m, ast_expr_t expr) {
    ast_env_access_t *ast = expr.value;
    lir_operand_t *index = int_operand(ast->index);
    lir_operand_t *result = linear_temp_var_operand(m, expr.type);
    lir_operand_t *dst_ref = lea_operand_pointer(m, result);

    uint64_t size = type_sizeof(expr.type);
    assertf(m->linear_current->fn_runtime_operand, "have env access, must have fn_runtime_operand");

    OP_PUSH(rt_call(RT_CALL_ENV_ACCESS_REF, NULL,
                    4,
                    m->linear_current->fn_runtime_operand,
                    index,
                    dst_ref,
                    int_operand(size)));

    return result;
}

/**
 * foo.bar
 * foo[0].bar
 * foo.bar.car
 * 证明非变量
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_map_access(module_t *m, ast_expr_t expr) {
    ast_map_access_t *ast = expr.value;


    // linear base address left_target
    lir_operand_t *map_target = linear_expr(m, ast->left);
    type_t type_map_decl = ast->left.type;

    // linear key to temp var
    lir_operand_t *key_target_ref = lea_operand_pointer(m, linear_expr(m, ast->key));
    lir_operand_t *value_target = linear_temp_var_operand(m, type_map_decl.map->value_type);
    lir_operand_t *value_target_ref = lea_operand_pointer(m, value_target);

    // runtime get slot by temp var runtime.map_offset(base, "key")
    lir_op_t *call_op = rt_call(RT_CALL_MAP_ACCESS, NULL,
                                3, map_target, key_target_ref, value_target_ref);
    OP_PUSH(call_op);

    return value_target;
}


/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *linear_set_new(module_t *m, ast_expr_t expr) {
    ast_set_new_t *ast = expr.value;
    type_t t = expr.type;

    lir_operand_t *set_target = linear_zero_set(m, t);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_map_element_t *element = ct_list_value(ast->elements, i);
        ast_expr_t key_expr = element->key;
        lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, key_expr));

        OP_PUSH(rt_call(RT_CALL_SET_ADD, NULL, 2, set_target, key_ref));
    }

    return set_target;
}

/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *linear_map_new(module_t *m, ast_expr_t expr) {
    ast_map_new_t *ast = expr.value;
    type_t map_type = expr.type;

    lir_operand_t *map_target = linear_zero_map(m, map_type);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_map_element_t *element = ct_list_value(ast->elements, i);
        ast_expr_t key_expr = element->key;
        ast_expr_t value_expr = element->value;
        lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, key_expr));
        lir_operand_t *value_ref = lea_operand_pointer(m, linear_expr(m, value_expr));

        lir_op_t *call_op = rt_call(RT_CALL_MAP_ASSIGN, NULL, 3, map_target, key_ref, value_ref);
        OP_PUSH(call_op);
    }

    return map_target;
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_struct_select(module_t *m, ast_expr_t expr) {
    ast_struct_select_t *ast = expr.value;

    lir_operand_t *struct_target = linear_expr(m, ast->left);
    type_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->property->type);
    uint64_t offset = type_struct_offset(t.struct_, ast->key);

    lir_operand_t *dst = linear_temp_var_operand(m, ast->property->type);
    lir_operand_t *dst_ref = lea_operand_pointer(m, dst);

    OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                    5,
                    dst_ref,
                    int_operand(0),
                    struct_target,
                    int_operand(offset),
                    int_operand(item_size)));

    return dst;
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_tuple_access(module_t *m, ast_expr_t expr) {
    ast_tuple_access_t *ast = expr.value;

    lir_operand_t *tuple_target = linear_expr(m, ast->left);
    type_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->element_type);
    uint64_t offset = type_tuple_offset(t.tuple, ast->index);

    lir_operand_t *dst = linear_temp_var_operand(m, ast->element_type);
    lir_operand_t *dst_ref = lea_operand_pointer(m, dst);
    OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                    5,
                    dst_ref,
                    int_operand(0),
                    tuple_target,
                    int_operand(offset),
                    int_operand(item_size)));

    return dst;
}

/**
 * foo.bar = 1
 *
 * person baz = person {
 *  age = 100
 *  sex = true
 * }
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_struct_new(module_t *m, ast_expr_t expr) {
    ast_struct_new_t *ast = expr.value;
    type_t type = ast->type;

    lir_operand_t *struct_target = linear_zero_struct(m, type);

    // 快速赋值,由于 struct 的相关属性都存储在 type 中，所以偏移量等值都需要在前端完成计算
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *p = ct_list_value(ast->properties, i);

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t offset = type_struct_offset(type.struct_, p->key);
        uint64_t item_size = type_sizeof(p->type);

        assertf(p->right, "struct new property_expr value empty");

        ast_expr_t *property_expr = p->right;
        lir_operand_t *property_target = linear_expr(m, *property_expr);
        lir_operand_t *src_ref = lea_operand_pointer(m, property_target);

        // move by item size
        OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        5,
                        struct_target,
                        int_operand(offset),
                        src_ref,
                        int_operand(0),
                        int_operand(item_size)));
    }

    return struct_target;
}


/**
 * var a = (1, a, 1.25)
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_tuple_new(module_t *m, ast_expr_t expr) {
    ast_tuple_new_t *ast = expr.value;

    // tuple new 时所有的值都必须进行初始化，所以不会出现 null 值
    uint64_t rtype_hash = ct_find_rtype_hash(expr.type);
    lir_operand_t *tuple_target = temp_var_operand(m, expr.type);
    OP_PUSH(rt_call(RT_CALL_TUPLE_NEW, tuple_target, 1, int_operand(rtype_hash)));

//    lir_operand_t *tuple_target = linear_zero_tuple(m, expr.type);

    uint64_t offset = 0;
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_expr_t *element = ct_list_value(ast->elements, i);

        uint64_t item_size = type_sizeof(element->type);
        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align((int64_t) offset, (int64_t) item_size);

        // tuple_target 中包含到是一个执行堆区到地址，直接将该堆区到地址丢给 memory_move 即可
        // offset(var) var must assign reg
        lir_operand_t *src_ref = lea_operand_pointer(m, linear_expr(m, *element));

        // move by item size
        OP_PUSH(rt_call(RT_CALL_MEMORY_MOVE, NULL,
                        5,
                        tuple_target,
                        int_operand(offset),
                        src_ref,
                        int_operand(0),
                        int_operand(item_size)));
        offset += item_size;
    }

    return tuple_target;
}

static lir_operand_t *linear_is_expr(module_t *m, ast_expr_t expr) {
    ast_is_expr_t *is_expr = expr.value;
    assert(is_expr->src_operand.type.kind == TYPE_UNION);
    lir_operand_t *operand = linear_expr(m, is_expr->src_operand);
    uint64_t target_rtype_hash = ct_find_rtype_hash(is_expr->target_type);
    lir_operand_t *output = temp_var_operand(m, type_basic_new(TYPE_BOOL));
    OP_PUSH(rt_call(RT_CALL_UNION_IS, output, 2, operand, int_operand(target_rtype_hash)));

    return output;
}

/**
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_as_expr(module_t *m, ast_expr_t expr) {
    ast_as_expr_t *as_expr = expr.value;
    lir_operand_t *input = linear_expr(m, as_expr->src_operand);
    uint64_t input_rtype_hash = ct_find_rtype_hash(as_expr->src_operand.type);

    // 数值类型转换
    if (is_number(as_expr->target_type.kind) && is_number(as_expr->src_operand.type.kind)) {
        lir_operand_t *output = linear_temp_var_operand(m, as_expr->target_type);
        lir_operand_t *output_rtype = int_operand(ct_find_rtype_hash(as_expr->target_type));
        lir_operand_t *output_ref = lea_operand_pointer(m, output);
        lir_operand_t *input_ref = lea_operand_pointer(m, input);

        OP_PUSH(rt_call(RT_CALL_NUMBER_CASTING, NULL, 4,
                        int_operand(input_rtype_hash), input_ref, output_rtype, output_ref));
        return output;
    }


    // bool 类型转换
    if (as_expr->target_type.kind == TYPE_BOOL) {
        lir_operand_t *output = temp_var_operand(m, as_expr->target_type);
        OP_PUSH(rt_call(RT_CALL_BOOL_CASTING, output, 2, int_operand(input_rtype_hash), input));
        return output;
    }

    // single type to union type
    if (as_expr->target_type.kind == TYPE_UNION) {
        assert(as_expr->src_operand.type.kind != TYPE_UNION);
        lir_operand_t *output = temp_var_operand(m, as_expr->target_type);
        lir_operand_t *input_ref = lea_operand_pointer(m, input);
        OP_PUSH(rt_call(RT_CALL_UNION_CASTING, output, 2, int_operand(input_rtype_hash), input_ref));
        return output;
    }

    // union assert
    if (as_expr->src_operand.type.kind == TYPE_UNION) {
        assert(as_expr->target_type.kind != TYPE_UNION);
        // 需要先预留好空间等待值 copy
        lir_operand_t *output = linear_temp_var_operand(m, as_expr->target_type);
        lir_operand_t *output_ref = lea_operand_pointer(m, output);
        uint64_t target_rtype_hash = ct_find_rtype_hash(as_expr->target_type);
        OP_PUSH(rt_call(RT_CALL_UNION_ASSERT, NULL, 3, input, int_operand(target_rtype_hash), output_ref));
        linear_error_handle(m);
        return output;
    }

    // string -> list u8
    if (as_expr->src_operand.type.kind == TYPE_STRING && is_list_u8(as_expr->target_type)) {
        lir_operand_t *output = linear_temp_var_operand(m, as_expr->target_type);
        OP_PUSH(rt_call(RT_CALL_STRING_TO_LIST, output, 1, input));
        return output;
    }

    // list u8 -> string
    if (is_list_u8(as_expr->src_operand.type) && as_expr->target_type.kind == TYPE_STRING) {
        lir_operand_t *output = linear_temp_var_operand(m, as_expr->target_type);
        OP_PUSH(rt_call(RT_CALL_LIST_TO_STRING, output, 1, input));
        return output;
    }

    // anybody to cptr
    if (as_expr->target_type.kind == TYPE_CPTR) {
        lir_operand_t *output = linear_temp_var_operand(m, as_expr->target_type);
        OP_PUSH(rt_call(RT_CALL_CPTR_CASTING, output, 1, input));
        return output;
    }

    assertf(false, "not support as_expr to type %s", type_kind_str[as_expr->target_type.kind]);
    exit(1);
}

static lir_operand_t *linear_try(module_t *m, ast_expr_t expr) {
    ast_try_t *try = expr.value;

    // 包含 catch 则右侧表达式遇到错误时应该跳转到 catch_error_label
    char *catch_error_label = make_unique_ident(m, CATCH_ERROR_IDENT);
    m->linear_current->catch_error_label = catch_error_label;
    lir_op_t *catch_end_label = lir_op_unique_label(m, CATCH_END_IDENT);

    symbol_t *s = symbol_table_get(ERRORT_TYPE_ALIAS);
    assert(s);

    ast_type_alias_stmt_t *type_alias_stmt = s->ast_value;
    assertf(type_alias_stmt->type.status == REDUCTION_STATUS_DONE, "errort type not reduction");

    lir_operand_t *result_operand = linear_expr(m, try->expr);
    m->linear_current->catch_error_label = NULL; // 表达式已经编译完成，可以清理标记位了

    // bal -> catch_end
    OP_PUSH(lir_op_bal(catch_end_label->output));

    // catch_error_label: ------------------------------------------------------------------------------------------------------
    OP_PUSH(lir_op_label(catch_error_label, true));

    if (try->expr.type.kind != TYPE_VOID) {
        // result_operand 此时是 null，但是 nature 不允许 null 值，所以需要赋予 0 值
        lir_operand_t *zero_operand = linear_zero_operand(m, try->expr.type);
        OP_PUSH(lir_op_move(result_operand, zero_operand));
    }

    // catch_end_label: ------------------------------------------------------------------------------------------------------
    OP_PUSH(catch_end_label);
    // errort_operand 可能为空的 struct 或者非空
    lir_operand_t *errort_operand = temp_var_operand(m, type_alias_stmt->type);
    OP_PUSH(rt_call(RT_CALL_PROCESSOR_REMOVE_ERRORT, errort_operand, 0));
    if (try->expr.type.kind == TYPE_VOID) {
        return errort_operand;
    }

    // result label 和 error label 此时都是非 null 的值，将他们写入到 tuple 中即可
    // make tuple target return
    assertf(result_operand->assert_type == LIR_OPERAND_VAR, "linear expr result operand must lir var");
    lir_var_t *var = result_operand->value;
    ast_expr_t *result_expr = ast_ident_expr(var->ident);
    result_expr->type = var->type;

    // temp error ident
    ast_expr_t *err_expr = ast_ident_expr(((lir_var_t *) errort_operand->value)->ident);
    err_expr->type = ((lir_var_t *) errort_operand->value)->type;

    // (call(), error_operand())
    ast_tuple_new_t *tuple = NEW(ast_tuple_new_t);
    tuple->elements = ct_list_new(sizeof(ast_expr_t));
    ct_list_push(tuple->elements, result_expr);
    ct_list_push(tuple->elements, err_expr);

    return linear_tuple_new(m, (ast_expr_t) {
            .type = expr.type,
            .assert_type = AST_EXPR_TUPLE_NEW,
            .value = tuple
    });
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *linear_literal(module_t *m, ast_expr_t expr) {
    ast_literal_t *literal = expr.value;
    literal->kind = cross_kind_trans(literal->kind);

    if (literal->kind == TYPE_STRING) {
        // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
        lir_operand_t *target = temp_var_operand(m, expr.type);
        lir_operand_t *imm_c_string_operand = string_operand(literal->value);
        lir_operand_t *imm_len_operand = int_operand(strlen(literal->value));
        lir_op_t *call_op = rt_call(RT_CALL_STRING_NEW, target, 2, imm_c_string_operand, imm_len_operand);
        OP_PUSH(call_op);
        return target;
    }

    if (literal->kind == TYPE_STRING) {
        return string_operand(literal->value);
    }

    if (literal->kind == TYPE_BOOL) {
        bool bool_value = false;
        if (strcmp(literal->value, "true") == 0) {
            bool_value = true;
        }

        return bool_operand(bool_value);
    }

    if (literal->kind == TYPE_NULL) {
        return int_operand(0);
    }

    if (is_integer(literal->kind)) {
        char *convert_endptr;
        int64_t i = strtoll(literal->value, &convert_endptr, 0);
        if (*convert_endptr != '\0') {
            assertf(false, "covert '%s' to number failed in %s", literal->value, convert_endptr);
        }

        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
        imm_operand->int_value = i;
        lir_operand_t *operand = NEW(lir_operand_t);
        operand->assert_type = LIR_OPERAND_IMM;
        operand->value = imm_operand;
        return operand;
    }

    if (literal->kind == TYPE_FLOAT32) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
        imm_operand->f32_value = (float) atof(literal->value);
        lir_operand_t *operand = NEW(lir_operand_t);
        operand->assert_type = LIR_OPERAND_IMM;
        operand->value = imm_operand;
        return operand;
    }

    if (literal->kind == TYPE_FLOAT64) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
        imm_operand->f64_value = atof(literal->value);
        lir_operand_t *operand = NEW(lir_operand_t);
        operand->assert_type = LIR_OPERAND_IMM;
        operand->value = imm_operand;
        return operand;
    }

    assertf(0, "cannot linear literal, kind=%s", type_kind_str[literal->kind]);
    exit(1);
}


/**
 * fndef 到 body 已经编译完成并变成了 label, 此时不需要再递归到 fn body 内部,也不需要调整 m->linear_current
 * 只需要将 fndef 到 env 写入到 fndef->name 对应到 envs 中即可, 返回值则返回函数到唯一 ident 即可
 *
 * fn_decl 允许在 stmt 或者 expr 中, 但是无论是在哪里声明，当前函数都可能会有两个 ident 需要处理
 * 1. fndef->closure_name，该 ident 作为一个 var 编译，其中存储了 runtime_fn_new
 * 2. fndef->symbol_name, 该 ident 作为一个 symbol fn 符号进行编译, 仅当 fndef->closure_name 为空时使用。
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_fn_decl(module_t *m, ast_expr_t expr) {
    // var a = fn() {} 类似此时的右值就是 fndef, 此时可以为 fn 创建对应的 closure 了
    ast_fndef_t *fndef = expr.value;

    // symbol label 不能使用 mov 在变量间自由的传递，所以这里将 symbol label 的 addr 加载出来返回
    lir_operand_t *fn_symbol_operand = symbol_label_operand(m, fndef->symbol_name);
    lir_operand_t *label_addr_operand = temp_var_operand(m, fndef->type);

    if (!fndef->closure_name) {
        if (!expr.target_type.kind) {
            return NULL; // 没有表达式需要接收值
        }

        OP_PUSH(lir_op_lea(label_addr_operand, fn_symbol_operand));
        return label_addr_operand;
    }
    assert(!str_equal(fndef->closure_name, ""));

    // 函数引用了外部的环境变量，所以需要编译成一个闭包
    // make envs
    lir_operand_t *length = int_operand(fndef->capture_exprs->length);
    // rt_call env_new(fndef->name, length)
    lir_operand_t *env_operand = temp_var_operand(m, type_basic_new(TYPE_INT64));
    OP_PUSH(rt_call(RT_CALL_ENV_NEW, env_operand, 1, length));

    slice_t *capture_vars = slice_new();
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr_t *item = ct_list_value(fndef->capture_exprs, i);
        // fndef 引用了当前环境的一些 ident, 需要在 ssa 中进行跟踪
        if (item->assert_type == AST_EXPR_IDENT) {
            char *ident = ((ast_ident *) item->value)->literal;
            slice_push(capture_vars, lir_var_new(m, ident));
        }

        //  加载 free var 在栈上的指针
        lir_operand_t *stack_addr_ref = lea_operand_pointer(m, linear_expr(m, *item));
        // rt_call env_assign(fndef->name, index_operand lir_operand)
        OP_PUSH(rt_call(RT_CALL_ENV_ASSIGN, NULL, 4,
                        env_operand,
                        int_operand(ct_reflect_type(item->type).hash),
                        int_operand(i),
                        stack_addr_ref));
    }

    // 记录引用关系, ssa 将会实时调整这些地方到值，一旦 ssa 完成这些 var 就有了唯一名称
    if (capture_vars->count > 0) {
        lir_operand_t *capture_operand = operand_new(LIR_OPERAND_VARS, capture_vars);
        OP_PUSH(lir_op_new(LIR_OPCODE_ENV_CAPTURE, capture_operand, NULL, NULL));
    }

    OP_PUSH(lir_op_lea(label_addr_operand, fn_symbol_operand));
    lir_operand_t *result = var_operand(m, fndef->closure_name);
    OP_PUSH(rt_call(RT_CALL_FN_NEW, result, 2, label_addr_operand, env_operand));

    return result;
}

static void linear_throw(module_t *m, ast_throw_stmt_t *stmt) {
    // msg to errort
    symbol_t *symbol = symbol_table_get(ERRORT_TYPE_ALIAS);

    assert(stmt->error.type.kind == TYPE_STRING);
    lir_operand_t *msg_operand = linear_expr(m, stmt->error);

    // attach errort to processor
    OP_PUSH(rt_call(RT_CALL_PROCESSOR_ATTACH_ERRORT, NULL, 1, msg_operand));

    // 插入 return 标识(用来做 return check 的，check 完会清除的)
    OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL));

    // ret
    OP_PUSH(lir_op_bal(label_operand(m->linear_current->end_label, false)));
}

static void linear_stmt(module_t *m, ast_stmt_t *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            linear_var_decl(m, stmt->value);
            return;
        }
        case AST_STMT_VARDEF: {
            return linear_vardef(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return linear_assign(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return linear_var_tuple_def_stmt(m, stmt->value);
        }
        case AST_STMT_IF: {
            return linear_if(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return linear_for_iterator(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return linear_for_cond(m, stmt->value);
        }
        case AST_STMT_BREAK: {
            return linear_break(m, stmt->value);
        }
        case AST_STMT_CONTINUE: {
            return linear_continue(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return linear_for_tradition(m, stmt->value);
        }
        case AST_FNDEF: {
            linear_fn_decl(m, (ast_expr_t) {
                    .line = stmt->line,
                    .assert_type = stmt->assert_type,
                    .value = stmt->value,
                    .target_type = NULL
            });
            return;
        }
        case AST_CALL: {
            ast_fndef_t *fndef = stmt->value;
            // stmt 中都 call 都是没有返回值的
            linear_call(m, (ast_expr_t) {
                    .line = stmt->line,
                    .assert_type = stmt->assert_type,
                    .type = type_basic_new(TYPE_FN),
                    .value = fndef,
            });
            return;
        }
        case AST_STMT_RETURN: {
            return linear_return(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return linear_throw(m, stmt->value);
        }
        case AST_STMT_TYPE_ALIAS: {
            return;
        }
        default: {
            assertf(false, "unknown stmt type=%d", stmt->assert_type);
        }
    }
}

linear_expr_fn expr_fn_table[] = {
        [AST_EXPR_LITERAL] = linear_literal,
        [AST_EXPR_IDENT] = linear_ident,
        [AST_EXPR_ENV_ACCESS] = linear_env_access,
        [AST_EXPR_BINARY] = linear_binary,
        [AST_EXPR_UNARY] = linear_unary,
        [AST_EXPR_LIST_NEW] = linear_list_new,
        [AST_EXPR_LIST_ACCESS] = linear_list_access,
        [AST_EXPR_MAP_NEW] = linear_map_new,
        [AST_EXPR_MAP_ACCESS] = linear_map_access,
        [AST_EXPR_STRUCT_NEW] = linear_struct_new,
        [AST_EXPR_STRUCT_SELECT] = linear_struct_select,
        [AST_EXPR_TUPLE_NEW] = linear_tuple_new,
        [AST_EXPR_TUPLE_ACCESS] = linear_tuple_access,
        [AST_EXPR_SET_NEW] = linear_set_new,
        [AST_CALL] = linear_call,
        [AST_FNDEF] = linear_fn_decl,
        [AST_EXPR_TRY] = linear_try,
        [AST_EXPR_AS] = linear_as_expr,
        [AST_EXPR_IS] = linear_is_expr,
};


static lir_operand_t *linear_expr(module_t *m, ast_expr_t expr) {
    // 特殊处理
    linear_expr_fn fn = expr_fn_table[expr.assert_type];
    assertf(fn, "ast right not support");

    lir_operand_t *operand = fn(m, expr);

    return operand;
}


static void linear_body(module_t *m, slice_t *body) {
    for (int i = 0; i < body->count; ++i) {
        ast_stmt_t *stmt = body->take[i];
        m->linear_line = stmt->line;
#ifdef DEBUG_linear
        debug_stmt("linear", *stmt);
#endif
        linear_stmt(m, stmt);
    }
}


/**
 * 这里主要编译 fn param 和 body, 不编译名称与 env
 * @param m
 * @param fndef
 * @return
 */
static closure_t *linear_fndef(module_t *m, ast_fndef_t *fndef) {
    // 创建 closure, 并写入到 m module 中
    closure_t *c = lir_closure_new(fndef);
    // 互相关联关系
    m->linear_current = c;
    c->module = m;
    c->end_label = str_connect("end_", c->symbol_name);
    c->error_label = str_connect("error_", c->symbol_name);

    // label name 使用 symbol_name!
    OP_PUSH(lir_op_label(fndef->symbol_name, false));


    // 编译 fn param -> lir_var_t*
    slice_t *params = slice_new();
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->formals, i);

        slice_push(params, lir_var_new(m, var_decl->ident));
    }

    // 和 linear_fndef 不同，linear_closure 是函数内部的空间中,添加的也是当前 fn 的形式参数
    // 当前 fn 的形式参数在 body 中都是可以随意调用的
    //if 包含 envs 则使用 custom_var_operand 注册一个临时变量，并加入到 LIR_OPCODE_FN_BEGIN 中
    if (fndef->closure_name) {
        // 直接使用 fn->closure_name 作为 runtime name?
        lir_operand_t *fn_runtime_operand = var_operand(m, fndef->closure_name);
        slice_push(params, fn_runtime_operand->value);
        c->fn_runtime_operand = fn_runtime_operand;
    }

    OP_PUSH(lir_op_result(LIR_OPCODE_FN_BEGIN, operand_new(LIR_OPERAND_FORMAL_PARAMS, params)));

    // 返回值处理
    if (fndef->return_type.kind != TYPE_VOID) {
        c->return_operand = custom_var_operand(m, fndef->return_type, "$result");
        // 初始化空值, 让 use-def 关系完整，避免 ssa 生成异常
        OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, c->return_operand));
    }

    linear_body(m, fndef->body);

    // bal end_label
    OP_PUSH(lir_op_bal(label_operand(c->end_label, true)));

    OP_PUSH(lir_op_label(c->error_label, true));
    OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL)); // 方便 return check
    // TODO error handle... example with stack
    OP_PUSH(lir_op_bal(label_operand(c->end_label, true)));


    OP_PUSH(lir_op_label(c->end_label, true));
    if (fndef->be_capture_locals->count > 0) {
        lir_operand_t *capture_operand = operand_new(LIR_OPERAND_CLOSURE_VARS, slice_new());
        lir_op_t *op = lir_op_new(LIR_OPCODE_ENV_CLOSURE, capture_operand, NULL, NULL);
        OP_PUSH(op);
        c->closure_vars = op->first->value;
        c->closure_var_table = table_new();
    }

    // lower 的时候需要进行特殊的处理
    OP_PUSH(lir_op_new(LIR_OPCODE_FN_END, c->return_operand, NULL, NULL));

    return c;
}

/**
 * @param c
 * @param ast
 * @return
 */
void linear(module_t *m) {
    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        closure_t *closure = linear_fndef(m, fndef);
        slice_push(m->closures, closure);
    }
}
