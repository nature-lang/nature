#include "linear.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "src/debug/debug.h"
#include "src/error.h"
#include "utils/linked.h"

lir_opcode_t ast_op_convert[] = {
        [AST_OP_ADD] = LIR_OPCODE_ADD,
        [AST_OP_SUB] = LIR_OPCODE_SUB,
        [AST_OP_MUL] = LIR_OPCODE_MUL, // 默认有符号
        [AST_OP_DIV] = LIR_OPCODE_SDIV,
        [AST_OP_REM] = LIR_OPCODE_SREM,

        [AST_OP_LSHIFT] = LIR_OPCODE_USHL,
        [AST_OP_RSHIFT] = LIR_OPCODE_SSHR, // 默认进行有符号，linear 进行调整
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

lir_opcode_t ast_op_to_for_continue[] = {
        [AST_OP_LT] = LIR_OPCODE_BLT,
        [AST_OP_LE] = LIR_OPCODE_BLE,
        [AST_OP_GT] = LIR_OPCODE_BGT,
        [AST_OP_GE] = LIR_OPCODE_BGE,
        [AST_OP_EE] = LIR_OPCODE_BEE,
        [AST_OP_NE] = LIR_OPCODE_BNE,
};

lir_opcode_t ast_op_to_if_alert[] = {
        [AST_OP_LT] = LIR_OPCODE_BGE,
        [AST_OP_LE] = LIR_OPCODE_BGT,
        [AST_OP_GT] = LIR_OPCODE_BLE,
        [AST_OP_GE] = LIR_OPCODE_BLT,
        [AST_OP_EE] = LIR_OPCODE_BNE,
        [AST_OP_NE] = LIR_OPCODE_BEE,
};

static lir_operand_t *
linear_inline_arr_element_addr_not_check(module_t *m, lir_operand_t *arr_target, lir_operand_t *index_target,
                                         type_t arr_type) {
    assert(arr_type.kind == TYPE_ARR);
    assert(arr_target->assert_type != LIR_OPERAND_SYMBOL_VAR);

    type_kind index_kind = operand_type_kind(index_target);
    if (type_kind_sizeof(index_kind) < POINTER_SIZE) {
        lir_operand_t *temp_index_target = temp_var_operand(m, type_kind_new(TYPE_INT));
        OP_PUSH(lir_op_uext(temp_index_target, index_target));
        index_target = temp_index_target;
    }

    int64_t element_size = type_sizeof(arr_type.array->element_type);

    // 计算偏移量: index * element_size
    lir_operand_t *offset_target = temp_var_operand(m, type_kind_new(TYPE_INT));
    OP_PUSH(lir_op_new(LIR_OPCODE_MUL, index_target, int_operand(element_size), offset_target));

    // 计算最终地址 data + offset 并返回
    lir_operand_t *result = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    OP_PUSH(lir_op_new(LIR_OPCODE_ADD, arr_target, offset_target, result));
    return result;
}

static lir_operand_t *
linear_inline_arr_element_addr(module_t *m, lir_operand_t *arr_target, lir_operand_t *index_target, type_t arr_type) {
    assert(arr_type.kind == TYPE_ARR);
    assert(arr_target->assert_type != LIR_OPERAND_SYMBOL_VAR);

    type_kind index_kind = operand_type_kind(index_target);
    if (type_kind_sizeof(index_kind) < POINTER_SIZE) {
        lir_operand_t *temp_index_target = temp_var_operand(m, type_kind_new(TYPE_INT));
        OP_PUSH(lir_op_uext(temp_index_target, index_target));
        index_target = temp_index_target;
    }

    // get vec length
    lir_operand_t *length_target = int_operand(arr_type.array->length);

    // cmp index < length to
    lir_operand_t *cmp_result = temp_var_operand_with_alloc(m, type_kind_new(TYPE_BOOL));
    OP_PUSH(lir_op_new(LIR_OPCODE_USLT, index_target, length_target, cmp_result));

    char *cmd_label_ident = label_ident_with_unique(".index");
    char *end_label_ident = str_connect(cmd_label_ident, LABEL_END_SUFFIX);
    lir_operand_t *cmp_end_label = lir_label_operand(end_label_ident, true);

    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), cmp_result, cmp_end_label));

    // TODO 共用 index out error handle label, 也就是有错误直接 goto 到这个地方，而不需要生成成百上千个
    char *error_label_ident = m->current_closure->error_label;
    bool be_catch = false;
    if (!stack_empty(m->current_closure->catch_error_labels)) {
        error_label_ident = stack_top(m->current_closure->catch_error_labels);
        be_catch = true;
    }

    push_rt_call(m, RT_CALL_THROW_INDEX_OUT_ERROR, NULL, 3, index_target, length_target, bool_operand(be_catch));
    // bal catch or end label
    OP_PUSH(lir_op_bal(lir_label_operand(error_label_ident, true)));
    OP_PUSH(lir_op_label(end_label_ident, true));

    int64_t element_size = type_sizeof(arr_type.array->element_type);

    // 计算偏移量: index * element_size
    lir_operand_t *offset_target = temp_var_operand(m, type_kind_new(TYPE_INT));
    OP_PUSH(lir_op_new(LIR_OPCODE_MUL, index_target, int_operand(element_size), offset_target));

    // 计算最终地址 data + offset 并返回
    lir_operand_t *result = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    OP_PUSH(lir_op_new(LIR_OPCODE_ADD, arr_target, offset_target, result));
    return result;
}

static lir_operand_t *
linear_inline_vec_element_addr_no_check(module_t *m, lir_operand_t *vec_target, lir_operand_t *index_target,
                                        type_t vec_element_type) {
    assert(vec_element_type.kind > 0);

    type_kind index_kind = operand_type_kind(index_target);

    if (type_kind_sizeof(index_kind) < POINTER_SIZE) {
        lir_operand_t *temp_index_target = temp_var_operand(m, type_kind_new(TYPE_INT));
        OP_PUSH(lir_op_uext(temp_index_target, index_target));

        index_target = temp_index_target;
    }

    if (vec_target->assert_type == LIR_OPERAND_SYMBOL_VAR) { // global vec copy to temp
        lir_operand_t *temp_vec_target = temp_var_operand(m, type_new(TYPE_VEC, &vec_element_type));
        OP_PUSH(lir_op_move(temp_vec_target, vec_target));
        vec_target = temp_vec_target;
    }

    // get vec length
    lir_operand_t *length_target = temp_var_operand(m, type_kind_new(TYPE_INT));
    lir_operand_t *length_src = indirect_addr_operand(m, type_kind_new(TYPE_INT), vec_target,
                                                      offsetof(n_vec_t, length));
    OP_PUSH(lir_op_move(length_target, length_src));

    int64_t element_size = type_sizeof(vec_element_type);

    // 计算偏移量: index * element_size
    lir_operand_t *offset = temp_var_operand(m, type_kind_new(TYPE_INT));
    OP_PUSH(lir_op_new(LIR_OPCODE_MUL, index_target, int_operand(element_size), offset));

    // 获取 data value(is_ptr)
    lir_operand_t *data_ptr = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    lir_operand_t *data_src = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), vec_target, offsetof(n_vec_t, data));
    OP_PUSH(lir_op_move(data_ptr, data_src));

    // 计算最终地址 data + offset 并返回
    lir_operand_t *result = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    OP_PUSH(lir_op_new(LIR_OPCODE_ADD, data_ptr, offset, result));
    return result;
}


static lir_operand_t *
linear_inline_vec_element_addr(module_t *m, lir_operand_t *vec_target, lir_operand_t *index_target,
                               type_t vec_element_type) {
    assert(vec_element_type.kind > 0);

    type_kind index_kind = operand_type_kind(index_target);

    if (type_kind_sizeof(index_kind) < POINTER_SIZE) {
        lir_operand_t *temp_index_target = temp_var_operand(m, type_kind_new(TYPE_INT));
        OP_PUSH(lir_op_uext(temp_index_target, index_target));

        index_target = temp_index_target;
    }

    if (vec_target->assert_type == LIR_OPERAND_SYMBOL_VAR) { // global vec copy to temp
        lir_operand_t *temp_vec_target = temp_var_operand(m, type_new(TYPE_VEC, &vec_element_type));
        OP_PUSH(lir_op_move(temp_vec_target, vec_target));
        vec_target = temp_vec_target;
    }

    // get vec length
    lir_operand_t *length_target = temp_var_operand(m, type_kind_new(TYPE_INT));
    lir_operand_t *length_src = indirect_addr_operand(m, type_kind_new(TYPE_INT), vec_target,
                                                      offsetof(n_vec_t, length));
    OP_PUSH(lir_op_move(length_target, length_src));

    // cmp index < length to
    lir_operand_t *cmp_result = temp_var_operand_with_alloc(m, type_kind_new(TYPE_BOOL));

    OP_PUSH(lir_op_new(LIR_OPCODE_USLT, index_target, length_target, cmp_result));

    char *cmd_label_ident = label_ident_with_unique(".index");
    char *end_label_ident = str_connect(cmd_label_ident, LABEL_END_SUFFIX);
    lir_operand_t *cmp_end_label = lir_label_operand(end_label_ident, true);

    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), cmp_result, cmp_end_label));

    char *error_label_ident = m->current_closure->error_label;
    bool be_catch = false;
    if (!stack_empty(m->current_closure->catch_error_labels)) {
        error_label_ident = stack_top(m->current_closure->catch_error_labels);
        be_catch = true;
    }

    push_rt_call(m, RT_CALL_THROW_INDEX_OUT_ERROR, NULL, 3, index_target, length_target, bool_operand(be_catch));
    // bal catch or end label
    OP_PUSH(lir_op_bal(lir_label_operand(error_label_ident, true)));
    OP_PUSH(lir_op_label(end_label_ident, true));

    int64_t element_size = type_sizeof(vec_element_type);

    // 计算偏移量: index * element_size
    lir_operand_t *offset = temp_var_operand(m, type_kind_new(TYPE_INT));
    OP_PUSH(lir_op_new(LIR_OPCODE_MUL, index_target, int_operand(element_size), offset));

    // 获取 data value(is_ptr)
    lir_operand_t *data_ptr = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    lir_operand_t *data_src = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), vec_target, offsetof(n_vec_t, data));
    OP_PUSH(lir_op_move(data_ptr, data_src));

    // 计算最终地址 data + offset 并返回
    lir_operand_t *result = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    OP_PUSH(lir_op_new(LIR_OPCODE_ADD, data_ptr, offset, result));
    return result;
}

static lir_operand_t *linear_unary(module_t *m, ast_expr_t expr, lir_operand_t *target);

/**
 * - 识别 dst(target) 是否为 null
 * - 如果是栈类型的数据，比如 struct/arr 则进行整片内存区域的 copy
 * - 进行普通的 <= 8byte move inst
 * @param m
 * @param expr_type
 * @param dst
 * @param src
 * @return
 */
static lir_operand_t *linear_super_move(module_t *m, type_t t, lir_operand_t *dst, lir_operand_t *src) {
    if (!dst) {
        return src;
    }

    if (is_stack_ref_big_type(t) && type_sizeof(t) > 0) {
        // 如果 dst 或者 src 是 global symbol, 则需要通过 lea 指令加载到 var 中
        if (dst->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            lir_operand_t *dst_ref = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_lea(dst_ref, dst));
            dst = dst_ref;
        }

        if (src->assert_type == LIR_OPERAND_SYMBOL_VAR) {
            lir_operand_t *src_ref = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_lea(src_ref, src));
            src = src_ref;
        }

        // indirect_addr(maybe env)
        if (src->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        }

        linked_t *temps = lir_memory_mov(m, type_sizeof(t), dst, src);
        linked_concat(m->current_closure->operations, temps);
        return dst;
    }

    OP_PUSH(lir_op_move(dst, src));
    return dst;
}

static lir_operand_t *linear_default_string(module_t *m, type_t t, lir_operand_t *target) {
    push_rt_call(m, RT_CALL_STRING_NEW, target, 2, string_operand("", 0), int_operand(0));
    return target;
}

static lir_operand_t *linear_default_nullable(module_t *m, type_t t, lir_operand_t *target) {
    type_t null_type = type_kind_new(TYPE_NULL);
    uint64_t rtype_hash = type_hash(null_type);
    lir_operand_t *null_operand = temp_var_operand(m, null_type);
    null_operand = linear_default_operand(m, null_type, null_operand);

    // lea null operand var
    lir_operand_t *value_ref = lea_operand_pointer(m, null_operand);
    push_rt_call(m, RT_CALL_UNION_CASTING, target, 2, int_operand(rtype_hash), value_ref);
    return target;
}

static lir_operand_t *
linear_default_vec(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    lir_operand_t *rtype_hash = int_operand(type_hash(t));
    lir_operand_t *element_index = int_operand(type_hash(t.vec->element_type));
    lir_operand_t *cap_operand = int_operand(VEC_DEFAULT_CAPACITY); // default cap_operand
    push_rt_call(m, RT_CALL_VEC_CAP, target, 3, rtype_hash, element_index, cap_operand);
    return target;
}

static lir_operand_t *
linear_unsafe_vec_new(module_t *m, type_t t, uint64_t len, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    lir_operand_t *rtype_hash = int_operand(type_hash(t));
    lir_operand_t *element_hash = int_operand(type_hash(t.vec->element_type));
    lir_operand_t *len_operand = int_operand(len); // default cap_operand
    push_rt_call(m, RT_CALL_UNSAFE_VEC_NEW, target, 3, rtype_hash, element_hash, len_operand);
    return target;
}

// target 中保存了栈地址，开始向上清理
static void linear_default_empty_stack(module_t *m, lir_operand_t *target, uint64_t offset, uint64_t size) {
    uint64_t remind = size;
    while (remind > 0) {
        uint64_t count = 0;
        uint64_t item_size = 0; // unit byte
        type_kind kind;
        if (remind >= QWORD) {
            kind = TYPE_UINT64;
            item_size = QWORD;
        } else if (remind >= DWORD) {
            kind = TYPE_UINT32;
            item_size = DWORD;
        } else if (remind >= WORD) {
            kind = TYPE_UINT16;
            item_size = WORD;
        } else {
            kind = TYPE_UINT8;
            item_size = BYTE;
        }

        count = remind / item_size;
        for (int i = 0; i < count; ++i) {
            lir_operand_t *dst = indirect_addr_operand(m, type_kind_new(kind), target, offset);
            OP_PUSH(lir_op_move(dst, integer_operand(0, kind)));
            offset += item_size;
        }

        remind -= count * item_size;
    }
}

static lir_operand_t *linear_default_arr(module_t *m, type_t arrary_type, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, arrary_type);
    }
    uint64_t element_size = type_sizeof(arrary_type.array->element_type);

    if (kind_in_heap(arrary_type.array->element_type.kind)) {
        for (int i = 0; i < arrary_type.array->length; i++) {
            // 基于 target 生成
            lir_operand_t *item_target = indirect_addr_operand(m, arrary_type.array->element_type, target,
                                                               i * element_size);

            linear_default_operand(m, arrary_type.array->element_type, item_target);
        }
    } else {
        linear_default_empty_stack(m, target, 0, type_sizeof(arrary_type));
    }
    return target;
}

static lir_operand_t *linear_default_map(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    uint64_t rtype_hash = type_hash(t);
    uint64_t key_hash = type_hash(t.map->key_type);
    uint64_t value_hash = type_hash(t.map->value_type);

    lir_operand_t *result = temp_var_operand_with_alloc(m, t);
    push_rt_call(m, RT_CALL_MAP_NEW, target, 3, int_operand(rtype_hash), int_operand(key_hash),
                 int_operand(value_hash));

    return target;
}

static lir_operand_t *linear_default_set(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    uint64_t rtype_hash = type_hash(t);
    uint64_t key_index = type_hash(t.map->key_type);

    push_rt_call(m, RT_CALL_SET_NEW, target, 2, int_operand(rtype_hash), int_operand(key_index));
    return target;
}

/**
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_struct_fill_default(module_t *m, type_t t, lir_operand_t *target, table_t *exists) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    assert(target);
    //    assert(target->assert_type == LIR_OPERAND_VAR);
    assert(t.kind == TYPE_STRUCT);

    for (int i = 0; i < t.struct_->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t.struct_->properties, i);

        if (exists && table_exist(exists, p->name)) {
            continue;
        }

        uint64_t offset = type_struct_offset(t.struct_, p->name);
        lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
        if (is_stack_ref_big_type(p->type)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_default_operand(m, p->type, dst);
    }

    return target;
}

/**
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_default_tuple(module_t *m, type_t t, lir_operand_t *target) {
    uint64_t rtype_hash = type_hash(t);
    push_rt_call(m, RT_CALL_TUPLE_NEW, target, 1, int_operand(rtype_hash));

    uint64_t offset = 0;
    for (int i = 0; i < t.tuple->elements->length; ++i) {
        type_t *element = ct_list_value(t.tuple->elements, i);

        uint64_t element_size = type_sizeof(*element);
        uint64_t element_align = type_alignof(*element);

        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align_up(offset, element_align);

        // 基于 target 计算 addr
        lir_operand_t *dst = indirect_addr_operand(m, *element, target, offset);
        if (is_stack_ref_big_type(*element)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_default_operand(m, *element, dst);
        offset += element_size;
    }

    return target;
}

static inline bool has_default_operand(module_t *m, type_t t) {
    return is_clv_default_type(t) || t.kind == TYPE_STRING || t.kind == TYPE_VEC || t.kind == TYPE_ARR ||
           t.kind == TYPE_MAP || t.kind == TYPE_SET || t.kind == TYPE_STRUCT || t.kind == TYPE_TUPLE;
}

/*
 * raw ptr 无法赋默认值
 */
static lir_operand_t *linear_default_operand(module_t *m, type_t t, lir_operand_t *target) {
    if (is_clv_default_type(t)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, target));
        return target;
    }

    if (t.kind == TYPE_UNION && (t.union_->nullable || t.union_->any)) {
        return linear_default_nullable(m, t, target);
    }

    if (t.kind == TYPE_STRING) {
        return linear_default_string(m, t, target);
    }

    if (t.kind == TYPE_VEC) {
        return linear_default_vec(m, t, target);
    }

    if (t.kind == TYPE_ARR) {
        return linear_default_arr(m, t, target);
    }

    if (t.kind == TYPE_MAP) {
        return linear_default_map(m, t, target);
    }

    if (t.kind == TYPE_SET) {
        return linear_default_set(m, t, target);
    }

    if (t.kind == TYPE_STRUCT) {
        return linear_struct_fill_default(m, t, target, NULL);
    }

    if (t.kind == TYPE_TUPLE) {
        return linear_default_tuple(m, t, target);
    }

    LINEAR_ASSERTF(false, "type '%s' must assign default value", type_format(t))
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
    char *symbol_ident = ident->literal;
    ast_fndef_t *fndef = s->ast_value;
    if (fndef->linkid) {
        symbol_ident = fndef->linkid;
    }

    return lir_label_operand(symbol_ident, s->is_local);
}

static void linear_has_panic(module_t *m) {
    char *error_target_label = m->current_closure->error_label;
    assertf(m->current_line < 1000000, "line '%d' exception", m->current_line);
    assertf(m->current_column < 1000000, "column '%d' exception", m->current_line);

    // panic 必须被立刻 catch, 判断当前表达式是否被 catch, 如果被 catch, 则走正常的 error 流程, 也就是有错误跳转到 catch label
    bool be_catch = false;
    if (!stack_empty(m->current_closure->catch_error_labels)) {
        error_target_label = stack_top(m->current_closure->catch_error_labels);
        be_catch = true;
    }

    lir_operand_t *path_operand = string_operand(m->rel_path, strlen(m->rel_path));
    lir_operand_t *fn_name_operand = string_operand(m->current_closure->fndef->fn_name_with_pkg, strlen(m->current_closure->fndef->fn_name_with_pkg));
    lir_operand_t *line_operand = int_operand(m->current_line);
    lir_operand_t *column_operand = int_operand(m->current_column);

    // 不可抢占，也不 yield，所以不需要添加任何勾子。
    // 如果没有被 catch, hash_error 直接 panic 并退出程序执行, 从而提醒 coder 及时处理错误
    lir_operand_t *has_error = temp_var_operand_with_alloc(m, type_kind_new(TYPE_BOOL));
    push_rt_call(m, RT_CALL_CO_HAS_PANIC, has_error, 5, bool_operand(be_catch), path_operand, fn_name_operand,
                 line_operand,
                 column_operand);

    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), has_error, lir_label_operand(error_target_label, true)));
}


static void linear_has_error(module_t *m) {
    char *error_target_label = m->current_closure->error_label;
    assertf(m->current_line < 1000000, "line '%d' exception", m->current_line);
    assertf(m->current_column < 1000000, "column '%d' exception", m->current_line);

    // 存在 catch error
    if (!stack_empty(m->current_closure->catch_error_labels)) {
        error_target_label = stack_top(m->current_closure->catch_error_labels);
    }

    lir_operand_t *has_error = temp_var_operand_with_alloc(m, type_kind_new(TYPE_BOOL));

    lir_operand_t *path_operand = string_operand(m->rel_path, strlen(m->rel_path));
    lir_operand_t *fn_name_operand = string_operand(m->current_closure->fndef->fn_name_with_pkg, strlen(m->current_closure->fndef->fn_name_with_pkg));
    lir_operand_t *line_operand = int_operand(m->current_line);
    lir_operand_t *column_operand = int_operand(m->current_column);

    // 不可抢占，也不 yield，所以不需要添加任何勾子。
    push_rt_call(m, RT_CALL_CO_HAS_ERROR, has_error, 4, path_operand, fn_name_operand, line_operand,
                 column_operand);
    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), has_error, lir_label_operand(error_target_label, true)));
}

/**
 * 当 local var 被 child fn 引用时, 需要将原本在栈中分配的 var 改成堆分配, 并对 ast_var_decl 中的 heap_ident 赋值
 * 避免可能的在 child fn 中产生的协程操作导致的 stack var 无法访问。当然这不是一个好的方式，并不是所有的 capture var 都会被协程
 * 引用并访问，更好的方式是分析 stack var 的使用点，判断其是否被协程引用。为了 nature 的快速完成，在当前版本中暂时不会进行此类分析。
 *
 * 原始 stmt: int a  = 12 如果 a  被闭包引用或者 impl call 使用，则 a 需要在堆上分配, 一般情况只需要改写 int a = 12 表达式为 ptr<int> a = gc_malloc, 然后
 * 赋值为 12 即可，但是 lir fn begin 指令中引用的 fn param 是 local var 其在 lower 阶段需要通过复杂的系统 ABI 改写。直接改写 fn param 为 ptr<int>
 * 造成的了后续的复杂性，所以选择了下面的实现方式
 *
 * int *t1 = gc_malloc()
 * *t1 = a // 后续使用中将不再使用 a， 使用 t1 来进行替代
 *
 * ---
 * 一般情况下 struct/arr 采用了指针引用买，所以不需要通过 escape rewrite, 但
 * fn param 如果是一个 struct 并且占用空间足够大的时候，param 不会直接申请空间，caller 会为 struct 申请足够的空间，callee 直接使用即可。
 * 但如果 callee 对应的 param 需要进行 gc_malloc, amd64_abi 中并不会进行完整的 struct mov，所以这里对 param 中这种特殊情况，采取 escape rewrite 的方式
 * 进行一次 super mov。
 *
 * @param m
 * @param var_decl
 */
static void linear_escape_rewrite(module_t *m, ast_var_decl_t *var_decl, bool force_rewrite) {
    symbol_t *s = symbol_table_get(var_decl->ident);
    assertf(s, "ident %s not declare");
    assert(s->type == SYMBOL_VAR);
    ast_var_decl_t *symbol_var = s->ast_value;

    // type 带有 in_heap 标志的简单类型需要进行 escape rewrite 改写才能分配在栈上
    if (!is_stack_alloc_type(symbol_var->type)) {
        return;
    }

    // not need
    if (is_stack_ref_big_type(symbol_var->type) && !force_rewrite) {
        return;
    }

    if (!symbol_var->type.in_heap) {
        return;
    }


    type_t heap_type = type_ptrof(symbol_var->type);
    if (is_stack_ref_big_type(symbol_var->type)) {
        heap_type = symbol_var->type;
    }

    // 由于数据在堆中分配，所以不需要调用 temp_var_operand_with_alloc 触发栈分配
    lir_operand_t *heap_operand = temp_var_operand(m, heap_type);
    symbol_var->heap_ident = ((lir_var_t *) heap_operand->value)->ident;

    lir_operand_t *temp_operand = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));

    // - 基于原始类型申请堆空间,此时 temp_operand 时一个指针类型
    uint64_t rtype_hash = type_hash(symbol_var->type);
    push_rt_call(m, RT_CALL_GC_MALLOC, temp_operand, 1, int_operand(rtype_hash));
    OP_PUSH(lir_op_move(heap_operand, temp_operand));

    // - mov src 则是原始数据
    lir_operand_t *src = lir_var_operand(m, var_decl->ident);
    lir_operand_t *dst = heap_operand;
    if (!is_stack_ref_big_type(var_decl->type)) {
        dst = indirect_addr_operand(m, var_decl->type, heap_operand, 0);
    }

    linear_super_move(m, var_decl->type, dst, src);
}

/**
 * 对于小于 8byte 类型的变量，可以直接将其值存储在 虚拟寄存器中.
 * 对于大于 8byte 类型的变量 (比如 struct/array) 通常需要在栈上申请空间, 虚拟寄存器中存储的是对应的栈地址
 * int a;
 * float b;
 * person a;
 * @param c
 * @param var_decl
 * @return
 */
static lir_operand_t *linear_var_decl(module_t *m, ast_var_decl_t *var_decl) {
    lir_operand_t *target = lir_var_operand(m, var_decl->ident);

    // 读取符号判断在堆上分配还是在栈上分配
    lir_var_t *var = target->value;
    symbol_t *s = symbol_table_get(var->ident);
    assert(s->type == SYMBOL_VAR);
    ast_var_decl_t *symbol_var = s->ast_value;

    if (is_stack_ref_big_type(var_decl->type)) {
        if (symbol_var->type.in_heap) {
            uint64_t rtype_hash = type_hash(var->type);
            // 更新类型避免在 lower 被识别成 struct 进行 amd64 下的特殊值传递
            type_t temp_type = var->type;
            var->type = type_kind_new(TYPE_ANYPTR);
            // gc malloc 的结果是指针，所以不能按照 target 来处理，至少 type 需要抵消掉
            push_rt_call(m, RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash));
            // 恢复 type, 后续的 struct 需要作为 struct 使用

            var->type = temp_type;
        } else {
            // 在没有逃逸分析之前默认直接在栈上分配
            OP_PUSH(lir_stack_alloc(m->current_closure, var_decl->type, target));
        }

        return target;
    }

    return target;
}

/**
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_ident(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_ident *ident = expr.value;
    symbol_t *s = symbol_table_get(ident->literal);
    assertf(s, "ident %s not declare");

    if (s->type == SYMBOL_FN) {
        // 现在 symbol fn 是作为一个 type_fn 值进行传递，所以需要取出其 label 进行处理。
        // 即使是 global fn 也不例外, linear call symbol 已经进行了特殊处理，进不到这里来
        // linkident 特殊处理
        ast_fndef_t *fndef = s->ast_value;
        char *symbol_ident = fndef->symbol_name;
        if (fndef->linkid) {
            symbol_ident = fndef->linkid;
        }

        // 加载 fn 地址
        lir_operand_t *fn_addr_operand = temp_var_operand(m, fndef->type);
        OP_PUSH(lir_op_lea(fn_addr_operand, symbol_label_operand(m, symbol_ident)));

        lir_operand_t *env_operand = int_operand(0);

        lir_operand_t *result = temp_var_operand(m, fndef->type);
        push_rt_call(m, RT_CALL_FN_NEW, result, 2, fn_addr_operand, env_operand);

        return linear_super_move(m, fndef->type, target, result);
    }

    if (s->type == SYMBOL_VAR) {
        ast_var_decl_t *symbol_var = s->ast_value;
        if (s->is_local) {
            // var 存在 heap_ident 表明当前 var 被弃用，后续应该使用 heap_ident。只有一些标量类型会发生这种情况。
            if (symbol_var->heap_ident) {
                assert(target == NULL);
                assert(is_stack_alloc_type(symbol_var->type));

                lir_operand_t *src = lir_var_operand(m, symbol_var->heap_ident);

                // struct 作为 param 时也需要进行 escape 处理
                if (is_stack_ref_big_type(symbol_var->type)) {
                    return src;
                }

                return indirect_addr_operand(m, symbol_var->type, src, 0);
            }

            lir_operand_t *src = lir_var_operand(m, ident->literal);
            return linear_super_move(m, expr.type, target, src);
        } else {
            lir_symbol_var_t *symbol = NEW(lir_symbol_var_t);
            symbol->ident = ident->literal;
            symbol->kind = symbol_var->type.kind;
            lir_operand_t *src = operand_new(LIR_OPERAND_SYMBOL_VAR, symbol);

            if (is_stack_ref_big_type(symbol_var->type)) {
                // 如果是 struct/arr 则直接返回 symbol 的地址, 并且继承原始类型, 而不是作为 void ptr
                // src_ref 此时是一个 var, 其中存储了一个地址，指向了 global data 中的一块内存区域
                // 总而言之返回的 src_ref 不再是 symbol_var 类型，而是 var 类型
                lir_operand_t *src_ref = temp_var_operand(m, symbol_var->type);
                OP_PUSH(lir_op_lea(src_ref, src));

                return linear_super_move(m, expr.type, target, src_ref);
            }

            // src 中直接存储了目标的值，如 int 类型就是 int 的值， vec 类型就是 vec 的指针
            return linear_super_move(m, expr.type, target, src);
        }
    }
    assertf(false, "ident %s exception", ident);
    exit(1);
}

/**
 * - 一旦将 addr 地址暴露出来，如果在 linear_expr 中存在 list push 操作就会造成 list.data 整体迁移，导致 addr 地址失效
 * - 应该总是优先编译右值，然后将右值 move 到左值中，避免出现 addr grow 的问题
 * @param m
 * @param stmt
 */
static void linear_vec_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_vec_access_t *vec_access = stmt->left.value;
    lir_operand_t *vec_target = linear_expr(m, vec_access->left, NULL);
    lir_operand_t *index_target = linear_expr(m, vec_access->index, NULL);
    type_t vec_element_type = vec_access->element_type;
    lir_operand_t *src = linear_expr(m, stmt->right, NULL);

    // target 中保存的是一个指针数据，指向的类型是 right.type
    lir_operand_t *target = linear_inline_vec_element_addr(m, vec_target, index_target, vec_element_type);

    if (is_gc_alloc(vec_element_type.kind)) {
        // target 已经是指针了，不需要再次计算 slot
        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, target, src);
    } else {
        if (!is_stack_ref_big_type(vec_element_type)) {
            target = indirect_addr_operand(m, vec_element_type, target, 0);
        }

        linear_super_move(m, vec_element_type, target, src);
    }
}

static void linear_array_assign(module_t *m, ast_assign_stmt_t *stmt) {
    // ptr 返回的是一个 indirect, 所以需要进行 lea 处理
    lir_operand_t *target = linear_array_access(m, stmt->left, NULL);
    if (is_gc_alloc(stmt->left.type.kind)) {
        lir_operand_t *new_obj = linear_expr(m, stmt->right, NULL);
        lir_operand_t *dst_slot = lea_operand_pointer(m, target);

        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_slot, new_obj);
    } else {
        linear_expr(m, stmt->right, target);
    }
}

static void linear_tuple_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_tuple_access_t *tuple_access = stmt->left.value;
    type_t tuple_type = tuple_access->left.type;
    lir_operand_t *tuple_target = linear_expr(m, tuple_access->left, NULL);

    uint64_t element_size = type_sizeof(tuple_access->element_type);
    uint64_t offset = type_tuple_offset(tuple_type.tuple, tuple_access->index);
    lir_operand_t *dst = indirect_addr_operand(m, stmt->left.type, tuple_target, offset);

    if (is_gc_alloc(stmt->left.type.kind)) {
        lir_operand_t *obj = linear_expr(m, stmt->right, NULL);
        lir_operand_t *dst_slot = lea_operand_pointer(m, dst);
        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_slot, obj);
    } else {
        if (is_stack_ref_big_type(stmt->left.type)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_expr(m, stmt->right, dst);
    }
}

static lir_operand_t *linear_inline_env_values(module_t *m) {
    assertf(m->current_closure->env_operand, "have env access, must have fn_runtime_operand");

    // mov [env+0] -> values
    lir_operand_t *values_operand = temp_var_operand(m, type_kind_new(TYPE_GC_ENV_VALUES));
    OP_PUSH(lir_op_move(values_operand, indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), m->current_closure->env_operand, 0)));

    return values_operand;
}

/**
 * foo = 12
 * *envs[1] = 12
 * @param m
 * @param stmt
 */
static void linear_env_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_env_access_t *ast = stmt->left.value;
    lir_operand_t *index = int_operand(ast->index);

    // 不需要 ref，直接 move 过去就行
    lir_operand_t *src = linear_expr(m, stmt->right, NULL);
    //    lir_operand_t *src_ref = lea_operand_pointer(m, src);
    uint64_t size = type_sizeof(stmt->right.type);

    lir_operand_t *values_operand = linear_inline_env_values(m);

    // move [values+x] -> dst_ptr(dst_ptr is heap_value)
    lir_operand_t *env_offset_operand = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), values_operand, ast->index * POINTER_SIZE);


    if (is_stack_ref_big_type(stmt->right.type)) {
        assert(stmt->right.type.in_heap);
    }

    if (is_stack_alloc_type(stmt->left.type)) {
        // mov to indirect(dst_ptr)
        lir_operand_t *dst_ptr = temp_var_operand(m, type_ptrof(stmt->right.type));
        OP_PUSH(lir_op_move(dst_ptr, env_offset_operand)); // mov env_values[0] -> dst_ptr

        if (is_stack_ref_big_type(stmt->left.type)) {
            linked_concat(m->current_closure->operations, lir_memory_mov(m, size, dst_ptr, src));
        } else {
            lir_operand_t *dst = indirect_addr_operand(m, stmt->left.type, dst_ptr, 0);
            OP_PUSH(lir_op_move(dst, src));
        }
    } else {
        // vec/map...
        lir_operand_t *dst_ptr = lea_operand_pointer(m, env_offset_operand);
        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_ptr, src);
    }
}

static void linear_map_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_map_access_t *map_access = stmt->left.value;
    lir_operand_t *map_target = linear_expr(m, map_access->left, NULL);

    lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, map_access->key, NULL));

    // dst 是一个 slot 提供写入地址
    lir_operand_t *dst = temp_var_operand_with_alloc(m, type_kind_new(TYPE_ANYPTR));
    push_rt_call(m, RT_CALL_MAP_ASSIGN, dst, 2, map_target, key_ref);


    if (is_gc_alloc(stmt->left.type.kind)) {
        // 不包含 struct/array
        lir_operand_t *obj = linear_expr(m, stmt->right, NULL);
        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst, obj);
    } else {
        if (!is_stack_ref_big_type(stmt->right.type)) {
            dst = indirect_addr_operand(m, stmt->right.type, dst, 0);
        }
        linear_expr(m, stmt->right, dst);
    }
}

static void linear_struct_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_struct_select_t *struct_access = stmt->left.value;
    type_t type_struct;

    lir_operand_t *struct_target = linear_expr(m, struct_access->instance, NULL);
    type_struct = struct_access->instance.type;
    if (is_struct_ptr(struct_access->instance.type)) {
        type_struct = struct_access->instance.type.ptr->value_type;
    } else if (is_struct_rawptr(type_struct)) {
        type_struct = type_struct.ptr->value_type;
    }

    assert(type_struct.kind == TYPE_STRUCT);

    int64_t offset = type_struct_offset(type_struct.struct_, struct_access->key);

    // indirect_addr -> [rax + 0x8], rax 存储的内容必须是一个内存地址, indirect addr 对于不同的指令来说含义不同
    // 1. mov 12 -> [rax+0x8] 表示将 12 移动到 rax 移动到 rax+0x8 对应的内存地址中, 移动的大小根据声明的尺寸
    // 2. lea [rax+0x8] -> rax 表示将 rax+0x8 计算到的内存地址存储在 rax 中
    // 3. push [rax+0x8] 等同于 mov 表示移动数据
    // 如果将 indirect 作为函数的参数，函数的参数在编译时默认使用 push/mov 传输，所以传输的是数据，如果需要 addr 则需要使用 lea
    // 将地址加载出来.
    // 由于 stmt->right 的 type 可能会超过 8byte, 所以通过 lea 将地址加载出来，再通过 lir_memory_mov 进行内存拷贝是正确的选择
    // 当然没 lea 的另外一个原因是，其会产生一个新的变量指向 struct instance 对应的内存区域, 在编译 stmt->right 时
    // 如果 stmt->right 是一个 arr/struct 这样的在内存中分配的区域，那么 linear 的返回值
    lir_operand_t *dst_slot = indirect_addr_operand(m, stmt->left.type, struct_target, offset);

    // TODO stmt->right.type.in_heap? handle
    if (is_gc_alloc(stmt->left.type.kind)) {
        lir_operand_t *obj = linear_expr(m, stmt->right, NULL);

        dst_slot = lea_operand_pointer(m, dst_slot);
        //        obj = lea_operand_pointer(m, obj);

        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_slot, obj);
    } else {
        // lea [rax+16], rcx
        if (is_stack_ref_big_type(stmt->left.type)) {
            dst_slot = lea_operand_pointer(m, dst_slot);
        }
        linear_expr(m, stmt->right, dst_slot);
    }
}

static void linear_ia_ptr_assign(module_t *m, ast_assign_stmt_t *stmt) {
    assert(stmt->left.assert_type == AST_EXPR_UNARY);
    ast_unary_expr_t *unary_expr = stmt->left.value;
    assert(unary_expr->op == AST_OP_IA);

    // 右值可能是一个 struct_new, 但是这又有什么影响呢，你必须创造新的空间？
    lir_operand_t *src = linear_expr(m, stmt->right, NULL);

    lir_operand_t *ptr_operand = linear_expr(m, unary_expr->operand, NULL);

    // *ptr = [a, b, c]
    // *ptr = 12
    lir_operand_t *dst;
    if (is_stack_ref_big_type(stmt->left.type)) {
        dst = ptr_operand; // super_move 会自动进行全尺寸移动
    } else {
        dst = indirect_addr_operand(m, stmt->left.type, ptr_operand, 0);
    }

    linear_super_move(m, stmt->left.type, dst, src);
}

/**
 * ident = operand
 * @param c
 * @param stmt
 */
static void linear_ident_assign(module_t *m, ast_assign_stmt_t *stmt) {
    assert(stmt->left.assert_type == AST_EXPR_IDENT);

    // 右值可能是一个 struct_new, 但是这又有什么影响呢，你必须创造新的空间？
    lir_operand_t *src = linear_expr(m, stmt->right, NULL);

    lir_operand_t *dst = linear_ident(m, stmt->left, NULL); // ident

    assert(stmt->left.type.kind > 0 && stmt->left.type.kind != TYPE_UNKNOWN);
    linear_super_move(m, stmt->left.type, dst, src);
}

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

        uint64_t element_size = type_sizeof(element->type);
        uint64_t element_align = type_alignof(element->type);

        offset = align_up(offset, element_align);

        // src 中已经保存了右值的具体值。可以用来 move
        lir_operand_t *element_src_operand = indirect_addr_operand(m, element->type, tuple_target, offset);
        if (is_stack_ref_big_type(element->type)) {
            element_src_operand = lea_operand_pointer(m, element_src_operand);
        }

        if (can_assign(element->assert_type)) {
            // 使用一个 temp_var 接收右值(走普通 mov, struct/array 不进行临时值生成)
            // 主要是为了得到 ident, 方便模拟表达式试过
            lir_operand_t *temp_var = temp_var_operand(m, element->type);
            OP_PUSH(lir_op_move(temp_var, element_src_operand));

            char *ident = ((lir_var_t *) temp_var->value)->ident;

            ast_assign_stmt_t *assign_stmt = NEW(ast_assign_stmt_t);
            assign_stmt->left = *element;
            assign_stmt->right = *ast_ident_expr(m->current_line, m->current_column, ident);
            assign_stmt->right.type = element->type;
            linear_assign(m, assign_stmt);
        } else if (element->assert_type == AST_EXPR_TUPLE_DESTR) {
            ast_tuple_destr_t *tuple_destr = element->value;

            linear_tuple_destr(m, tuple_destr, element_src_operand);
        } else {
            assertf(false, "var tuple destr must var/tuple_destr");
        }
        offset += element_size;
    }
}

/**
 * (a, b, (c[0], d.b)) = operand
 * @param m
 * @param stmt
 */
static void linear_tuple_destr_stmt(module_t *m, ast_assign_stmt_t *stmt) {
    ast_tuple_destr_t *destr = stmt->left.value;
    lir_operand_t *tuple_target = linear_expr(m, stmt->right, NULL);
    linear_tuple_destr(m, destr, tuple_target);
}

/**
 * var (a, b, (c, d))
 * @param m
 * @param destr
 * @param tuple_target
 */
static void linear_var_tuple_destr(module_t *m, ast_tuple_destr_t *destr, lir_operand_t *tuple_target) {
    uint64_t offset = 0;

    for (int i = 0; i < destr->elements->length; ++i) {
        // 这里的 element 指的是左侧值的 element(一般都是 ident, 或者 access/select)
        ast_expr_t *element = ct_list_value(destr->elements, i);
        uint64_t element_size = type_sizeof(element->type);

        uint64_t element_align = type_alignof(element->type);

        offset = align_up(offset, element_align);

        // 将 tuple 中的值 mov 到新的 var 空间中
        lir_operand_t *element_src_operand = indirect_addr_operand(m, element->type, tuple_target, offset);
        if (is_stack_ref_big_type(element->type)) {
            element_src_operand = lea_operand_pointer(m, element_src_operand);
        }

        if (element->assert_type == AST_VAR_DECL) {
            // 由于存在 var 的赋值，所以这里存在重复的空间申请。这是没有问题的
            ast_var_decl_t *var_decl = element->value;
            lir_operand_t *dst = linear_var_decl(m, var_decl);

            linear_super_move(m, element->type, dst, element_src_operand);

            // var *t1 = 12
            linear_escape_rewrite(m, var_decl, false);
        } else if (element->assert_type == AST_EXPR_TUPLE_DESTR) {
            ast_tuple_destr_t *tuple_destr = element->value;

            linear_var_tuple_destr(m, tuple_destr, element_src_operand);
        } else {
            assertf(false, "var tuple destr must var/tuple_destr");
        }

        offset += element_size;
    }
}

/**
 * var (a, b, (c, d)) = operand
 * @param m
 * @param var_tuple_def
 * @return
 */
static void linear_var_tuple_def_stmt(module_t *m, ast_var_tuple_def_stmt_t *var_tuple_def) {
    // - 左侧的值如果是一个 tuple_def 是否需要申请空间？ 原则上不用，右侧值会申请足够的空间, 然后将
    // addr 返回回来，所以 tuple_target 中保存的就是一个指针地址。
    lir_operand_t *tuple_target = linear_expr(m, var_tuple_def->right, NULL);

    linear_var_tuple_destr(m, var_tuple_def->tuple_destr, tuple_target);
}

/**
 * var a = 1
 * var a = [1, 2, 3]
 * var a = person {}
 * var a = list[1]
 * @param stmt
 * @return
 */
static void linear_vardef(module_t *m, ast_vardef_stmt_t *stmt) {
    // 不传递 dst 在 type > 8byte 时会造成一定的空间浪费, 但是也让 move 和 super_move 的边界更加的清晰
    // 后续 expr 遇到 target 时总是使用普通 move, 只有在特定的地方(右值赋值给左值时，才会使用 super_move)
    // 左值右值主要提现在，vardef/var_tup_def/assign/call(arg)
    lir_operand_t *dst = linear_var_decl(m, &stmt->var_decl);
    assert(stmt->right);
    lir_operand_t *src = linear_expr(m, *stmt->right, NULL);

    linear_super_move(m, stmt->var_decl.type, dst, src);

    // 将 dst 值 copy 出来进行重新分配改写
    linear_escape_rewrite(m, &stmt->var_decl, false);
}

static void linear_expr_fake(module_t *m, ast_expr_fake_stmt_t *stmt) {
    linear_expr(m, stmt->expr, NULL);
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
    if (left.assert_type == AST_EXPR_VEC_ACCESS) {
        return linear_vec_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_ARRAY_ACCESS) {
        return linear_array_assign(m, stmt);
    }

    // set assign m['a'] = 2
    if (left.assert_type == AST_EXPR_MAP_ACCESS) {
        return linear_map_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_ENV_ACCESS) {
        return linear_env_assign(m, stmt);
    }

    if (left.assert_type == AST_EXPR_TUPLE_ACCESS) {
        return linear_tuple_assign(m, stmt);
    }

    // struct assign p.name = 'wei'
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

    if (left.assert_type == AST_EXPR_UNARY) {
        return linear_ia_ptr_assign(m, stmt);
    }

    // 比如 set[0] = 1; 不允许这么赋值，set  .add 来添加 key
    assertf(false, "dose not support assign to %d", left.assert_type);
}

static void linear_cmp_neg(module_t *m, ast_expr_t *cond, lir_operand_t *cmp_target_label, bool is_if) {
    bool binary_optimize = false;
    if (cond->assert_type == AST_EXPR_BINARY) {
        ast_binary_expr_t *binary_expr = cond->value;
        if (ast_op_is_cmp(binary_expr->op) && binary_expr->left.type.kind != TYPE_STRING) {
            binary_optimize = true;
        }
    }

    if (binary_optimize) {
        ast_binary_expr_t *binary_expr = cond->value;
        lir_operand_t *left_target = linear_expr(m, binary_expr->left, NULL);
        lir_operand_t *right_target = linear_expr(m, binary_expr->right, NULL);


        lir_opcode_t opcode;
        if (is_if) {
            opcode = ast_op_to_if_alert[binary_expr->op];
        } else {
            opcode = ast_op_to_for_continue[binary_expr->op];
        }

        if (is_unsigned(binary_expr->left.type.kind)) {
            switch (opcode) {
                case LIR_OPCODE_BLT:
                    opcode = LIR_OPCODE_BULT;
                    break;
                case LIR_OPCODE_BLE:
                    opcode = LIR_OPCODE_BULE;
                    break;
                case LIR_OPCODE_BGT:
                    opcode = LIR_OPCODE_BUGT;
                    break;
                case LIR_OPCODE_BGE:
                    opcode = LIR_OPCODE_BUGE;
                    break;
                default:
                    break;
            }
        }

        assert(opcode > 0);

        OP_PUSH(lir_op_new(opcode, left_target, right_target, cmp_target_label));
    } else {
        lir_operand_t *condition_target = linear_expr(m, *cond, NULL);
        lir_operand_t *cond_target;
        if (is_if) {
            cond_target = bool_operand(false);
        } else {
            cond_target = bool_operand(true);
        }

        OP_PUSH(lir_op_new(LIR_OPCODE_BEE, cond_target, condition_target, cmp_target_label));
    }
}

/**
 * push_rt_call get count => count
 * for_iterator:
 *  cmp_goto count == 0 to end for
 *  push_rt_call get key => key
 *  push_rt_call get value => value // 可选
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
    lir_operand_t *iterator_target = linear_expr(m, ast->iterate, NULL);

    uint64_t rtype_hash = type_hash(ast->iterate.type);

    // cursor 初始值
    lir_operand_t *cursor_operand = unique_var_operand(m, type_kind_new(TYPE_INT), ITERATOR_CURSOR);
    OP_PUSH(lir_op_move(cursor_operand, int_operand(-1))); // cursor 初始值 = --

    char *for_start_ident = label_ident_with_unique(FOR_ITERATOR_IDENT);
    char *for_update_ident = str_connect(for_start_ident, LABEL_UPDATE_SUFFIX);
    char *for_continue_ident = str_connect(for_start_ident, LABEL_CONTINUE_SUFFIX);
    char *for_end_ident = str_connect(for_start_ident, LABEL_END_SUFFIX);

    // make label
    lir_op_t *for_start_label = lir_op_label(for_start_ident, true);
    lir_op_t *for_end_label = lir_op_label(for_end_ident, true);

    stack_push(m->current_closure->continue_labels, for_start_label->output);
    stack_push(m->current_closure->break_labels, for_end_label->output);

    // set label
    OP_PUSH(for_start_label);

    // key 和 value 需要进行一次初始化
    lir_operand_t *first_target = linear_var_decl(m, &ast->first);
    lir_operand_t *first_ref;
    if (is_stack_ref_big_type(ast->first.type)) {
        // first _target 中已经保存了相关的地址，
        first_ref = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
        OP_PUSH(lir_op_move(first_ref, first_target));
    } else {
        OP_PUSH(lir_op_nop_def(first_target)); // var_decl 没有进行初始化，所以需要进行一下 def 初始化
        first_ref = lea_operand_pointer(m, first_target);
    }

    // iter 单个值的情况下(不存在 second) 的情况下， list/string/chan 使用 next_value, 其他所有情况都时需 next_key
    if (!ast->second && (ast->iterate.type.kind == TYPE_VEC || ast->iterate.type.kind == TYPE_STRING ||
                         ast->iterate.type.kind == TYPE_CHAN)) {
        push_rt_call(m, RT_CALL_ITERATOR_NEXT_VALUE, cursor_operand, 4, iterator_target, int_operand(rtype_hash),
                     cursor_operand,
                     first_ref);
    } else {
        push_rt_call(m, RT_CALL_ITERATOR_NEXT_KEY, cursor_operand, 4, iterator_target, int_operand(rtype_hash),
                     cursor_operand, first_ref);
    }

    // 基于 key 已经可以判断迭代是否还有了，下面的 next value 直接根据 cursor_operand 取值即可
    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, int_operand(-1), cursor_operand, lir_copy_label_operand(for_end_label->output)));

    // 添加 continue label
    OP_PUSH(lir_op_label(for_continue_ident, true));

    // gen value
    if (ast->second) {
        lir_operand_t *second_target = linear_var_decl(m, ast->second);
        lir_operand_t *second_ref;

        if (is_stack_ref_big_type(ast->second->type)) {
            second_ref = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_move(second_ref, second_target));
        } else {
            OP_PUSH(lir_op_nop_def(second_target));
            second_ref = lea_operand_pointer(m, second_target);
        }

        push_rt_call(m, RT_CALL_ITERATOR_TAKE_VALUE, NULL, 4, iterator_target, int_operand(rtype_hash), cursor_operand,
                     second_ref);
    }
    // block
    linear_body(m, ast->body);

    // goto for start
    OP_PUSH(lir_op_bal(for_start_label->output)); // 重新进行迭代的计算
    OP_PUSH(for_end_label);

    stack_pop(m->current_closure->continue_labels);
    stack_pop(m->current_closure->break_labels);
}

/**
 *
 * @param c
 * @param ast
 */
static void linear_for_cond(module_t *m, ast_for_cond_stmt_t *ast) {
    char *for_start_ident = label_ident_with_unique(FOR_COND_IDENT);
    char *for_continue_ident = str_connect(for_start_ident, ".cont");
    char *for_cond_ident = str_connect(for_start_ident, ".cond");
    char *for_end_ident = str_connect(for_start_ident, ".end");

    stack_push(m->current_closure->continue_labels, lir_label_operand(for_cond_ident, true));
    stack_push(m->current_closure->break_labels, lir_label_operand(for_end_ident, true));

    // for_cond start
    OP_PUSH(lir_op_label(for_start_ident, true));
    OP_PUSH(lir_op_bal(lir_label_operand(for_cond_ident, true)));

    // for_continue: loop body
    OP_PUSH(lir_op_label(for_continue_ident, true));
    linear_body(m, ast->body);

    // for_cond: condition check at end, use linear_cmp_neg for comparison optimization
    OP_PUSH(lir_op_label(for_cond_ident, true));
    linear_cmp_neg(m, &ast->condition, lir_label_operand(for_continue_ident, true), false);

    // for_end
    OP_PUSH(lir_op_label(for_end_ident, true));

    stack_pop(m->current_closure->continue_labels);
    stack_pop(m->current_closure->break_labels);
}

static void linear_for_tradition(module_t *m, ast_for_tradition_stmt_t *ast) {
    char *for_start_ident = label_ident_with_unique(FOR_TRADITION_IDENT);
    char *for_update_ident = str_connect(for_start_ident, ".up");
    char *for_continue_ident = str_connect(for_start_ident, ".cont");
    char *for_cond_ident = str_connect(for_start_ident, ".cond");
    char *for_end_ident = str_connect(for_start_ident, ".end");

    stack_push(m->current_closure->continue_labels, lir_label_operand(for_update_ident, true));
    stack_push(m->current_closure->break_labels, lir_label_operand(for_end_ident, true));

    // for_tradition
    OP_PUSH(lir_op_label(for_start_ident, true));
    linear_stmt(m, ast->init);
    OP_PUSH(lir_op_bal(lir_label_operand(for_cond_ident, true)));

    OP_PUSH(lir_op_label(for_continue_ident, true));
    linear_body(m, ast->body);

    OP_PUSH(lir_op_label(for_update_ident, true));
    linear_stmt(m, ast->update);

    OP_PUSH(lir_op_label(for_cond_ident, true));
    linear_cmp_neg(m, &ast->cond, lir_label_operand(for_continue_ident, true), false);

    OP_PUSH(lir_op_label(for_end_ident, true));

    stack_pop(m->current_closure->continue_labels);
    stack_pop(m->current_closure->break_labels);
}

static void linear_continue(module_t *m) {
    LINEAR_ASSERTF(m->current_closure->continue_labels->count > 0, "continue must use in for stmt")
    lir_operand_t *label = stack_top(m->current_closure->continue_labels);
    OP_PUSH(lir_op_bal(label));
}

static void linear_break(module_t *m) {
    LINEAR_ASSERTF(m->current_closure->break_labels->count > 0, "break must use in for stmt body");
    lir_operand_t *label = stack_top(m->current_closure->break_labels);
    OP_PUSH(lir_op_bal(label));
}

static void linear_ret(module_t *m, ast_ret_stmt_t *stmt) {
    LINEAR_ASSERTF(m->current_closure->ret_labels->count > 0, "ret must use in match body");
    lir_operand_t *label = stack_top(m->current_closure->ret_labels);

    // stmt->expr type maybe void, like println()
    if (m->current_closure->ret_targets->count > 0) {
        lir_operand_t *target = stack_top(m->current_closure->ret_targets);
        linear_expr(m, stmt->expr, target);
    }

    OP_PUSH(lir_op_new(LIR_OPCODE_RET, NULL, NULL, label));
    OP_PUSH(lir_op_bal(label));
}

static void linear_return(module_t *m, ast_return_stmt_t *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *src = linear_expr(m, *ast->expr, NULL);
        // return void_expr() 时, m->linear_current->return_operand 是 null
        //        if (m->current_closure->return_operand) {
        //            linear_super_move(m, ast->expr->type, m->current_closure->return_operand, src);
        //        }

        // 保留用来做 return check
        OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, src, NULL, NULL));
    }

    OP_PUSH(lir_op_bal(lir_label_operand(m->current_closure->end_label, false)));
}

static void linear_if(module_t *m, ast_if_stmt_t *if_stmt) {
    // 编译 condition
    char *if_label_ident = label_ident_with_unique(IF_IDENT);
    char *end_label_ident = str_connect(if_label_ident, IF_END_IDENT);
    char *alternate_label_ident = str_connect(if_label_ident, IF_ALTERNATE_IDENT);

    lir_operand_t *cmp_target_label;
    if (if_stmt->alternate->count == 0) {
        cmp_target_label = lir_label_operand(end_label_ident, true);
    } else {
        cmp_target_label = lir_label_operand(alternate_label_ident, true);
    }

    linear_cmp_neg(m, &if_stmt->condition, cmp_target_label, true);

    OP_PUSH(lir_op_label(str_connect(if_label_ident, IF_CONTINUE_IDENT), true));

    // 编译 consequent block
    linear_body(m, if_stmt->consequent);
    OP_PUSH(lir_op_bal(lir_label_operand(end_label_ident, true)));

    // 编译 alternate block
    if (if_stmt->alternate->count != 0) {
        OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, lir_label_operand(alternate_label_ident, true)));
        linear_body(m, if_stmt->alternate);
    }

    // 追加 end_if 标签
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, lir_label_operand(end_label_ident, true)));
}

static void linear_select(module_t *m, ast_select_stmt_t *select_stmt) {
    char *select_start_ident = label_ident_with_unique(SELECT_IDENT);
    char *select_end_ident = str_connect(select_start_ident, LABEL_END_SUFFIX);
    OP_PUSH(lir_op_label(select_start_ident, true));

    // 创建 call chan_select
    int64_t cases_count = select_stmt->send_count + select_stmt->recv_count;

    // select {} -> RT_CALL_SELECT_BLOCK
    if (cases_count == 0) {
        push_rt_call(m, RT_CALL_SELECT_BLOCK, NULL, 0);
        return;
    }

    /*typedef struct {
        n_chan_t *chan;
        void *msg_ptr;
    } scase;*/
    // 创建栈空间存储 scase 数据, send 放在前面，recv 放在后面
    type_t type_arr = type_array_new(TYPE_ANYPTR, cases_count * 2);
    // 实际上 c 语言不能接收一个数组作为参数，所以传递参数时需要转换为指针
    lir_operand_t *scase_target = temp_var_operand_with_alloc(m, type_arr);
    int64_t send_offset = 0;
    int64_t recv_offset = select_stmt->send_count * POINTER_SIZE * 2;

    // case_index -> ast_select_case
    int64_t *cases_map = mallocz(sizeof(int64_t) * cases_count);

    SLICE_FOR(select_stmt->cases) {
        ast_select_case_t *select_case = SLICE_VALUE(select_stmt->cases);
        if (select_case->is_default) {
            break;
        }

        int64_t casi = 0;

        lir_operand_t *recv_target;
        if (select_case->recv_var) {
            recv_target = linear_var_decl(m, select_case->recv_var);
            OP_PUSH(lir_op_nop_def(recv_target));
        }

        // 从 on_call 中提取 chan 参数 mov 到 scase_target 中
        ast_call_t *call = select_case->on_call;
        list_t *args = call->args;

        ast_expr_t *chan_expr = ct_list_value(args, 0);
        assert(chan_expr->type.kind == TYPE_CHAN);
        lir_operand_t *chan_target = linear_expr(m, *chan_expr, NULL);

        if (select_case->is_recv) {
            casi = recv_offset / (POINTER_SIZE * 2);

            lir_operand_t *dst_chan = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), scase_target, recv_offset);
            recv_offset += POINTER_SIZE;
            lir_operand_t *dst_msg_ptr = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), scase_target,
                                                               recv_offset);
            recv_offset += POINTER_SIZE;

            OP_PUSH(lir_op_move(dst_chan, chan_target));
            if (select_case->recv_var) {
                assert(recv_target);
                OP_PUSH(lir_op_move(dst_msg_ptr, lea_operand_pointer(m, recv_target)));
            } else {
                OP_PUSH(lir_op_move(dst_msg_ptr, integer_operand(0, TYPE_ANYPTR)));
            }
        } else {
            casi = send_offset / (POINTER_SIZE * 2);

            // send
            lir_operand_t *dst_chan = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), scase_target, send_offset);
            send_offset += POINTER_SIZE;
            OP_PUSH(lir_op_move(dst_chan, chan_target));

            // 从 args 第二个参数提取 msg target
            ast_expr_t *msg_expr = ct_list_value(args, 1);
            lir_operand_t *msg_target = linear_expr(m, *msg_expr, NULL);

            // 获取 msg 的 type, 以前剧场 chan_expr 定义的 element type
            lir_operand_t *dst_msg_ptr = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), scase_target,
                                                               send_offset);
            send_offset += POINTER_SIZE;

            OP_PUSH(lir_op_move(dst_msg_ptr, lea_operand_pointer(m, msg_target)));
        }

        cases_map[casi] = _i;
    }

    // int rt_chan_select(scase *cases, int16_t sends_count, int16_t recvs_count, bool _try)
    // select has default, _try = true
    lir_operand_t *chan_select_case_index = temp_var_operand(m, type_kind_new(TYPE_INT));
    lir_var_t *scase_var = scase_target->value;
    scase_var->type = type_kind_new(TYPE_ANYPTR);
    push_rt_call(m, RT_CALL_CHAN_SELECT, chan_select_case_index, 4, scase_target,
                 int16_operand(select_stmt->send_count),
                 int16_operand(select_stmt->recv_count), bool_operand(select_stmt->has_default));

    /*int i = rt_select()
	beq i != 1 -> select_case_1.end
	bl select_case_1
select_case_1:
	// handle body
	bl select_end
select_case_1.end:
	beq i != 2 -> select_case2.end
	bl select_case2
select_case_2:
	// handle body
	bl select_end
select_case_2.end:
	// default handle body
select_end:*/
    for (int i = 0; i < cases_count; ++i) {
        ast_select_case_t *select_case = select_stmt->cases->take[cases_map[i]];
        char *handle_start_ident = label_ident_with_unique(str_connect(select_start_ident, CASE_HANDLE));
        lir_op_t *handle_start = lir_op_label(handle_start_ident, true);
        lir_op_t *handle_end = lir_op_label(str_connect(handle_start_ident, LABEL_END_SUFFIX), true);

        // beq i != 1 -> select_case_1.end
        OP_PUSH(lir_op_new(LIR_OPCODE_BEE, chan_select_case_index, int_operand(i), handle_start->output));
        OP_PUSH(lir_op_bal(handle_end->output));
        OP_PUSH(handle_start);
        // handle body
        linear_body(m, select_case->handle_body);
        // bl select end
        OP_PUSH(lir_op_bal(lir_label_operand(select_end_ident, true)););
        OP_PUSH(handle_end);
    }

    // default case
    if (select_stmt->has_default) {
        ast_select_case_t *select_case = select_stmt->cases->take[select_stmt->cases->count - 1];
        linear_body(m, select_case->handle_body);
    }

    // handle end
    OP_PUSH(lir_op_label(select_end_ident, true));
}

/**
 * - 函数参数使用 param var 存储,按约定从左到右(code.result 为 param, code.first 为实参)
 * - code.operand 模仿 phi body 弄成列表的形式！
 * - 可能存在错误需要确认
 * @param c
 * @param expr
 * @return
 */
static lir_operand_t *linear_call(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_call_t *call = expr.value;
    lir_operand_t *fn_target = NULL;
    type_fn_t *type_fn = NULL;
    slice_t *args = slice_new();

    bool is_global_fn = false;

    if (call->left.assert_type == AST_EXPR_SELECT) {
        do {
            ast_expr_select_t *select = call->left.value;
            type_t select_left_type = select->left.type;
            if (select_left_type.ident_kind != TYPE_IDENT_INTERFACE) {
                break;
            }

            type_interface_t *interface_type = select_left_type.interface;

            // find index
            int index = -1;
            type_t *interface_fn_type = NULL;
            for (int i = 0; i < interface_type->elements->length; ++i) {
                type_t *element = ct_list_value(interface_type->elements, i);
                assert(element->kind == TYPE_FN);
                if (str_equal(element->fn->fn_name, select->key)) {
                    index = i;
                    interface_fn_type = element;
                    break;
                }
            }
            assert(index != -1); // infer 已经校验过了

            // linear select left get union target
            lir_operand_t *interface_target = linear_expr(m, select->left, NULL);

            /**
             * typedef struct {
             *   value_casting value;
             *   rtype_t *rtype;
             *   int64_t method_count;
             *   int64_t *methods; // methods
             * } n_union_t;
             */
            lir_operand_t *methods_target = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            lir_operand_t *src = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), interface_target, POINTER_SIZE);
            OP_PUSH(lir_op_move(methods_target, src));

            fn_target = temp_var_operand(m, *interface_fn_type); // fn target 可以直接调用
            src = indirect_addr_operand(m, *interface_fn_type, methods_target, POINTER_SIZE * index);
            OP_PUSH(lir_op_move(fn_target, src));
            type_fn = interface_fn_type->fn;

            // get self, 不需要进行额外的数据分配， union_casting 中已经进行了数据处理, 直接按照 anyptr 进行数据处理即可
            lir_operand_t *self_target_ptr = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            src = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), interface_target, 0);
            OP_PUSH(lir_op_move(self_target_ptr, src));
            slice_push(args, self_target_ptr);

            is_global_fn = true;
        } while (0);
    } else {
        // global ident call optimize to 'call symbol'
        fn_target = global_fn_symbol(m, call->left);
        if (!fn_target) {
            lir_operand_t *temp_operand = temp_var_operand_with_alloc(m, call->left.type);
            fn_target = linear_expr(m, call->left, temp_operand);
        } else {
            is_global_fn = true;
        }

        type_fn = call->left.type.fn;
    }

    assert(type_fn);
    assert(fn_target);


    // call 所有的参数都丢到 params 变量中
    for (int i = 0; i < type_fn->param_types->length; ++i) {
        if (type_fn->is_rest && i >= type_fn->param_types->length - 1) {
            // is_rest 超载情况处理
            type_t *rest_list_type = ct_list_value(type_fn->param_types, i);
            assertf(rest_list_type->kind == TYPE_VEC, "is_rest param must list type");

            // actual 的参数个数与 formal 的参数一致，并且 actual last type(must list) == formal last type 一致。
            if (call->args->length == type_fn->param_types->length && (call->args->length - 1) == i) {
                ast_expr_t *last_arg = ct_list_value(call->args, i);

                // last param
                if (type_compare(*rest_list_type, last_arg->type)) {
                    lir_operand_t *actual_operand = linear_expr(m, *last_arg, NULL);
                    slice_push(args, actual_operand);
                    break;
                }
            }

            // actual 剩余的所有参数进行 linear_expr 之后 都需要用一个数组收集起来，并写入到 target_operand 中
            int len = call->args->length - i; // 5, 1
            lir_operand_t *rest_vec_target = linear_unsafe_vec_new(m, *rest_list_type, len, NULL);
            type_t vec_element_type = rest_list_type->vec->element_type;

            int index = 0;
            for (int j = i; j < call->args->length; ++j) {
                ast_expr_t *arg = ct_list_value(call->args, j);
                lir_operand_t *rest_src_target = linear_expr(m, *arg, NULL);

                // no check
                lir_operand_t *index_target = int_operand(index);
                lir_operand_t *arg_dst_target = linear_inline_vec_element_addr_no_check(m, rest_vec_target,
                                                                                        index_target, vec_element_type);

                // 基于 vec_element_type 判断 assign 的 item 类型
                if (is_gc_alloc(vec_element_type.kind)) {
                    // TODO check barrier is enable
                    push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, arg_dst_target, rest_src_target);
                } else {
                    if (!is_stack_ref_big_type(vec_element_type)) {
                        arg_dst_target = indirect_addr_operand(m, vec_element_type, arg_dst_target, 0);
                    }

                    // 直接进行 mov
                    linear_super_move(m, vec_element_type, arg_dst_target, rest_src_target);
                }

                index += 1;
            }

            slice_push(args, rest_vec_target);
            break;
        }

        // 普通情况参数处理
        ast_expr_t *actual_expr = ct_list_value(call->args, i);
        lir_operand_t *actual_operand = linear_expr(m, *actual_expr, NULL);
        // call args 必须是 VAR 类型，避免 lower 阶段产生额外的 LEA 指令打断并行移动编号
        if (actual_operand->assert_type != LIR_OPERAND_VAR) {
            lir_operand_t *temp = temp_var_operand(m, actual_expr->type);
            OP_PUSH(lir_op_move(temp, actual_operand));
            actual_operand = temp;
        }
        slice_push(args, actual_operand);
    }


    // 如果 base target 是 global fn symbol
    if (!is_global_fn) {
        // change base target, mov [base_target], base_target
        lir_operand_t *new_fn_target = temp_var_operand(m, call->left.type);
        lir_operand_t *env_target = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));

        OP_PUSH(lir_op_move(env_target, indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), fn_target, 0)));
        OP_PUSH(lir_op_move(new_fn_target, indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), fn_target, POINTER_SIZE)));

        fn_target = new_fn_target;
        slice_push(args, env_target);
    }

    lir_operand_t *temp = NULL;
    if (call->return_type.kind != TYPE_VOID) {
        temp = temp_var_operand(m, call->return_type);
    }

    // call base_target,params -> target
    OP_PUSH(lir_op_new(LIR_OPCODE_CALL, fn_target, operand_new(LIR_OPERAND_ARGS, args), temp));

    // 目标函数可能会产生错误才需要进行错误判断，并跳转到对应的 error label 或者 continue label
    if (type_fn->is_errable) {
        linear_has_error(m);
    }

    if (temp) {
        return linear_super_move(m, expr.type, target, temp);
    }

    return target;
}

static lir_operand_t *linear_logical_or(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    assert(expr.type.kind == TYPE_BOOL);
    // 编译 left, 如果 left 为 true,则直接返回 true
    ast_binary_expr_t *logical_expr = expr.value;
    lir_operand_t *logic_end_operand = lir_label_operand(label_ident_with_unique(LOGICAL_OR_IDENT), true);

    // xxx left -> result
    lir_operand_t *left_src = linear_expr(m, logical_expr->left, NULL);
    linear_super_move(m, expr.type, target, left_src);

    // beq result,true -> logic_or_end
    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), target, logic_end_operand));

    // mov right -> result
    lir_operand_t *right_src = linear_expr(m, logical_expr->right, NULL);
    linear_super_move(m, expr.type, target, right_src);

    // logic_end:
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, logic_end_operand));
    return target;
}

static lir_operand_t *linear_logical_and(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    assert(expr.type.kind == TYPE_BOOL);

    // 编译 left, 如果 left 为 true,则直接返回 true
    ast_binary_expr_t *logical_expr = expr.value;

    lir_operand_t *logic_end_operand = lir_label_operand(label_ident_with_unique(LOGICAL_AND_IDENT), true);

    // xxx left -> result
    lir_operand_t *left_target = linear_expr(m, logical_expr->left, NULL);
    OP_PUSH(lir_op_move(target, left_target));

    // beq result,true -> logic_or_end
    OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(false), target, logic_end_operand));

    // mov right -> result
    lir_operand_t *right_target = linear_expr(m, logical_expr->right, NULL);
    OP_PUSH(lir_op_move(target, right_target));

    // logic_end:
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, logic_end_operand));
    return target;
}

static lir_operand_t *linear_binary(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_binary_expr_t *binary_expr = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // 特殊 binary 处理
    if (binary_expr->op == AST_OP_OR_OR) {
        return linear_logical_or(m, expr, target);
    }
    if (binary_expr->op == AST_OP_AND_AND) {
        return linear_logical_and(m, expr, target);
    }

    lir_operand_t *left_target = linear_expr(m, binary_expr->left, NULL);
    lir_operand_t *right_target = linear_expr(m, binary_expr->right, NULL);
    lir_opcode_t opcode = ast_op_convert[binary_expr->op];
    if (is_unsigned(binary_expr->left.type.kind)) {
        if (opcode == LIR_OPCODE_SSHR) {
            opcode = LIR_OPCODE_USHR;
        } else if (opcode == LIR_OPCODE_SDIV) {
            opcode = LIR_OPCODE_UDIV;
        } else if (opcode == LIR_OPCODE_SREM) {
            opcode = LIR_OPCODE_UREM;
        } else if (opcode == LIR_OPCODE_SLT) {
            opcode = LIR_OPCODE_USLT;
        } else if (opcode == LIR_OPCODE_SLE) {
            opcode = LIR_OPCODE_USLE;
        } else if (opcode == LIR_OPCODE_SGT) {
            opcode = LIR_OPCODE_USGT;
        } else if (opcode == LIR_OPCODE_SGE) {
            opcode = LIR_OPCODE_USGE;
        }
    }

    if (binary_expr->left.type.kind == TYPE_STRING && binary_expr->right.type.kind == TYPE_STRING) {
        switch (opcode) {
            case LIR_OPCODE_ADD: {
                push_rt_call(m, RT_CALL_STRING_CONCAT, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SEE: {
                push_rt_call(m, RT_CALL_STRING_EE, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SNE: {
                push_rt_call(m, RT_CALL_STRING_NE, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SGT: {
                push_rt_call(m, RT_CALL_STRING_GT, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SGE: {
                push_rt_call(m, RT_CALL_STRING_GE, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SLT: {
                push_rt_call(m, RT_CALL_STRING_LT, target, 2, left_target, right_target);
                break;
            }
            case LIR_OPCODE_SLE: {
                push_rt_call(m, RT_CALL_STRING_LE, target, 2, left_target, right_target);
                break;
            }
            default: {
                assertf(false, "not support string operator %d", ast_expr_op_str[binary_expr->op]);
            }
        }

        return target;
    }

    OP_PUSH(lir_op_new(opcode, left_target, right_target, target));

    return target;
}

/**
 * - (1 + 1)
 * NOT first_param => result_target
 * @param c
 * @param expr
 * @param result_target
 * @return
 */
static lir_operand_t *linear_unary(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_unary_expr_t *unary_expr = expr.value;

    lir_operand_t *first = linear_expr(m, unary_expr->operand, NULL);

    // 判断 first 的类型，如果是 imm 数，则直接对 int_value 取反，否则使用 lir minus  指令编译
    // !imm 为异常, parse 阶段已经识别了, [] 有可能
    if (unary_expr->op == AST_OP_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        assertf(is_number(imm->kind), "only number can neg operate");
        if (imm->kind == TYPE_INT) {
            imm->int_value = 0 - imm->int_value;
        } else {
            imm->f64_value = 0 - imm->f64_value;
        }

        return linear_super_move(m, expr.type, target, first);
    }

    if (unary_expr->op == AST_OP_NOT) {
        assert(unary_expr->operand.type.kind == TYPE_BOOL);
        if (first->assert_type == LIR_OPERAND_IMM) {
            lir_imm_t *imm = first->value;
            imm->bool_value = !imm->bool_value;
            return linear_super_move(m, expr.type, target, first);
        }

        if (!target) {
            target = temp_var_operand_with_alloc(m, expr.type);
        }

        // bool not to bit xor  !true = xor $1,true
        OP_PUSH(lir_op_new(LIR_OPCODE_XOR, first, bool_operand(true), target));
        return target;
    }


    if (unary_expr->op == AST_OP_SAFE_LA) {
        assert(!target);
        // target must ptr or anyptr or rawptr
        if (!target) {
            target = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
        }

        // not need handle analyze? only move? 需要做什么？ident 已经识别出来了。直接 move 到 target 好了
        if (is_stack_ref_big_type(unary_expr->operand.type) || unary_expr->operand.type.kind == TYPE_PTR) {
            OP_PUSH(lir_op_move(target, first));
            return target;
        }

        // 如果 first->assert_type 不是 LIR_OPERAND_INDIRECT_ADDR, 则可能是 call/as/literal 表达式所产生的 var, 此时直接进行堆分配
        if (first->assert_type != LIR_OPERAND_INDIRECT_ADDR) {
            lir_operand_t *temp_operand = temp_var_operand(m, type_ptrof(unary_expr->operand.type));
            int64_t rtype_hash = type_hash(unary_expr->operand.type);
            push_rt_call(m, RT_CALL_GC_MALLOC, temp_operand, 1, int_operand(rtype_hash));
            lir_operand_t *dst = indirect_addr_operand(m, unary_expr->operand.type, temp_operand, 0);
            OP_PUSH(lir_op_move(dst, first));

            return temp_operand;
        }


        lir_operand_t *src_operand;
        if (first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
            lir_indirect_addr_t *indirect_addr = first->value;
            if (indirect_addr->offset == 0) {
                // indirect addr base 就是指针本身
                src_operand = indirect_addr->base;
            } else {
                src_operand = lea_operand_pointer(m, first);
            }
        } else {
            src_operand = lea_operand_pointer(m, first);
        }

        return linear_super_move(m, type_kind_new(TYPE_ANYPTR), target, src_operand);
    }

    // neg source -> target
    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // &var, 指针引用可能会造成内存逃逸，所以需要特殊处理 TODO 当前版本不存在隐式的逃逸处理，需要显式的调用 sla 才会触发逃逸处理。
    if (unary_expr->op == AST_OP_LA || unary_expr->op == AST_OP_UNSAFE_LA) {
        // 如果是 stack_type, 则直接移动到 target 即可，src 中存放的已经是一个栈指针了，没有必要再 lea 了
        if (is_stack_ref_big_type(unary_expr->operand.type)) {
            // 必须 move target，这同时也是一个类型转换的过程
            OP_PUSH(lir_op_move(target, first));
            return target;
        }

        if (unary_expr->op == AST_OP_UNSAFE_LA && unary_expr->operand.type.kind == TYPE_PTR) {
            OP_PUSH(lir_op_move(target, first));
            return target;
        }

        // super_move 只有 target == null, 此时直接返回 first
        lir_operand_t *src_operand;
        if (first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
            lir_indirect_addr_t *indirect_addr = first->value;
            if (indirect_addr->offset == 0) {
                // indirect addr base 就是指针本身
                src_operand = indirect_addr->base;
            } else {
                src_operand = lea_operand_pointer(m, first);
            }
        } else {
            src_operand = lea_operand_pointer(m, first);
        }


        return linear_super_move(m, expr.type, target, src_operand);
    }


    // *var
    // int a = *b
    // vec<2> a = *b
    // person_t a = *b
    // 所以 target 真的有足够的空间么？target 默认就是 ptr， 无论是不是超过 8byte!
    // first 是个 ptr
    if (unary_expr->op == AST_OP_IA) {
        // checking TODO 性能问题，暂时不启用
        //        if (unary_expr->operand.type.kind == TYPE_RAWPTR) {
        //            push_rt_call(m, RT_CALL_RAWPTR_VALID, NULL, 1, first);
        //            linear_has_panic(m);
        //        }

        if (!target) {
            target = temp_var_operand_with_alloc(m, expr.type);
        }

        // indirect addr first, and mov
        lir_operand_t *src = first;
        if (!is_stack_ref_big_type(expr.type)) {
            src = indirect_addr_operand(m, expr.type, first, 0);
        }

        return linear_super_move(m, expr.type, target, src);
    }

    lir_opcode_t type = ast_op_convert[unary_expr->op];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    OP_PUSH(unary);

    return target;
}

static lir_operand_t *linear_vec_slice(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_vec_slice_t *ast = expr.value;

    lir_operand_t *vec_target = linear_expr(m, ast->left, NULL);
    lir_operand_t *start_target = linear_expr(m, ast->start, NULL);
    lir_operand_t *end_target = linear_expr(m, ast->end, NULL);

    if (target == NULL) {
        target = temp_var_operand_with_alloc(m, ast->left.type);
    }
    push_rt_call(m, RT_CALL_VEC_SLICE, target, 3, vec_target, start_target, end_target);

    linear_has_panic(m);

    return target;
}

/**
 * int a = list[0]
 * string s = list[1]
 */
static lir_operand_t *linear_vec_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_vec_access_t *ast = expr.value;

    lir_operand_t *vec_target = linear_expr(m, ast->left, NULL);
    lir_operand_t *index_target = linear_expr(m, ast->index, NULL);

    lir_operand_t *src = linear_inline_vec_element_addr(m, vec_target, index_target, ast->element_type);

    if (!is_stack_ref_big_type(expr.type)) {
        src = indirect_addr_operand(m, expr.type, src, 0);
    } else {
        if (target == NULL) {
            //            target = temp_var_operand_with_alloc(m, expr.type);
            assert(src->assert_type == LIR_OPERAND_VAR);
            lir_var_t *src_var = src->value;
            src_var->type = type_copy(m, expr.type);
        }
    }

    // bug: probindex[0].index = 1, 所以 target 应该由外部控制，如果没有 target 就返回引用的指针

    // 如果此时 list 发生了 grow, 则该地址会变成一个无效的脏地址，比如 grow(list).foo = list[1]
    // 所以对于 struct 的 access,这里的 target 不应该为 null
    return linear_super_move(m, expr.type, target, src);
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
 * @param vec_target
 * @return
 */
static lir_operand_t *linear_vec_new(module_t *m, ast_expr_t expr, lir_operand_t *vec_target) {
    ast_vec_new_t *ast = expr.value;
    type_t t = expr.type;

    vec_target = linear_unsafe_vec_new(m, t, ast->elements->length, vec_target);
    type_t vec_element_type = t.vec->element_type;

    if (ast->elements) {
        for (int i = 0; i < ast->elements->length; ++i) {
            ast_expr_t *item_expr = ct_list_value(ast->elements, i);

            lir_operand_t *index_target = int_operand(i);

            lir_operand_t *item_dst_target = linear_inline_vec_element_addr_no_check(m, vec_target, index_target,
                                                                                     vec_element_type);

            lir_operand_t *item_src_target = linear_expr(m, *item_expr, NULL);

            // 基于 vec_element_type 判断 assign 的 item 类型
            if (is_gc_alloc(vec_element_type.kind)) {
                // TODO check barrier is enable
                push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, item_dst_target, item_src_target);
            } else {
                if (!is_stack_ref_big_type(vec_element_type)) {
                    item_dst_target = indirect_addr_operand(m, vec_element_type, item_dst_target, 0);
                }

                // 直接进行 mov
                linear_super_move(m, vec_element_type, item_dst_target, item_src_target);
            }
        }
    }

    return vec_target;
}

/**
 * int a = list[0]
 * list[0] = a
 *
 * target 不存在时表明需要地址引用, 比如 array_assign 中就调用了 array_access
 */
static lir_operand_t *linear_array_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_array_access_t *ast = expr.value;

    lir_operand_t *array_target = linear_expr(m, ast->left, NULL);
    lir_operand_t *index_target = linear_expr(m, ast->index, NULL);

    if (array_target->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        assert(false);
        // linear_ident 处理了 global symbol var, 不会再有 global symbol var 返回了
        // 加载 array symbol 的地址到 array_target_ref 中
        // 这里明确 symbol 是 array 所以才能这么做
        //        OP_PUSH(lir_op_lea(array_target_ref, array_target));
    }

    // 自动解构
    type_t arr_type = ast->left.type;
    if (ast->left.type.kind == TYPE_PTR || ast->left.type.kind == TYPE_RAWPTR) {
        arr_type = ast->left.type.ptr->value_type;
    }
    // item_targe 的类型是 ptr, 指向了 element_addr,
    lir_operand_t *item_target = linear_inline_arr_element_addr(m, array_target, index_target, arr_type);

    //    push_rt_call(m, RT_CALL_ARRAY_ELEMENT_ADDR, item_target, 3, array_target, int_operand(rtype_hash),
    //                 index_target);

    if (!is_stack_ref_big_type(expr.type)) {
        item_target = indirect_addr_operand(m, expr.type, item_target, 0);
    } else {
        if (target == NULL) {
            //            target = temp_var_operand_with_alloc(m, expr.type);
            assert(item_target->assert_type == LIR_OPERAND_VAR);
            lir_var_t *item_var = item_target->value;
            item_var->type = type_copy(m, expr.type);
        }
    }

    // 如果此时 list 发生了 grow, 则该地址会变成一个无效的脏地址，比如 grow(list).foo = list[1]
    // 所以对于 struct 的 access,这里的 target 不应该为 null
    return linear_super_move(m, expr.type, target, item_target);
}

/**
 * 采用和 struct 一样的在栈上分配的方式,  target 是一个 var，其中保存了栈上的指针
 * 最终返回值是一个中转变量，其指向了 arr 所在的内存区域。
 * @param m
 * @param expr
 * @param target
 * @return
 */
static lir_operand_t *linear_array_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_array_new_t *ast = expr.value;
    type_t type_array = expr.type;

    if (!target) {
        target = temp_var_operand_with_alloc(m, type_array);
    }

    bool arr_in_heap = type_array.in_heap;
    assert(target && target->assert_type == LIR_OPERAND_VAR);

    uint64_t rtype_hash = type_hash(type_array);

    lir_operand_t *target_ref = temp_var_operand(m, type_ptrof(type_array));
    OP_PUSH(lir_op_move(target_ref, target));

    int64_t offset = 0;
    // 无论 array 分配在栈还是堆，都需要进行数据初始化，当然有一些例外情况可以进行优化
    for (int i = 0; i < type_array.array->length; ++i) {
        lir_operand_t *item_target = linear_inline_arr_element_addr_not_check(m, target_ref, int_operand(i),
                                                                              type_array);

        if (!is_stack_ref_big_type(type_array.array->element_type)) {
            item_target = indirect_addr_operand(m, type_array.array->element_type, item_target, 0);
        }

        if (i < ast->elements->length) {
            ast_expr_t *item_expr = ct_list_value(ast->elements, i);
            linear_expr(m, *item_expr, item_target);
        } else {
            // in stack
            if (arr_in_heap && is_scala_type(type_array.array->element_type)) {
                // default is zero, not need set default value
            } else {
                linear_default_operand(m, type_array.array->element_type, item_target);
            }
        }
    }

    return target;
}

static lir_operand_t *linear_array_repeat_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_array_repeat_new_t *ast = expr.value;
    type_t type_array = expr.type;

    if (!target) {
        target = temp_var_operand_with_alloc(m, type_array);
    }

    assert(target && target->assert_type == LIR_OPERAND_VAR);

    uint64_t rtype_hash = type_hash(type_array);

    lir_operand_t *target_ref = temp_var_operand(m, type_ptrof(type_array));
    OP_PUSH(lir_op_move(target_ref, target));

    lir_operand_t *default_element_target = linear_expr(m, ast->default_element, NULL);

    int64_t offset = 0;
    for (int i = 0; i < type_array.array->length; ++i) {
        lir_operand_t *item_target = linear_inline_arr_element_addr_not_check(m, target_ref, int_operand(i),
                                                                              type_array);
        if (!is_stack_ref_big_type(type_array.array->element_type)) {
            item_target = indirect_addr_operand(m, type_array.array->element_type, item_target, 0);
        }

        linear_super_move(m, type_array.array->element_type, item_target, default_element_target);
    }

    return target;
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
static lir_operand_t *linear_env_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    assert(expr.type.in_heap);

    ast_env_access_t *ast = expr.value;
    lir_operand_t *index = int_operand(ast->index);
    uint64_t size = type_sizeof(expr.type);

    lir_operand_t *values_operand = linear_inline_env_values(m);

    // move [values+x] -> dst_ptr (values 中的值已经取了出来，丢到了 dst_ptr 里面)
    lir_operand_t *src_ptr = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), values_operand, ast->index * POINTER_SIZE);

    // envs 中总是存储的堆指针，即使是 scala 类型的数据, 所以此处需要进一步 indirect 获取对应的值，而不是 ptr value
    if (is_scala_type(expr.type)) {
        src_ptr = indirect_addr_operand(m, expr.type, src_ptr, 0);
    }

    if (is_stack_type(expr.type.kind) && expr.type.in_heap && !target) {
        return src_ptr;
    }


    // 把 values 中存储的值取出来，放在 target 中
    if (!target) {
        target = temp_var_operand(m, expr.type);
    }

    // 无论是 struct/arr 还是 string/vec/ptr 等都直接进行 move, 而不是 support move
    OP_PUSH(lir_op_move(target, src_ptr));
    return target;
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
static lir_operand_t *linear_map_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_map_access_t *ast = expr.value;

    // linear base address left_target
    lir_operand_t *map_target = linear_expr(m, ast->left, NULL);

    // linear key to temp var
    lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, ast->key, NULL));

    lir_operand_t *value_target = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    push_rt_call(m, RT_CALL_MAP_ACCESS, value_target, 2, map_target, key_ref);
    if (!is_stack_ref_big_type(expr.type)) {
        value_target = indirect_addr_operand(m, expr.type, value_target, 0);
    }

    linear_has_panic(m);

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    return linear_super_move(m, expr.type, target, value_target);
}

/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *linear_set_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_set_new_t *ast = expr.value;
    type_t t = expr.type;

    target = linear_default_set(m, t, target);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_map_element_t *element = ct_list_value(ast->elements, i);
        ast_expr_t key_expr = element->key;
        lir_operand_t *key_target = linear_expr(m, key_expr, NULL);
        lir_operand_t *key_ref = lea_operand_pointer(m, key_target);
        push_rt_call(m, RT_CALL_SET_ADD, NULL, 2, target, key_ref);
    }

    return target;
}

/**
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
static lir_operand_t *linear_map_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_map_new_t *ast = expr.value;
    type_t map_type = expr.type;

    target = linear_default_map(m, map_type, target);

    // 默认值初始化 rt_call map_assign
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_map_element_t *element = ct_list_value(ast->elements, i);
        ast_expr_t key_expr = element->key;
        lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, key_expr, NULL));

        lir_operand_t *value_ptr_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_ANYPTR));
        push_rt_call(m, RT_CALL_MAP_ASSIGN, value_ptr_target, 2, target, key_ref);

        if (!is_stack_ref_big_type(map_type.map->value_type)) {
            value_ptr_target = indirect_addr_operand(m, map_type.map->value_type, value_ptr_target, 0);
        }

        linear_expr(m, element->value, value_ptr_target);
    }

    return target;
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_struct_select(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_struct_select_t *ast = expr.value;

    lir_operand_t *struct_target = linear_expr(m, ast->instance, NULL);
    type_t type_struct = ast->instance.type;
    if (is_struct_ptr(type_struct)) {
        type_struct = type_struct.ptr->value_type;
    } else if (is_struct_rawptr(type_struct)) {
        type_struct = type_struct.ptr->value_type;
        // TODO inline 校验, 校验失败调用 panic
        //        push_rt_call(m, RT_CALL_RAWPTR_VALID, NULL, 1, struct_target);
        //        linear_has_panic(m);
    }

    assert(type_struct.kind == TYPE_STRUCT);

    uint64_t offset = type_struct_offset(type_struct.struct_, ast->key);

    // 如果 target 存在则进行数据 copy, 否则优先返回引用 operand
    // 对于 scala type 返回 [struct_target+offset|scala type]
    // 对于 big type 使用 lea 加载指针地址，并返回对应的指针类型

    // 先找到存放地址(可以用 indirect addr 算出来, 也可以直接用加法算出来？)
    // 总之先找到存放数据的 addr(这里直接计算出了)
    lir_operand_t *src = indirect_addr_operand(m, expr.type, struct_target, offset);

    if (is_stack_ref_big_type(expr.type)) {
        src = lea_operand_pointer(m, src);
        assert(src->assert_type == LIR_OPERAND_VAR);
        lir_var_t *src_var = src->value;

        // 调整为 expr.type
        src_var->type = type_copy(m, expr.type);
    }

    return linear_super_move(m, expr.type, target, src);
}

/**
 * mov [base+slot,n] => target
 * bar().baz
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_tuple_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_tuple_access_t *ast = expr.value;

    // may be symbol_var
    lir_operand_t *tuple_target = linear_expr(m, ast->left, NULL);
    type_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->element_type);
    uint64_t offset = type_tuple_offset(t.tuple, ast->index);

    lir_operand_t *src = indirect_addr_operand(m, ast->element_type, tuple_target, offset);

    // 如果是 struct/arr 的话，src 中存储的应该是指向的 addr, 从而方便 super move
    if (is_stack_ref_big_type(ast->element_type)) {
        src = lea_operand_pointer(m, src);
        assert(src->assert_type == LIR_OPERAND_VAR);
        lir_var_t *src_var = src->value;

        // 调整为 expr.type
        src_var->type = type_copy(m, expr.type);
    }

    return linear_super_move(m, ast->element_type, target, src);
}

/**
 * struct_new 初始化时还无法判断当前 struct 是否会出现逃逸行为，所以无法判断应该在栈上还是堆上分配
 *
 * foo.bar = 1
 *
 * person baz = person {
 *  age = 100
 *  sex = true
 * }
 *
 * var a = (person{age=1}).age
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_struct_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_struct_new_t *ast = expr.value;
    type_t t = expr.type;

    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    assert(target && target->assert_type == LIR_OPERAND_VAR);


    // 快速赋值,由于 struct 的相关属性都存储在 type 中，所以偏移量等值都需要在前端完成计算
    table_t *exists = table_new();
    for (int i = 0; i < ast->properties->length; ++i) {
        struct_property_t *p = ct_list_value(ast->properties, i);

        table_set(exists, p->name, p);

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t offset = type_struct_offset(t.struct_, p->name);

        assertf(p->right, "struct new property_expr value empty");
        ast_expr_t *property_expr = p->right;

        lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
        if (is_stack_ref_big_type(p->type)) {
            // foo.bar = person {}
            dst = lea_operand_pointer(m, dst);
        }

        linear_expr(m, *property_expr, dst);
    }

    linear_struct_fill_default(m, t, target, exists);

    return target;
}

/**
 * var a = (1, a, 1.25)
 * @param c
 * @param ast
 * @param target
 * @return
 */
static lir_operand_t *linear_tuple_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_tuple_new_t *ast = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // tuple new 时所有的值都必须进行初始化，所以不会出现 null 值
    uint64_t rtype_hash = type_hash(expr.type);
    push_rt_call(m, RT_CALL_TUPLE_NEW, target, 1, int_operand(rtype_hash));

    uint64_t offset = 0;
    for (int i = 0; i < ast->elements->length; ++i) {
        ast_expr_t *element = ct_list_value(ast->elements, i);

        uint64_t element_size = type_sizeof(element->type);
        int element_align = type_alignof(element->type);

        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align_up(offset, element_align);

        // 基于 target 计算 addr
        lir_operand_t *dst = indirect_addr_operand(m, element->type, target, offset);
        if (is_stack_ref_big_type(element->type)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_expr(m, *element, dst);

        offset += element_size;
    }

    return target;
}

/**
 * 生成 tagged enum 变体构造的代码
 * 实现方式：创建 payload 数据，然后用 union casting 包装
 */
static lir_operand_t *linear_tagged_union_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_tagged_union_t *tagged_new = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }


    int64_t tag_hash = hash_string(tagged_new->tagged_name);
    lir_operand_t *payload_ptr = int_operand(0);
    int64_t payload_type_hash = 0;

    // maybe null
    type_t payload_type = tagged_new->element->type;
    if (payload_type.kind != TYPE_VOID) {
        assert(tagged_new->arg);
        lir_operand_t *payload_operand = linear_expr(m, *tagged_new->arg, NULL);
        if (is_stack_ref_big_type(payload_type)) {
            payload_ptr = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_move(payload_ptr, payload_operand));
        } else {
            payload_ptr = lea_operand_pointer(m, payload_operand);
        }

        payload_type_hash = type_hash(payload_type);
    }

    push_rt_call(m, RT_CALL_TAGGED_UNION_CASTING, target, 3, int_operand(tag_hash), int_operand(payload_type_hash), payload_ptr);
    return target;
}

static lir_operand_t *linear_new_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    // 调用 runtime_malloc 进行内存申请，并将申请的结果返回，其中返回的类型是一个 pointer 结构
    if (!target) {
        target = temp_var_operand(m, expr.type); // 必定是一个指针类型
    }

    ast_new_expr_t *new_expr = expr.value;

    uint64_t rtype_hash = type_hash(new_expr->type);
    push_rt_call(m, RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash));

    if (new_expr->type.kind == TYPE_STRUCT) {
        // 默认值处理
        type_struct_t *type_struct = new_expr->type.struct_;
        table_t *exists = table_new();
        for (int i = 0; i < new_expr->properties->length; ++i) {
            struct_property_t *p = ct_list_value(new_expr->properties, i);

            table_set(exists, p->name, p);
            // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
            uint64_t offset = type_struct_offset(type_struct, p->name);

            assertf(p->right, "struct new property_expr value empty");
            ast_expr_t *property_expr = p->right;

            lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
            if (is_stack_ref_big_type(p->type)) {
                // foo.bar = person {}
                dst = lea_operand_pointer(m, dst);
            }

            linear_expr(m, *property_expr, dst);
        }
        linear_struct_fill_default(m, new_expr->type, target, exists);
    } else {
        if (new_expr->default_expr) {
            lir_operand_t *src = linear_expr(m, *new_expr->default_expr, NULL);
            lir_operand_t *dst;
            if (is_stack_ref_big_type(new_expr->type)) {
                // handle arr
                dst = target;
            } else {
                // indirect ptr operand
                dst = indirect_addr_operand(m, new_expr->type, target, 0);
            }

            linear_super_move(m, new_expr->type, dst, src);
        }
    }


    return target;
}

static lir_operand_t *linear_default_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    ast_macro_default_expr_t *macro_default_expr = expr.value;
    return linear_default_operand(m, macro_default_expr->target_type, target);
}

static lir_operand_t *linear_sizeof_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_macro_sizeof_expr_t *ast = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    uint64_t size = type_sizeof(ast->target_type);

    OP_PUSH(lir_op_move(target, int_operand(size)));
    return target;
}

static lir_operand_t *linear_macro_async(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    assert(target == NULL);
    ast_macro_async_t *async_expr = expr.value;

    lir_operand_t *flag_operand = linear_expr(m, *async_expr->flag_expr, NULL);

    if (async_expr->origin_call->left.assert_type == AST_EXPR_IDENT) {
        ast_ident *call_ident = async_expr->origin_call->left.value;
        // check symbol is fn symbol
        symbol_t *s = symbol_table_get(call_ident->literal);
        assertf(s, "ident %s not declare");
        if (s->type == SYMBOL_FN) {
            lir_operand_t *fn_addr_operand = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_lea(fn_addr_operand, symbol_label_operand(m, s->ident)));

            // rt_call
            push_rt_call(m, RT_CALL_COROUTINE_ASYNC2, NULL, 3, fn_addr_operand, flag_operand, bool_operand(true));
        } else {
            lir_operand_t *fn_operand = linear_expr(m, async_expr->origin_call->left, NULL);
            push_rt_call(m, RT_CALL_COROUTINE_ASYNC2, NULL, 3, fn_operand, flag_operand, bool_operand(false));
        }
    } else {
        lir_operand_t *fn_operand = linear_expr(m, async_expr->origin_call->left, NULL);
        push_rt_call(m, RT_CALL_COROUTINE_ASYNC2, NULL, 3, fn_operand, flag_operand, bool_operand(false));
    }

    // is ident and global symbol
    // direct call rt_coroutine_async, flag set direct
    //    push_rt_call(m, )


    return NULL;
}

static lir_operand_t *linear_reflect_hash_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_macro_sizeof_expr_t *ast = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    OP_PUSH(lir_op_move(target, int_operand(type_hash(ast->target_type))));
    return target;
}

static lir_operand_t *linear_is_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_is_expr_t *is_expr = expr.value;
    assert(is_expr->src->type.kind == TYPE_UNION ||
           is_expr->src->type.kind == TYPE_RAWPTR ||
           is_expr->src->type.kind == TYPE_TAGGED_UNION ||
           is_expr->src->type.kind == TYPE_INTERFACE);

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    lir_operand_t *src_operand = linear_expr(m, *is_expr->src, NULL);


    if (is_expr->src->type.kind == TYPE_RAWPTR) {
        // is target 只能只能判断是否为 null
        LINEAR_ASSERTF(is_expr->target_type.kind == TYPE_NULL,
                       "%s is only support null, example: %s is null", type_format(is_expr->src->type),
                       type_format(is_expr->src->type));

        OP_PUSH(lir_op_new(LIR_OPCODE_SEE, src_operand, int_operand(0), target));
        return target;
    }

    if (is_expr->src->type.kind == TYPE_TAGGED_UNION) {
        assert(is_expr->union_tag);
        ast_tagged_union_t *tagged_union = is_expr->union_tag->value;
        lir_operand_t *expected_hash = int_operand(hash_string(tagged_union->tagged_name));
        lir_operand_t *actual_hash = indirect_addr_operand(m, type_kind_new(TYPE_INT64), src_operand, QWORD);
        OP_PUSH(lir_op_new(LIR_OPCODE_SEE, expected_hash, actual_hash, target));
        return target;
    }

    uint64_t target_rtype_hash = type_hash(is_expr->target_type);
    if (is_expr->src->type.kind == TYPE_INTERFACE) {
        push_rt_call(m, RT_CALL_INTERFACE_IS, target, 2, src_operand, int_operand(target_rtype_hash));
    } else {
        push_rt_call(m, RT_CALL_UNION_IS, target, 2, src_operand, int_operand(target_rtype_hash));
    }

    return target;
}

/**
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_as_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_as_expr_t *as_expr = expr.value;
    lir_operand_t *src_operand = linear_expr(m, as_expr->src, NULL);

    // 如果 src 和 dst 类型一致，则不需要做任何的处理
    if (type_compare(as_expr->src.type, as_expr->target_type)) {
        return linear_super_move(m, expr.type, target, src_operand);
    }

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    uint64_t src_rtype_hash = type_hash(as_expr->src.type);
    uint64_t target_size = type_sizeof(as_expr->target_type);
    uint64_t src_size = type_sizeof(as_expr->src.type);

    // 数值类型转换
    if (is_integer(as_expr->target_type.kind) && is_integer(as_expr->src.type.kind)) {
        if (target_size > src_size) {
            if (is_unsigned(as_expr->src.type.kind)) {
                OP_PUSH(lir_op_uext(target, src_operand));
            } else {
                OP_PUSH(lir_op_sext(target, src_operand));
            }
        } else if (target_size < src_size) {
            OP_PUSH(lir_op_trunc(target, src_operand));
        } else {
            OP_PUSH(lir_op_move(target, src_operand));
        }

        return target;
    } else if (is_float(as_expr->target_type.kind) && is_float(as_expr->src.type.kind)) {
        if (target_size > src_size) {
            OP_PUSH(lir_op_new(LIR_OPCODE_FEXT, src_operand, NULL, target));
        } else if (target_size < src_size) {
            OP_PUSH(lir_op_new(LIR_OPCODE_FTRUNC, src_operand, NULL, target));
        } else {
            OP_PUSH(lir_op_move(target, src_operand));
        }
        return target;
    } else if (is_float(as_expr->target_type.kind) && is_unsigned(as_expr->src.type.kind)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_UITOF, src_operand, NULL, target));
        return target;
    } else if (is_float(as_expr->target_type.kind) && is_integer(as_expr->src.type.kind)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_SITOF, src_operand, NULL, target));
        return target;
    } else if (is_unsigned(as_expr->target_type.kind) && is_float(as_expr->src.type.kind)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_FTOUI, src_operand, NULL, target));
        return target;
    } else if (is_signed(as_expr->target_type.kind) && is_float(as_expr->src.type.kind)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_FTOSI, src_operand, NULL, target));
        return target;
    }

    // single type to union type
    if (as_expr->target_type.kind == TYPE_UNION) {
        assert(as_expr->src.type.kind != TYPE_UNION); // in infer casting
        lir_operand_t *union_value;
        if (is_stack_ref_big_type(as_expr->src.type)) {
            union_value = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_move(union_value, src_operand)); // mov pointer
        } else {
            union_value = lea_operand_pointer(m, src_operand);
        }

        type_union_t *union_type = as_expr->target_type.union_;

        push_rt_call(m, RT_CALL_UNION_CASTING, target, 2, int_operand(src_rtype_hash), union_value);
        return target;
    }

    // union assert
    if (as_expr->src.type.kind == TYPE_UNION) {
        assert(as_expr->target_type.kind != TYPE_UNION);
        OP_PUSH(lir_op_nop_def(target));
        lir_operand_t *output_ref = lea_operand_pointer(m, target);
        uint64_t target_rtype_hash = type_hash(as_expr->target_type);
        push_rt_call(m, RT_CALL_UNION_ASSERT, NULL, 3, src_operand, int_operand(target_rtype_hash), output_ref);
        linear_has_panic(m);
        return target;
    }

    if (as_expr->src.type.kind == TYPE_TAGGED_UNION) {
        lir_operand_t *src = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), src_operand, 0);
        OP_PUSH(lir_op_move(target, src));
        return target;
    }

    // interface as
    if (as_expr->src.type.kind == TYPE_INTERFACE) {
        assert(as_expr->target_type.kind != TYPE_INTERFACE);
        OP_PUSH(lir_op_nop_def(target));

        lir_operand_t *output_ref = target;
        if (!is_stack_ref_big_type(as_expr->target_type)) {
            output_ref = lea_operand_pointer(m, target);
        }

        uint64_t target_rtype_hash = type_hash(as_expr->target_type);
        push_rt_call(m, RT_CALL_INTERFACE_ASSERT, NULL, 3, src_operand, int_operand(target_rtype_hash), output_ref);
        linear_has_panic(m);
        return target;
    }

    if (as_expr->target_type.kind == TYPE_INTERFACE) {
        lir_operand_t *interface_value;
        if (is_stack_ref_big_type(as_expr->src.type)) {
            interface_value = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
            OP_PUSH(lir_op_move(interface_value, src_operand));
        } else {
            interface_value = lea_operand_pointer(m, src_operand);
        }

        type_interface_t *interface_type = as_expr->target_type.interface;

        type_t src_type = as_expr->src.type;
        if (src_type.ident_kind != TYPE_IDENT_DEF && (src_type.kind == TYPE_PTR || src_type.kind == TYPE_RAWPTR)) {
            src_type = src_type.ptr->value_type;
        }

        assert(src_type.ident_kind == TYPE_IDENT_DEF);

        // find ident
        symbol_t *s = symbol_table_get(src_type.ident);
        assert(s && s->type == SYMBOL_TYPE);
        ast_typedef_stmt_t *typedef_stmt = s->ast_value;
        assert(!typedef_stmt->is_interface);

        if (interface_type->elements->length > 0) {
            // alloc stack
            type_t methods_type_arr = type_array_new(TYPE_ANYPTR, interface_type->elements->length);
            lir_operand_t *methods_target = temp_var_operand_with_alloc(m, methods_type_arr);

            // fn label to stack by interface fn sequence
            for (int i = 0; i < interface_type->elements->length; ++i) {
                type_t *temp = ct_list_value(interface_type->elements, i);
                assert(temp->kind == TYPE_FN);
                type_fn_t *interface_fn_type = temp->fn;
                assert(interface_fn_type->fn_name);

                // 按照 union type 中的定义顺序写入
                char *fn_ident = str_connect_by(src_type.ident, interface_fn_type->fn_name, IMPL_CONNECT_IDENT);

                ast_fndef_t *ast_fndef = sc_map_get_sv(&typedef_stmt->method_table, fn_ident);
                assert(ast_fndef);
                symbol_t *fn_symbol = symbol_table_get(fn_ident);
                assert(fn_symbol);

                if (ast_fndef->receiver_wrapper) {
                    symbol_t *wrapper_fn_symbol = symbol_table_get(ast_fndef->receiver_wrapper->symbol_name);
                    assert(wrapper_fn_symbol);

                    ast_fndef = ast_fndef->receiver_wrapper;
                    fn_ident = ast_fndef->symbol_name;
                }

                assert(ast_fndef->self_kind != PARAM_SELF_T);
                if (ast_fndef->linkid) {
                    fn_ident = ast_fndef->linkid;
                }

                // lea fn_label to stack
                lir_operand_t *fn_label = lir_label_operand(fn_ident, false);
                // methods 需要通过 c 传递，为了让其符合 c 的 abi, 这里需要将其转换位 anyptr 进行传递
                lir_var_t *var = methods_target->value;
                var->type = type_kind_new(TYPE_ANYPTR);
                lir_operand_t *item_target = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), methods_target,
                                                                   i * POINTER_SIZE);
                OP_PUSH(lir_op_lea(item_target, fn_label));
            }

            push_rt_call(m, RT_CALL_INTERFACE_CASTING, target, 4, int_operand(src_rtype_hash), interface_value,
                         int_operand(interface_type->elements->length),
                         methods_target);

        } else {
            push_rt_call(m, RT_CALL_INTERFACE_CASTING, target, 4, int_operand(src_rtype_hash), interface_value,
                         int_operand(0),
                         int_operand(0));
        }

        return target;
    }

    // nullable pointer assert
    if (as_expr->src.type.kind == TYPE_RAWPTR) {
        // rawptr<T> as anyptr
        if (as_expr->target_type.kind == TYPE_ANYPTR) {
            OP_PUSH(lir_op_move(target, src_operand));
            return target;
        }

        // rawptr<T> as ptr<T>
        assert(as_expr->target_type.kind == TYPE_PTR);
        push_rt_call(m, RT_CALL_RAWPTR_ASSERT, target, 1, src_operand);
        linear_has_panic(m);
        return target;
    }

    // string -> list u8
    if (as_expr->src.type.kind == TYPE_STRING && is_vec_u8(as_expr->target_type)) {
        // OP_PUSH(lir_op_move(target, src_operand));
        push_rt_call(m, RT_CALL_STRING_TO_VEC, target, 1, src_operand);
        return target;
    }

    // list u8 -> string
    if (is_vec_u8(as_expr->src.type) && as_expr->target_type.kind == TYPE_STRING) {
        //        OP_PUSH(lir_op_move(target, src_operand));
        push_rt_call(m, RT_CALL_VEC_TO_STRING, target, 1, src_operand);
        return target;
    }

    if (as_expr->src.type.kind == TYPE_ENUM && is_integer(as_expr->target_type.kind)) {
        OP_PUSH(lir_op_move(target, src_operand));
        return target;
    }

    // anybody to anyptr
    if (as_expr->target_type.kind == TYPE_ANYPTR) {
        // 如果类型长度匹配直接进行 mov 即可, arr/struct 则直接 move point 即可
        if (!is_stack_ref_big_type(as_expr->src.type) && type_sizeof(as_expr->src.type) < POINTER_SIZE) {
            push_rt_call(m, RT_CALL_ANYPTR_CASTING, target, 1, src_operand);
        } else {
            OP_PUSH(lir_op_move(target, src_operand));
        }
        return target;
    }

    // anyptr to anybody without float
    if (as_expr->src.type.kind == TYPE_ANYPTR) {
        assertf(as_expr->src.type.kind != TYPE_FLOAT64, "anyptr cannot casting to float");
        assertf(as_expr->src.type.kind != TYPE_FLOAT32, "anyptr cannot casting to float");

        if (type_sizeof(as_expr->target_type) < POINTER_SIZE) {
            push_rt_call(m, RT_CALL_CASTING_TO_ANYPTR, target, 1, src_operand);
        } else {
            OP_PUSH(lir_op_move(target, src_operand));
        }
        return target;
    }

    assertf(false, "not support as_expr type %s to type %s", type_kind_str[as_expr->src.type.kind],
            type_kind_str[as_expr->target_type.kind]);
    exit(1);
}

static lir_operand_t *linear_match_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_match_t *match_expr = expr.value;

    char *match_start_ident = label_ident_with_unique(MATCH_IDENT);
    char *match_end_ident = str_connect(match_start_ident, LABEL_END_SUFFIX);

    OP_PUSH(lir_op_label(match_start_ident, true));


    bool has_ret = expr.type.kind != TYPE_VOID;

    table_set(m->current_closure->match_has_ret, match_start_ident, (void *) has_ret);
    if (has_ret && !target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    lir_op_t *match_end = lir_op_label(match_end_ident, true);

    stack_push(m->current_closure->ret_labels, match_end->output);
    stack_push(m->current_closure->ret_targets, target);


    lir_operand_t *subject_operand = NULL;
    if (match_expr->subject) {
        subject_operand = temp_var_operand_with_alloc(m, match_expr->subject->type);
        linear_expr(m, *match_expr->subject, subject_operand);
        assert(subject_operand);
    }

    SLICE_FOR(match_expr->cases) {
        // .@MATCH_79.case_handle_end
        char *handle_start_ident = label_ident_with_unique(str_connect(match_start_ident, CASE_HANDLE));
        lir_op_t *handle_start = lir_op_label(handle_start_ident, true);
        lir_op_t *handle_end = lir_op_label(str_connect(handle_start_ident, LABEL_END_SUFFIX), true);

        ast_match_case_t *match_case = SLICE_VALUE(match_expr->cases);

        // 最后一条分支不需要进行 check, 直接进入 handle body
        if (match_case->is_default || _i == match_expr->cases->count - 1) {
            goto LINEAR_HANDLE_BODY;
        }

        for (int i = 0; i < match_case->cond_list->length; ++i) {
            ast_expr_t *cond_expr = ct_list_value(match_case->cond_list, i);

            if (match_expr->subject) {
                assert(subject_operand->assert_type == LIR_OPERAND_VAR);
                lir_var_t *subject_var = subject_operand->value;
                ast_expr_t *subject_new_expr = ast_ident_expr(cond_expr->line, cond_expr->column,
                                                              subject_var->ident);
                subject_new_expr->type = match_expr->subject->type;

                // 不太好改写, 毕竟一个是 a, 一个是 b, 但是也不算难改写。提取 ident 即可
                if (cond_expr->assert_type == AST_EXPR_IS) {
                    ast_is_expr_t *cond = cond_expr->value;
                    cond->src = subject_new_expr;
                    assert(cond_expr->type.kind == TYPE_BOOL);
                } else {
                    // eq expr
                    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);
                    binary_expr->op = AST_OP_EE; // ==
                    binary_expr->left = *cond_expr;
                    binary_expr->right = *subject_new_expr;

                    cond_expr->assert_type = AST_EXPR_BINARY;
                    cond_expr->value = binary_expr;
                    cond_expr->type = type_kind_new(TYPE_BOOL);
                }
            }

            lir_operand_t *cond_target = linear_expr(m, *cond_expr, NULL);
            OP_PUSH(lir_op_new(LIR_OPCODE_BEE, bool_operand(true), cond_target, handle_start->output));
        }

        OP_PUSH(lir_op_bal(handle_end->output));
    LINEAR_HANDLE_BODY:
        OP_PUSH(handle_start);

        if (match_expr->subject && match_expr->subject->assert_type == AST_CALL && match_case->insert_auto_as) {
            assert(match_case->handle_body->count > 0);
            ast_stmt_t *as_stmt = match_case->handle_body->take[0];

            lir_var_t *subject_var = subject_operand->value;
            ast_expr_t *new_src_expr = ast_ident_expr(as_stmt->line, as_stmt->column,
                                                      subject_var->ident);
            new_src_expr->type = match_expr->subject->type;

            if (as_stmt->assert_type == AST_STMT_VARDEF) {
                ast_vardef_stmt_t *vardef_stmt = as_stmt->value;
                ast_as_expr_t *as_expr = vardef_stmt->right->value;
                as_expr->src = *new_src_expr;
            } else if (as_stmt->assert_type == AST_STMT_VAR_TUPLE_DESTR) {
                ast_var_tuple_def_stmt_t *var_tuple_def_stmt = as_stmt->value;
                ast_as_expr_t *as_expr = var_tuple_def_stmt->right.value;
                as_expr->src = *new_src_expr;
            } else {
                assert(false);
            }
        }

        linear_body(m, match_case->handle_body);
        // 只要运行了 exec， 就直接结束 case 而不是继续执行。
        OP_PUSH(lir_op_bal(match_end->output));
        // default 逻辑, 不需要添加 handle end, 其必定会进入 handle_expr, 然后退出 match
        OP_PUSH(handle_end); // case_end
    }

    match_end->line = m->current_line + 1;
    match_end->column = m->current_column;
    OP_PUSH(match_end);
    stack_pop(m->current_closure->ret_labels);
    stack_pop(m->current_closure->ret_targets);

    return target;
}

static lir_operand_t *linear_block_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_block_expr_t *block_expr = expr.value;

    char *block_start_label = label_ident_with_unique(BLOCK_IDENT);
    char *block_end_ident = str_connect(block_start_label, LABEL_END_SUFFIX);
    lir_op_t *block_end_label = lir_op_label(block_end_ident, true);


    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    OP_PUSH(lir_op_label(block_start_label, true));

    stack_push(m->current_closure->ret_labels, block_end_label->output);
    stack_push(m->current_closure->ret_targets, target);
    linear_body(m, block_expr->body);
    stack_pop(m->current_closure->ret_labels);
    stack_pop(m->current_closure->ret_targets);

    OP_PUSH(block_end_label);

    return target;
}

static lir_operand_t *linear_catch_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_catch_t *catch_expr = expr.value;
    // 编译 expr, 有异常应该直接就跳转到 catch 了。
    char *catch_start_label = label_ident_with_unique(CATCH_IDENT);
    char *catch_end_ident = str_connect(catch_start_label, LABEL_END_SUFFIX);

    bool has_ret = expr.type.kind != TYPE_VOID;

    if (has_ret && !target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    stack_push(m->current_closure->catch_error_labels, catch_start_label);
    linear_expr(m, catch_expr->try_expr, target);
    stack_pop(m->current_closure->catch_error_labels);

    // 跳过错误处理部分
    lir_op_t *catch_end_label = lir_op_label(catch_end_ident, true);
    OP_PUSH(lir_op_bal(catch_end_label->output));

    OP_PUSH(lir_op_label(catch_start_label, true));

    // catch 中总是会对 target 进行赋值，或者直接退出当前函数，所以不需要进行 default operand 处理
    if (has_ret && has_default_operand(m, expr.type)) {
        linear_default_operand(m, expr.type, target);
        has_ret = false; // 设置了默认值之后可以不进行 break check
    } else {
        OP_PUSH(lir_op_nop_def(target));
    }

    table_set(m->current_closure->match_has_ret, catch_start_label, (void *) has_ret);

    // 从 catch expr 中获取 err 然后为 err 赋值
    lir_operand_t *err_operand = linear_var_decl(m, &catch_expr->catch_err);

    push_rt_call(m, RT_CALL_CO_REMOVE_ERROR, err_operand, 0);

    stack_push(m->current_closure->ret_labels, catch_end_label->output);
    stack_push(m->current_closure->ret_targets, target);

    linear_body(m, catch_expr->catch_body);

    stack_pop(m->current_closure->ret_labels);
    stack_pop(m->current_closure->ret_targets);

    OP_PUSH(catch_end_label);

    return target;
}

static void linear_try_catch_stmt(module_t *m, ast_try_catch_stmt_t *try_stmt) {
    // 编译 expr, 有异常应该直接就跳转到 catch 了。
    char *catch_start_label = label_ident_with_unique(CATCH_IDENT);
    char *catch_end_ident = str_connect(catch_start_label, LABEL_END_SUFFIX);

    bool has_ret = false;


    stack_push(m->current_closure->catch_error_labels, catch_start_label);
    linear_body(m, try_stmt->try_body);
    stack_pop(m->current_closure->catch_error_labels);

    // 跳过错误处理部分
    lir_op_t *catch_end_label = lir_op_label(catch_end_ident, true);
    OP_PUSH(lir_op_bal(catch_end_label->output));
    OP_PUSH(lir_op_label(catch_start_label, true));


    table_set(m->current_closure->match_has_ret, catch_start_label, (void *) has_ret);

    // 为 err 赋值
    lir_operand_t *err_operand = linear_var_decl(m, &try_stmt->catch_err);
    push_rt_call(m, RT_CALL_CO_REMOVE_ERROR, err_operand, 0);

    stack_push(m->current_closure->ret_labels, catch_end_label->output);

    linear_body(m, try_stmt->catch_body);

    stack_pop(m->current_closure->ret_labels);

    OP_PUSH(catch_end_label);
}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *linear_literal(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_literal_t *literal = expr.value;
    if (literal->kind == TYPE_STRING) {
        if (!target) {
            target = temp_var_operand(m, expr.type);
        }

        // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
        lir_operand_t *imm_c_string_operand = string_operand(literal->value, literal->len);
        lir_operand_t *imm_len_operand = int_operand(literal->len);
        push_rt_call(m, RT_CALL_STRING_NEW_WITH_POOL, target, 2, imm_c_string_operand, imm_len_operand);
        return target;
    }

    if (literal->kind == TYPE_NULL) {
        lir_operand_t *src = int_operand(0);
        return linear_super_move(m, expr.type, target, src);
    }

    // 面对像 1.tostr() 这样的表达式的时候，需要创建一个堆分配的 target, expr.type 是 int/bool 等等类型
    if (expr.type.in_heap && !target) {
        uint64_t rtype_hash = type_hash(expr.type);
        lir_operand_t *temp_operand = temp_var_operand(m, type_ptrof(expr.type));
        push_rt_call(m, RT_CALL_GC_MALLOC, temp_operand, 1, int_operand(rtype_hash));

        target = indirect_addr_operand(m, expr.type, temp_operand, 0);
    }

    if (literal->kind == TYPE_BOOL) {
        bool bool_value = false;
        if (strcmp(literal->value, "true") == 0) {
            bool_value = true;
        }

        lir_operand_t *src = bool_operand(bool_value);
        return linear_super_move(m, expr.type, target, src);
    }

    if (is_integer_or_anyptr(literal->kind)) {
        char *convert_endptr;
        union {
            uint64_t u;
            int64_t s;
        } i;

        type_kind literal_kind = literal->kind;
        if (literal_kind == TYPE_ANYPTR) {
            literal_kind = TYPE_UINT;
        }

        const bool is_uint = is_unsigned(literal_kind);

        if (is_uint) {
            i.u = strtoull(literal->value, &convert_endptr, 0);
        } else {
            i.s = strtoll(literal->value, &convert_endptr, 0);
        }
        if (*convert_endptr != '\0') {
            LINEAR_ASSERTF(false, "covert '%s' to number failed", literal->value)
        }

        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = literal_kind;
        if (is_uint) {
            imm_operand->uint_value = i.u;
        } else {
            imm_operand->int_value = i.s;
        }
        lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
        return linear_super_move(m, expr.type, target, src);
    }

    if (literal->kind == TYPE_FLOAT32) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = literal->kind;
        imm_operand->f32_value = (float) atof(literal->value);
        lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
        return linear_super_move(m, expr.type, target, src);
    }

    if (literal->kind == TYPE_FLOAT64) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = literal->kind;
        imm_operand->f64_value = atof(literal->value);
        lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
        return linear_super_move(m, expr.type, target, src);
    }

    assertf(0, "cannot linear literal, kind=%s", type_kind_str[literal->kind]);
    exit(1);
}

/**
 * 将被 child fn 引用的 local var 放入到 envs 中，并传递给 child fn
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_capture_expr(module_t *m, ast_expr_t *expr) {
    if (expr->assert_type == AST_EXPR_ENV_ACCESS) {
        // 被 child fn 引用的 var 是更上一级的 var, local fn 也只能通过 env[n] 的方式引用
        ast_env_access_t *env_access = expr->value;
        lir_operand_t *values_operand = linear_inline_env_values(m);
        lir_operand_t *env_value_ptr = indirect_addr_operand(m, expr->type, values_operand, env_access->index * POINTER_SIZE);

        return env_value_ptr;
    } else if (expr->assert_type == AST_EXPR_IDENT) {
        // 被 child fn 引用的 var 是 local fn 中的 local var
        ast_ident *ident = expr->value;
        symbol_t *s = symbol_table_get(ident->literal);
        assert(s);
        assert(s->type == SYMBOL_VAR);
        ast_var_decl_t *symbol_var = s->ast_value;
        assert(symbol_var->be_capture);

        // 当前版本中所有的简单类型只要被闭包引用，就需要在堆中引用，envs 中值需要 ptr
        if (symbol_var->heap_ident) {
            return lir_var_operand(m, symbol_var->heap_ident);
        }

        // fn/vec/map/tup/set/string 都走这里直接访问原 var
        // struct/arr 同样直接访问原 ptr
        return lir_var_operand(m, symbol_var->ident);
    }

    assertf(false, "not support capture expr");
    return NULL;
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
static lir_operand_t *linear_fn_decl(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    // var a = fn() {} 类似此时的右值就是 fndef, 此时可以为 fn 创建对应的 closure 了
    ast_fndef_t *fndef = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, fndef->type);
    }

    // symbol label 不能使用 mov 在变量间自由的传递，所以这里将 symbol label 的 addr 加载出来返回
    lir_operand_t *fn_symbol_operand = symbol_label_operand(m, fndef->symbol_name);
    lir_operand_t *env_operand = int_operand(0);
    if (fndef->capture_exprs->length > 0) {
        // make envs
        lir_operand_t *length = int_operand(fndef->capture_exprs->length);
        // rt_call env_new(fndef->name, length)
        env_operand = temp_var_operand(m, type_kind_new(TYPE_GC_ENV));

        // 不通过 post hook 推出 rt_call 状态，避免进行抢占, 使 env_new+env_assign+fn_new 成为一个整体，避免被抢占
        push_rt_call(m, RT_CALL_ENV_NEW, env_operand, 1, length);
    }

    // 函数引用了外部的环境变量，所以需要编译成一个闭包，closure_name 本质就是一个 temp var, 指向了 fn_new 的结果
    // 即使在 for 循环中，temp var 此时依旧唯一指向 fn_new 的结果。所以 closure_new 存储的数据类型就是一个指针，指向了 heap 中
    // 不需要担心 coroutine 下的协程逃逸问题。

    // fn_new(env)
    lir_operand_t *fn_addr_operand = temp_var_operand(m, fndef->type);
    OP_PUSH(lir_op_lea(fn_addr_operand, fn_symbol_operand));

    lir_operand_t *result = temp_var_operand(m, fndef->type);
    push_rt_call(m, RT_CALL_FN_NEW, result, 2, fn_addr_operand, env_operand);

    // env_assign
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr_t *item = ct_list_value(fndef->capture_exprs, i);

        // - mov env[0] -> values_arr
        // - mov src_ptr -> value_arr[0]
        // 所有的 capture var 都会在堆中分配, linear_capture_expr 返回的也是堆上的引用变量的 ident(type.in_heap = true)
        // env 中直接存储了元素在堆上的地址，不需要再关注 upvalue 是否 close
        // 对于 scala type 比如 int 等，此时 ident 存储的是一个指针类型的数据，所以需要堆 ident 进行 indirect 才能获取具体的值
        // 当然实际存储时，只需要存储 ident 对应的堆的 ptr 即可, 由于此处固定堆指针，所以总是需要通过 write barrier 写入
        lir_operand_t *src_ptr = linear_capture_expr(m, item);

        // dst addr
        lir_operand_t *values_operand = temp_var_operand_with_alloc(m, type_kind_new(TYPE_GC_ENV_VALUES));
        OP_PUSH(lir_op_move(values_operand,
                            indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), env_operand, 0)));

        // src_ptr 一定是一个指针类型的地址, 比如 PTR/STRING/VEC 等等，所以此处可以直接使用 TYPE_ANYPTR
        lir_operand_t *dst = indirect_addr_operand(m, type_kind_new(TYPE_ANYPTR), values_operand,
                                                   i * POINTER_SIZE);

        if (src_ptr->assert_type == LIR_OPERAND_VAR) {
            lir_var_t *src_var = src_ptr->value;
            if (is_stack_ref_big_type(src_var->type)) {
                src_var->type = type_kind_new(TYPE_ANYPTR); // writer_barrier 参数不能是 struct
            }
        } else {
            lir_indirect_addr_t *src_indirect = src_ptr->value;
            if (is_stack_ref_big_type(src_indirect->type)) {
                src_indirect->type = type_kind_new(TYPE_ANYPTR);
            }
        }


        lir_operand_t *dst_ptr = lea_operand_pointer(m, dst);
        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_ptr, src_ptr);
        //        OP_PUSH(lir_op_move(dst, src_ptr));
    }

    return linear_super_move(m, fndef->type, target, result);
}

static void linear_throw(module_t *m, ast_throw_stmt_t *stmt) {
    // msg to errort
    assert(str_equal(stmt->error.type.ident, THROWABLE_IDENT));
    lir_operand_t *error_operand = linear_expr(m, stmt->error, NULL);

    lir_operand_t *path_operand = string_operand(m->rel_path, strlen(m->rel_path));
    assert(m->current_closure->fndef->fn_name_with_pkg);
    lir_operand_t *fn_name_operand = string_operand(m->current_closure->fndef->fn_name_with_pkg,
                                                    strlen(m->current_closure->fndef->fn_name_with_pkg));
    lir_operand_t *line_operand = int_operand(m->current_line);
    lir_operand_t *column_operand = int_operand(m->current_column);

    // attach errort to processor
    push_rt_call(m, RT_CALL_CO_THROW_ERROR, NULL, 5, error_operand, path_operand, fn_name_operand,
                 line_operand,
                 column_operand);

    // 插入 return 标识(用来做 return check 的，check 完会清除的)
    OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL));

    // bal to end label
    OP_PUSH(lir_op_bal(lir_label_operand(m->current_closure->end_label, false)));
}

static void linear_stmt(module_t *m, ast_stmt_t *stmt) {
    m->current_line = stmt->line;
    m->current_column = stmt->column;

    switch (stmt->assert_type) {
        case AST_STMT_EXPR_FAKE: {
            return linear_expr_fake(m, stmt->value);
        }
        case AST_STMT_SELECT: {
            return linear_select(m, stmt->value);
        }
        case AST_STMT_VARDEF: {
            return linear_vardef(m, stmt->value);
        }
        case AST_STMT_GLOBAL_ASSIGN:
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
            return linear_break(m);
        }
        case AST_STMT_CONTINUE: {
            return linear_continue(m);
        }
        case AST_STMT_RET: {
            return linear_ret(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return linear_for_tradition(m, stmt->value);
        }
        case AST_FNDEF: {
            linear_fn_decl(m,
                           (ast_expr_t) {
                                   .line = stmt->line,
                                   .assert_type = stmt->assert_type,
                                   .value = stmt->value,
                                   .target_type = NULL},
                           NULL);
            return;
        }
        case AST_CALL: {
            ast_call_t *call = stmt->value;
            // stmt 中都 call 都是没有返回值的
            linear_call(m,
                        (ast_expr_t) {
                                .line = stmt->line,
                                .column = stmt->column,
                                .assert_type = AST_CALL,
                                .type = call->return_type,
                                .target_type = call->return_type,
                                .value = call,
                        },
                        NULL);
            return;
        }
        case AST_CATCH: {
            ast_catch_t *catch = stmt->value;
            linear_catch_expr(m,
                              (ast_expr_t) {
                                      .line = stmt->line,
                                      .column = stmt->column,
                                      .assert_type = AST_CATCH,
                                      .type = catch->try_expr.type,
                                      .target_type = catch->try_expr.type,
                                      .value = catch,
                              },
                              NULL);
            return;
        }
        case AST_STMT_TRY_CATCH: {
            return linear_try_catch_stmt(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return linear_return(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return linear_throw(m, stmt->value);
        }
        case AST_STMT_TYPEDEF: {
            return;
        }
        default: {
            //            assertf(false, "unknown stmt type=%d", stmt->assert_type);
            // like constdef stmt, just skip it
            return;
        }
    }
}

linear_expr_fn expr_fn_table[] = {
        [AST_EXPR_LITERAL] = linear_literal,
        [AST_EXPR_IDENT] = linear_ident,
        [AST_EXPR_ENV_ACCESS] = linear_env_access,
        [AST_EXPR_BINARY] = linear_binary,
        [AST_EXPR_UNARY] = linear_unary,
        [AST_EXPR_ARRAY_NEW] = linear_array_new,
        [AST_EXPR_ARRAY_REPEAT_NEW] = linear_array_repeat_new,
        [AST_EXPR_ARRAY_ACCESS] = linear_array_access,
        [AST_EXPR_VEC_NEW] = linear_vec_new,
        [AST_EXPR_VEC_ACCESS] = linear_vec_access,
        [AST_EXPR_VEC_SLICE] = linear_vec_slice,
        [AST_EXPR_MAP_NEW] = linear_map_new,
        [AST_EXPR_MAP_ACCESS] = linear_map_access,
        [AST_EXPR_STRUCT_NEW] = linear_struct_new,
        [AST_EXPR_STRUCT_SELECT] = linear_struct_select,
        [AST_EXPR_TUPLE_NEW] = linear_tuple_new,
        [AST_EXPR_TAGGED_UNION_NEW] = linear_tagged_union_new,
        [AST_EXPR_TUPLE_ACCESS] = linear_tuple_access,
        [AST_EXPR_SET_NEW] = linear_set_new,
        [AST_CALL] = linear_call,
        [AST_FNDEF] = linear_fn_decl,
        [AST_EXPR_AS] = linear_as_expr,
        [AST_EXPR_IS] = linear_is_expr,
        [AST_MACRO_EXPR_DEFAULT] = linear_default_expr,
        [AST_MACRO_EXPR_SIZEOF] = linear_sizeof_expr,
        [AST_MACRO_EXPR_REFLECT_HASH] = linear_reflect_hash_expr,
        [AST_MACRO_ASYNC] = linear_macro_async,
        [AST_EXPR_NEW] = linear_new_expr,
        [AST_CATCH] = linear_catch_expr,
        [AST_MATCH] = linear_match_expr,
        [AST_EXPR_BLOCK] = linear_block_expr,
};

static lir_operand_t *linear_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    m->current_line = expr.line;
    m->current_column = expr.column;

    // 特殊处理
    linear_expr_fn fn = expr_fn_table[expr.assert_type];
    assertf(fn, "ast right not support");

    return fn(m, expr, target);
}

static void linear_body(module_t *m, slice_t *body) {
    for (int i = 0; i < body->count; ++i) {
        ast_stmt_t *stmt = body->take[i];
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
    m->current_closure = c;
    c->module = m;

    c->end_label = str_connect(c->linkident, LABEL_END_SUFFIX);
    c->error_label = str_connect(c->linkident, LABEL_ERROR_SUFFIX);

    // label name 使用 symbol_name
    OP_PUSH(lir_op_label(c->linkident, false));

    // 编译 fn param -> lir_var_t*
    slice_t *params = slice_new();

    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->params, i);
        assert(var_decl->type.status == REDUCTION_STATUS_DONE);

        slice_push(params, lir_var_new(m, var_decl->ident));
    }

    // 和 linear_fndef 不同，linear_closure 是函数内部的空间中,添加的也是当前 fn 的形式参数
    // 当前 fn 的形式参数在 body 中都是可以随意调用的
    // if 包含 envs 则使用 custom_var_operand 注册一个临时变量，并加入到 LIR_OPCODE_FN_BEGIN 中
    if (fndef->capture_exprs->length > 0) {
        // test.env
        char *env_symbol = str_connect(fndef->symbol_name, ".env");
        lir_operand_t *env_operand = unique_var_operand(m, type_kind_new(TYPE_GC_ENV), env_symbol);
        // 写入到 params 最后一个参数中
        slice_push(params, env_operand->value); // 这个奇怪的赋值方式！？现在直接改成 env 了, 怎么命名好呢？

        c->env_operand = env_operand;
    }

    // 返回值 operand 也 push 到 params1 里面，方便处理
    //    if (fndef->return_type.kind != TYPE_VOID) {
    //        lir_operand_t *return_operand = unique_var_operand(m, fndef->return_type, TEMP_RESULT);
    //
    //        //        slice_push(params, return_operand->value);
    //
    //        // 这里直接引用了 op->output->value, 在 ssa rename 时，c->return_operand 可以联动改名
    //        c->return_operand = return_operand;
    //    }

    OP_PUSH(lir_op_output(LIR_OPCODE_FN_BEGIN, operand_new(LIR_OPERAND_PARAMS, params)));

    OP_PUSH(lir_op_safepoint());

    // 参数 escape rewrite
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->params, i);
        assert(var_decl->type.status == REDUCTION_STATUS_DONE);
        linear_escape_rewrite(m, var_decl, true);
    }

    linear_body(m, fndef->body);

    // bal end_label
    OP_PUSH(lir_op_bal(lir_label_operand(c->end_label, true)));

    OP_PUSH(lir_op_label(c->error_label, true));
    OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL)); // 方便 return check
    OP_PUSH(lir_op_bal(lir_label_operand(c->end_label, true))); // bal end

    OP_PUSH(lir_op_label(c->end_label, true));

    //    OP_PUSH(lir_op_safepoint());
    // lower 的时候需要进行特殊的处理(return_operand 为了让 ssa use-def 链条完整)
    OP_PUSH(lir_op_new(LIR_OPCODE_FN_END, NULL, NULL, NULL));

    return c;
}

/**
 * @param c
 * @param ast
 * @return
 */
void linear(module_t *m) {
    m->current_line = 0;
    m->current_column = 0;

    for (int i = 0; i < m->ast_fndefs->count; ++i) {
        ast_fndef_t *fndef = m->ast_fndefs->take[i];
        if (fndef->is_tpl) {
            continue;
        }

        closure_t *closure = linear_fndef(fndef->module, fndef);
        slice_push(m->closures, closure);
    }
}
