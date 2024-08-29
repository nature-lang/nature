#include "linear.h"

#include <stdio.h>
#include <string.h>

#include "src/debug/debug.h"
#include "src/error.h"
#include "utils/linked.h"

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

    if (is_large_alloc_type(t)) {
        linked_t *temps = lir_memory_mov(m, t, dst, src);
        linked_concat(m->current_closure->operations, temps);
        return dst;
    }

    OP_PUSH(lir_op_move(dst, src));
    return dst;
}

static lir_operand_t *linear_default_string(module_t *m, type_t t, lir_operand_t *target) {
    push_rt_call(m, RT_CALL_STRING_NEW, target, 2, string_operand(""), int_operand(0));
    return target;
}

static lir_operand_t *
linear_default_vec(module_t *m, type_t t, lir_operand_t *len, lir_operand_t *cap, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    lir_operand_t *rtype_hash = int_operand(ct_find_rtype_hash(t));
    lir_operand_t *element_index = int_operand(ct_find_rtype_hash(t.vec->element_type));
    lir_operand_t *len_operand = int_operand(-1);
    lir_operand_t *cap_operand = int_operand(-1);
    if (len) {
        len_operand = len;
    }
    if (cap) {
        cap_operand = cap;
    }

    push_rt_call(m, RT_CALL_VEC_NEW, target, 4, rtype_hash, element_index, len_operand, cap_operand);
    return target;
}

// target 中保存了栈地址，开始向上清理
static void linear_default_empty_stack(module_t *m, lir_operand_t *target, uint64_t size) {
    uint16_t remind = size;
    uint16_t offset = 0;
    while (remind > 0) {
        uint16_t count = 0;
        uint16_t item_size = 0; // unit byte
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
            lir_imm_t *imm_operand = NEW(lir_imm_t);
            imm_operand->kind = TYPE_INT;
            imm_operand->uint_value = 0;
            lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
            lir_operand_t *dst = indirect_addr_operand(m, type_kind_new(kind), target, offset);
            OP_PUSH(lir_op_move(dst, src));
            offset += item_size;
        }

        remind -= count * item_size;
    }
}

static lir_operand_t *linear_default_arr(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    linear_default_empty_stack(m, target, type_sizeof(t));
    return target;
}

static lir_operand_t *linear_default_map(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    uint64_t rtype_hash = ct_find_rtype_hash(t);
    uint64_t key_hash = ct_find_rtype_hash(t.map->key_type);
    uint64_t value_hash = ct_find_rtype_hash(t.map->value_type);

    lir_operand_t *result = temp_var_operand_with_alloc(m, t);
    push_rt_call(m, RT_CALL_MAP_NEW, target, 3, int_operand(rtype_hash), int_operand(key_hash),
                 int_operand(value_hash));

    return target;
}

static lir_operand_t *linear_default_set(module_t *m, type_t t, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, t);
    }

    uint64_t rtype_hash = ct_find_rtype_hash(t);
    uint64_t key_index = ct_find_rtype_hash(t.map->key_type);

    push_rt_call(m, RT_CALL_SET_NEW, target, 2, int_operand(rtype_hash), int_operand(key_index));
    return target;
}

/**
 * throw 一个错误的 fn
 * @param m
 * @param t
 * @return
 */
static lir_operand_t *linear_default_fn(module_t *m, type_t t, lir_operand_t *target) {
    lir_operand_t *default_fn_operand = lir_label_operand(RT_CALL_DEFAULT_FN, false);

    OP_PUSH(lir_op_lea(target, default_fn_operand));
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
    assert(target->assert_type == LIR_OPERAND_VAR);
    assert(t.kind == TYPE_STRUCT);

    for (int i = 0; i < t.struct_->properties->length; ++i) {
        struct_property_t *p = ct_list_value(t.struct_->properties, i);

        if (exists && table_exist(exists, p->key)) {
            continue;
        }

        uint64_t offset = type_struct_offset(t.struct_, p->key);
        lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
        if (is_large_alloc_type(p->type)) {
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
    uint64_t rtype_hash = ct_find_rtype_hash(t);
    push_rt_call(m, RT_CALL_TUPLE_NEW, target, 1, int_operand(rtype_hash));

    uint64_t offset = 0;
    for (int i = 0; i < t.tuple->elements->length; ++i) {
        type_t *element = ct_list_value(t.tuple->elements, i);

        uint64_t element_size = type_sizeof(*element);
        uint16_t element_align = type_alignof(*element);

        // tuple 和 struct 一样需要对齐，不然没法做 gc_bits
        offset = align_up(offset, element_align);

        // 基于 target 计算 addr
        lir_operand_t *dst = indirect_addr_operand(m, *element, target, offset);
        if (is_large_alloc_type(*element)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_default_operand(m, *element, dst);
        offset += element_size;
    }

    return target;
}

/*
 * raw ptr 无法赋默认值
 */
static lir_operand_t *linear_default_operand(module_t *m, type_t t, lir_operand_t *target) {
    if (is_clv_default_type(t)) {
        OP_PUSH(lir_op_new(LIR_OPCODE_CLV, NULL, NULL, target));
        return target;
    }

    if (t.kind == TYPE_STRING) {
        return linear_default_string(m, t, target);
    }

    if (t.kind == TYPE_VEC) {
        return linear_default_vec(m, t, NULL, NULL, target);
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

    if (t.kind == TYPE_FN) {
        return linear_default_fn(m, t, target);
    }

    if (t.kind == TYPE_STRUCT) {
        return linear_struct_fill_default(m, t, target, NULL);
    }

    if (t.kind == TYPE_TUPLE) {
        return linear_default_tuple(m, t, target);
    }

    LINEAR_ASSERTF(false, "type '%s' no default value", type_format(t))
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

static void linear_has_error(module_t *m) {
    char *error_target_label = m->current_closure->error_label;
    assertf(m->current_line < 1000000, "line '%d' exception", m->current_line);
    assertf(m->current_column < 1000000, "column '%d' exception", m->current_line);

    // 存在 catch error
    if (m->current_closure->catch_error_label) {
        error_target_label = m->current_closure->catch_error_label;
    }

    lir_operand_t *has_error = temp_var_operand_with_alloc(m, type_kind_new(TYPE_BOOL));

    lir_operand_t *path_operand = string_operand(m->rel_path);
    lir_operand_t *fn_name_operand = string_operand(m->current_closure->fndef->symbol_name);
    lir_operand_t *line_operand = int_operand(m->current_line);
    lir_operand_t *column_operand = int_operand(m->current_column);

    // 不可抢占，也不 yield，所以不需要添加任何勾子。
    push_rt_call_no_hook(m, RT_CALL_CO_HAS_ERROR, has_error, 4, path_operand, fn_name_operand, line_operand,
                         column_operand);
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, bool_operand(true), has_error, lir_label_operand(error_target_label, true)));
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
    if (!is_escape_rewrite_type(symbol_var->type)) {
        return;
    }

    if (is_large_alloc_type(symbol_var->type) && !force_rewrite) {
        return;
    }

    if (!symbol_var->type.in_heap) {
        return;
    }

    lir_operand_t *temp_operand = temp_var_operand(m, type_ptrof(symbol_var->type));
    symbol_var->heap_ident = ((lir_var_t *) temp_operand->value)->ident;

    // - 基于原始类型申请堆空间,此时 temp_operand 时一个指针类型
    uint64_t rtype_hash = ct_find_rtype_hash(symbol_var->type);
    push_rt_call(m, RT_CALL_GC_MALLOC, temp_operand, 1, int_operand(rtype_hash));

    // - mov src 则是原始数据
    lir_operand_t *src = lir_var_operand(m, var_decl->ident);
    lir_operand_t *dst = temp_operand;
    if (!is_large_alloc_type(var_decl->type)) {
        dst = indirect_addr_operand(m, var_decl->type, temp_operand, 0);
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

    if (is_large_alloc_type(var_decl->type)) {
        if (symbol_var->type.in_heap) {
            uint64_t rtype_hash = ct_find_rtype_hash(var->type);
            // 更新类型避免在 lower 被识别成 struct 进行 amd64 下的特殊值传递
            var->type = type_kind_new(TYPE_VOID_PTR);
            push_rt_call(m, RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash));
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

    // checking closure name
    char *closure_name = m->current_closure->fndef->closure_name;
    if (closure_name && str_equal(s->ident, closure_name)) {
        // symbol 中的该符号已经改写成 closure var 了，该 closure var 通过 last param 丢了进来
        // 所以直接使用 fn 该 fn 就行了，该 fn 一定被赋值了，就放心好了
        assertf(s->type == SYMBOL_VAR, "closure symbol=%s not var");
        assertf(m->current_closure->fn_runtime_operand, "closure->fn_runtime_operand not init");
        lir_operand_t *operand = m->current_closure->fn_runtime_operand;
        lir_var_t *var = operand->value;
        assert(str_equal(var->ident, ident->literal));
    }

    if (s->type == SYMBOL_FN) {
        // 现在 symbol fn 是作为一个 type_fn 值进行传递，所以需要取出其 label 进行处理。
        // 即使是 global fn 也不例外, linear call symbol 已经进行了特殊处理，进不到这里来
        if (!target) {
            target = temp_var_operand_with_alloc(m, expr.type);
        }

        // linkident 特殊处理
        ast_fndef_t *fndef = s->ast_value;
        char *symbol_ident = fndef->symbol_name;
        if (fndef->linkid) {
            symbol_ident = fndef->linkid;
        }

        OP_PUSH(lir_op_lea(target, symbol_label_operand(m, symbol_ident)));
        return target;
    }

    if (s->type == SYMBOL_VAR) {
        ast_var_decl_t *symbol_var = s->ast_value;
        if (s->is_local) {
            // var 存在 heap_ident 表明当前 var 被弃用，后续应该使用 heap_ident。只有一些标量类型会发生这种情况。
            if (symbol_var->heap_ident) {
                assert(target == NULL);
                assert(is_escape_rewrite_type(symbol_var->type));

                lir_operand_t *src = lir_var_operand(m, symbol_var->heap_ident);

                // struct 作为 param 时也需要进行 escape 处理
                if (is_large_alloc_type(symbol_var->type)) {
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
    type_t t = stmt->right.type;

    lir_operand_t *src = linear_expr(m, stmt->right, NULL);

    // target 中保存的是一个指针数据，指向的类型是 right.type
    lir_operand_t *target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));

    push_rt_call(m, RT_CALL_VEC_ELEMENT_ADDR, target, 2, vec_target, index_target);
    if (!is_large_alloc_type(t)) {
        target = indirect_addr_operand(m, t, target, 0);
    }

    linear_has_error(m);

    linear_super_move(m, t, target, src);
}

static void linear_array_assign(module_t *m, ast_assign_stmt_t *stmt) {
    lir_operand_t *target = linear_array_access(m, stmt->left, NULL);

    linear_expr(m, stmt->right, target);
}

static void linear_tuple_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_tuple_access_t *tuple_access = stmt->left.value;
    type_t tuple_type = tuple_access->left.type;
    lir_operand_t *tuple_target = linear_expr(m, tuple_access->left, NULL);

    uint64_t element_size = type_sizeof(tuple_access->element_type);
    uint64_t offset = type_tuple_offset(tuple_type.tuple, tuple_access->index);

    lir_operand_t *dst = indirect_addr_operand(m, stmt->left.type, tuple_target, offset);
    if (is_large_alloc_type(stmt->left.type)) {
        dst = lea_operand_pointer(m, dst);
    }

    linear_expr(m, stmt->right, dst);
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

    lir_operand_t *src_ref = lea_operand_pointer(m, linear_expr(m, stmt->right, NULL));
    uint64_t size = type_sizeof(stmt->right.type);
    assertf(m->current_closure->fn_runtime_operand, "have env access, must have fn_runtime_operand");

    push_rt_call(m, RT_CALL_ENV_ASSIGN_REF, NULL, 4, m->current_closure->fn_runtime_operand, index, src_ref,
                 int_operand(size));
}

static void linear_map_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_map_access_t *map_access = stmt->left.value;
    lir_operand_t *map_target = linear_expr(m, map_access->left, NULL);

    lir_operand_t *key_ref = lea_operand_pointer(m, linear_expr(m, map_access->key, NULL));
    lir_operand_t *dst = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
    push_rt_call(m, RT_CALL_MAP_ASSIGN, dst, 2, map_target, key_ref);
    if (!is_large_alloc_type(stmt->right.type)) {
        dst = indirect_addr_operand(m, stmt->right.type, dst, 0);
    }

    linear_expr(m, stmt->right, dst);
}

static void linear_struct_assign(module_t *m, ast_assign_stmt_t *stmt) {
    ast_struct_select_t *struct_access = stmt->left.value;
    type_t type_struct;

    lir_operand_t *struct_target = linear_expr(m, struct_access->instance, NULL);
    type_struct = struct_access->instance.type;
    if (is_struct_ptr(struct_access->instance.type)) {
        type_struct = struct_access->instance.type.ptr->value_type;
    } else if (is_struct_raw_ptr(type_struct)) {
        type_struct = type_struct.ptr->value_type;
        push_rt_call(m, RT_CALL_RAW_PTR_VALID, NULL, 1, struct_target);
        linear_has_error(m);
    }

    assert(type_struct.kind == TYPE_STRUCT);

    int64_t offset = type_struct_offset(type_struct.struct_, struct_access->key);

    // indirect_addr -> [rax + 0x8], rax 存储的内容必须是一个内存地址, indirect addr 对于不同的指令来说含义不通
    // 1. mov 12 -> [rax+0x8] 表示将 12 移动到 rax 移动到 rax+0x8 对应的内存地址中, 移动的大小根据声明的尺寸
    // 2. lea [rax+0x8] -> rax 表示将 rax+0x8 计算到的内存地址存储在 rax 中
    // 3. push [rax+0x8] 等同于 mov 表示移动数据
    // 如果将 indirect 作为函数的参数，函数的参数在编译时默认使用 push/mov 传输，所以传输的是数据，如果需要 addr 则需要使用 lea
    // 将地址加载出来.
    // 由于 stmt->right 的 type 可能会超过 8byte, 所以通过 lea 将地址加载出来，再通过 lir_memory_mov 进行内存拷贝是正确的选择
    // 当然没 lea 的另外一个原因是，其会产生一个新的变量指向 struct instance 对应的内存区域, 在编译 stmt->right 时
    // 如果 stmt->right 是一个 arr/struct 这样的在内存中分配的区域，那么 linear 的返回值
    lir_operand_t *dst_slot = indirect_addr_operand(m, stmt->left.type, struct_target, offset);

    if (is_gc_alloc(stmt->left.type)) {
        lir_operand_t *obj = linear_expr(m, stmt->right, NULL);

        dst_slot = lea_operand_pointer(m, dst_slot);
        obj = lea_operand_pointer(m, obj);

        push_rt_call(m, RT_CALL_WRITE_BARRIER, NULL, 2, dst_slot, obj);
    } else {
        // lea [rax+16], rcx
        if (is_large_alloc_type(stmt->left.type)) {
            dst_slot = lea_operand_pointer(m, dst_slot);
        }
        linear_expr(m, stmt->right, dst_slot);
    }
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

        uint16_t element_size = type_sizeof(element->type);
        uint16_t element_align = type_alignof(element->type);

        offset = align_up(offset, element_align);

        // src 中已经保存了右值的具体值。可以用来 move
        lir_operand_t *element_src_operand = indirect_addr_operand(m, element->type, tuple_target, offset);
        if (is_large_alloc_type(element->type)) {
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
        uint16_t element_size = type_sizeof(element->type);

        uint16_t element_align = type_alignof(element->type);

        offset = align_up(offset, element_align);

        // 将 tuple 中的值 mov 到新的 var 空间中
        lir_operand_t *element_src_operand = indirect_addr_operand(m, element->type, tuple_target, offset);
        if (is_large_alloc_type(element->type)) {
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
    lir_operand_t *dst = linear_var_decl(m, &stmt->var_decl);

    linear_expr(m, stmt->right, dst);

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

    // 比如 set[0] = 1; 不允许这么赋值，set  .add 来添加 key
    assertf(false, "dose not support assign to %d", left.assert_type);
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

    uint64_t rtype_hash = ct_find_rtype_hash(ast->iterate.type);

    // cursor 初始值
    lir_operand_t *cursor_operand = unique_var_operand(m, type_kind_new(TYPE_INT), ITERATOR_CURSOR);
    OP_PUSH(lir_op_move(cursor_operand, int_operand(-1))); // cursor 初始值 = --

    // make label
    lir_op_t *for_start_label = lir_op_label_with_prefix(m, FOR_ITERATOR_IDENT);
    lir_op_t *for_end_label = lir_op_label_with_prefix(m, FOR_END_IDENT);

    stack_push(m->current_closure->continue_labels, for_start_label->output);
    stack_push(m->current_closure->break_labels, for_end_label->output);

    // set label
    OP_PUSH(for_start_label);

    // key 和 value 需要进行一次初始化
    lir_operand_t *first_target = linear_var_decl(m, &ast->first);
    lir_operand_t *first_ref;
    if (is_large_alloc_type(ast->first.type)) {
        // first _target 中已经保存了相关的地址，
        first_ref = temp_var_operand(m, type_kind_new(TYPE_VOID_PTR));
        OP_PUSH(lir_op_move(first_ref, first_target));
    } else {
        OP_PUSH(lir_op_nop_def(first_target)); // var_decl 没有进行初始化，所以需要进行一下 def 初始化
        first_ref = lea_operand_pointer(m, first_target);
    }

    // 单值遍历清空下, 对于 list 调用 next value,
    if (!ast->second && (ast->iterate.type.kind == TYPE_VEC || ast->iterate.type.kind == TYPE_STRING)) {
        push_rt_call(m, RT_CALL_ITERATOR_NEXT_VALUE, cursor_operand, 4, iterator_target, int_operand(rtype_hash),
                     cursor_operand,
                     first_ref);
    } else {
        push_rt_call(m, RT_CALL_ITERATOR_NEXT_KEY, cursor_operand, 4, iterator_target, int_operand(rtype_hash),
                     cursor_operand, first_ref);
    }

    // 基于 key 已经可以判断迭代是否还有了，下面的 next value 直接根据 cursor_operand 取值即可
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, int_operand(-1), cursor_operand, lir_copy_label_operand(for_end_label->output)));

    // 添加 continue label
    OP_PUSH(lir_op_label_with_prefix(m, FOR_CONTINUE_IDENT));

    // gen value
    if (ast->second) {
        lir_operand_t *second_target = linear_var_decl(m, ast->second);
        assert(!is_large_alloc_type(ast->second->type));
        OP_PUSH(lir_op_nop_def(second_target)); // var_decl 没有进行初始化，所以需要进行一下 def 初始化
        lir_operand_t *value_ref = lea_operand_pointer(m, second_target);

        push_rt_call(m, RT_CALL_ITERATOR_TAKE_VALUE, NULL, 4, iterator_target, int_operand(rtype_hash), cursor_operand,
                     value_ref);
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
    lir_op_t *for_start = lir_op_label_with_prefix(m, FOR_COND_IDENT);
    lir_operand_t *for_end_operand = lir_label_operand(label_ident_with_prefix(m, FOR_END_IDENT), true);
    stack_push(m->current_closure->continue_labels, for_start->output);
    stack_push(m->current_closure->break_labels, for_end_operand);

    OP_PUSH(for_start);

    lir_operand_t *condition_target = linear_expr(m, ast->condition, NULL);
    lir_op_t *cmp_goto = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), condition_target, for_end_operand);

    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_label_with_prefix(m, FOR_CONTINUE_IDENT));
    linear_body(m, ast->body);

    // bal => goto
    OP_PUSH(lir_op_bal(for_start->output));

    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));

    stack_pop(m->current_closure->continue_labels);
    stack_pop(m->current_closure->break_labels);
}

static void linear_for_tradition(module_t *m, ast_for_tradition_stmt_t *ast) {
    // init
    linear_stmt(m, ast->init);

    lir_op_t *for_start = lir_op_label_with_prefix(m, FOR_TRADITION_IDENT);
    lir_op_t *for_update = lir_op_label_with_prefix(m, FOR_UPDATE_IDENT);
    lir_operand_t *for_end_operand = lir_label_operand(label_ident_with_prefix(m, FOR_END_IDENT), true);
    stack_push(m->current_closure->continue_labels, for_update->output);
    stack_push(m->current_closure->break_labels, for_end_operand);

    // for_tradition
    OP_PUSH(for_start);

    // cond -> for_end
    lir_operand_t *cond_target = linear_expr(m, ast->cond, NULL);
    lir_op_t *beq = lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), cond_target, for_end_operand);
    OP_PUSH(beq);

    // continue
    OP_PUSH(lir_op_label_with_prefix(m, FOR_CONTINUE_IDENT));

    // block
    linear_body(m, ast->body);

    // update
    OP_PUSH(for_update);
    linear_stmt(m, ast->update);
    OP_PUSH(lir_op_bal(for_start->output));

    // label for_end
    OP_PUSH(lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, for_end_operand));

    stack_pop(m->current_closure->continue_labels);
    stack_pop(m->current_closure->break_labels);
}

static void linear_continue(module_t *m, ast_continue_t *stmt) {
    LINEAR_ASSERTF(m->current_closure->continue_labels->count > 0, "continue must use in for stmt")
    lir_operand_t *label = stack_top(m->current_closure->continue_labels);
    OP_PUSH(lir_op_bal(label));
}

static void linear_break(module_t *m, ast_break_t *stmt) {
    LINEAR_ASSERTF(m->current_closure->break_labels->count > 0, "break must use in for stmt");
    lir_operand_t *label = stack_top(m->current_closure->break_labels);

    if (stmt->expr != NULL) {
        assert(m->current_closure->break_labels->count);
        lir_operand_t *target = stack_top(m->current_closure->break_targets);
        linear_expr(m, *stmt->expr, target);

        OP_PUSH(lir_op_new(LIR_OPCODE_BREAK, NULL, NULL, label));
    }

    OP_PUSH(lir_op_bal(label));
}

static void linear_return(module_t *m, ast_return_stmt_t *ast) {
    if (ast->expr != NULL) {
        lir_operand_t *src = linear_expr(m, *ast->expr, NULL);
        // return void_expr() 时, m->linear_current->return_operand 是 null
        if (m->current_closure->return_operand) {
            // OP_PUSH(lir_op_move(m->linear_current->return_operand, src));
            linear_super_move(m, ast->expr->type, m->current_closure->return_operand, src);
        }

        // 保留用来做 return check
        OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL));
    }

    OP_PUSH(lir_op_bal(lir_label_operand(m->current_closure->end_label, false)));
}

static void linear_if(module_t *m, ast_if_stmt_t *if_stmt) {
    // 编译 condition
    lir_operand_t *condition_target = linear_expr(m, if_stmt->condition, NULL);

    // 判断结果是否为 false, false 对应 else
    lir_operand_t *false_target = bool_operand(false);
    lir_operand_t *end_label_operand = lir_label_operand(label_ident_with_prefix(m, END_IF_IDENT), true);
    lir_operand_t *alternate_label_operand = lir_label_operand(label_ident_with_prefix(m, IF_ALTERNATE_IDENT), true);

    lir_op_t *cmp_goto;
    if (if_stmt->alternate->count == 0) {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(end_label_operand));
    } else {
        cmp_goto = lir_op_new(LIR_OPCODE_BEQ, false_target, condition_target,
                              lir_copy_label_operand(alternate_label_operand));
    }
    OP_PUSH(cmp_goto);
    OP_PUSH(lir_op_label_with_prefix(m, IF_CONTINUE_IDENT));

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
static lir_operand_t *linear_call(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_call_t *call = expr.value;

    // global ident call optimize to 'call symbol'
    lir_operand_t *base_target = global_fn_symbol(m, call->left);
    if (!base_target) {
        lir_operand_t *temp_operand = temp_var_operand_with_alloc(m, call->left.type);
        base_target = linear_expr(m, call->left, temp_operand);
    }

    slice_t *params = slice_new();
    type_fn_t *type_fn = call->left.type.fn;
    assert(type_fn);

    // call 所有的参数都丢到 params 变量中
    for (int i = 0; i < type_fn->param_types->length; ++i) {
        if (type_fn->rest && i >= type_fn->param_types->length - 1) {
            // rest 超载情况处理
            type_t *rest_list_type = ct_list_value(type_fn->param_types, i);
            assertf(rest_list_type->kind == TYPE_VEC, "rest param must list type");

            // actual 的参数个数与 formal 的参数一致，并且 actual last type(must list) == formal last type 一致。
            if (call->args->length == type_fn->param_types->length && (call->args->length - 1) == i) {
                ast_expr_t *last_arg = ct_list_value(call->args, i);

                // last param
                if (type_compare(*rest_list_type, last_arg->type, NULL)) {
                    lir_operand_t *actual_operand = linear_expr(m, *last_arg, NULL);
                    slice_push(params, actual_operand);
                    break;
                }
            }

            // actual 剩余的所有参数进行 linear_expr 之后 都需要用一个数组收集起来，并写入到 target_operand 中
            lir_operand_t *rest_target = linear_default_vec(m, *rest_list_type, NULL, NULL, NULL);

            for (int j = i; j < call->args->length; ++j) {
                ast_expr_t *arg = ct_list_value(call->args, j);
                lir_operand_t *rest_arg = linear_expr(m, *arg, NULL);

                // 将栈上的地址传递给 list 即可,不需要管栈中存储的值
                lir_operand_t *rest_arg_ref = lea_operand_pointer(m, rest_arg);
                push_rt_call(m, RT_CALL_VEC_PUSH, NULL, 2, rest_target, rest_arg_ref);
            }

            slice_push(params, rest_target);
            break;
        }

        // 普通情况参数处理
        ast_expr_t *actual_expr = ct_list_value(call->args, i);
        lir_operand_t *actual_operand = linear_expr(m, *actual_expr, NULL);
        slice_push(params, actual_operand);
    }

    // 使用一个 int_operand(0) 预留出 fn_runtime 所需的空间,这里不需要也不能判断出 target 是否有空间引用，所以统一预留
    // 比如 [fn():int] 这样的 list 引用下的, list[0]() 无法通过类型判断出 list[0] 是否引用的外部环境，是否需要预留 fn_runtime
    // 空间,用于环境改写。 统一预留空间在最后一个参数基本不会有什么影响
    slice_push(params, int_operand(0));

    lir_operand_t *temp = NULL;
    if (call->return_type.kind != TYPE_VOID) {
        temp = temp_var_operand(m, call->return_type);
    }

    // rtcall 自带 pre_hook, 所以这里不需要重复处理了
    if (!is_rtcall(type_fn->name) && type_fn->tpl) {
        push_rt_call_no_hook(m, RT_CALL_PRE_TPLCALL_HOOK, NULL, 1, string_operand(type_fn->name));
    }
    // call base_target,params -> target
    OP_PUSH(lir_op_new(LIR_OPCODE_CALL, base_target, operand_new(LIR_OPERAND_ARGS, params), temp));

    if (type_fn->tpl) {
        push_rt_call_no_hook(m, RT_CALL_POST_TPLCALL_HOOK, NULL, 1, string_operand(type_fn->name));
    }

    // builtin call 不会抛出异常只是直接 panic， 所以不需要判断 has_error
    if (!is_builtin_call(type_fn->name)) {
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
    lir_operand_t *logic_end_operand = lir_label_operand(label_ident_with_prefix(m, LOGICAL_OR_IDENT), true);

    // xxx left -> result
    lir_operand_t *left_src = linear_expr(m, logical_expr->left, NULL);
    linear_super_move(m, expr.type, target, left_src);

    // beq result,true -> logic_or_end
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, bool_operand(true), target, logic_end_operand));

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

    lir_operand_t *logic_end_operand = lir_label_operand(label_ident_with_prefix(m, LOGICAL_AND_IDENT), true);

    // xxx left -> result
    lir_operand_t *left_target = linear_expr(m, logical_expr->left, NULL);
    OP_PUSH(lir_op_move(target, left_target));

    // beq result,true -> logic_or_end
    OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), target, logic_end_operand));

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
    if (binary_expr->operator == AST_OP_OR_OR) {
        return linear_logical_or(m, expr, target);
    }
    if (binary_expr->operator == AST_OP_AND_AND) {
        return linear_logical_and(m, expr, target);
    }

    lir_operand_t *left_target = linear_expr(m, binary_expr->left, NULL);
    lir_operand_t *right_target = linear_expr(m, binary_expr->right, NULL);
    lir_opcode_t operator = ast_op_convert[binary_expr->operator];

    if (binary_expr->left.type.kind == TYPE_STRING && binary_expr->right.type.kind == TYPE_STRING) {
        switch (operator) {
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
                assertf(false, "not support string operator %d", ast_expr_op_str[binary_expr->operator]);
            }
        }

        return target;
    }

    OP_PUSH(lir_op_new(operator, left_target, right_target, target));

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
    if (unary_expr->operator == AST_OP_NEG && first->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = first->value;
        assertf(is_number(imm->kind), "only number can neg operate");
        if (imm->kind == TYPE_INT) {
            imm->int_value = 0 - imm->int_value;
        } else {
            imm->f64_value = 0 - imm->f64_value;
        }

        return linear_super_move(m, expr.type, target, first);
    }

    if (unary_expr->operator == AST_OP_NOT) {
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

    // neg source -> target
    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // &var, 指针引用可能会造成内存逃逸，所以需要特殊处理 TODO 当前版本不存在隐式的逃逸处理，需要显式的调用 sla 才会触发逃逸处理。
    if (unary_expr->operator == AST_OP_LA || unary_expr->operator == AST_OP_UNSAFE_LA) {
        // 如果是 stack_type, 则直接移动到 target 即可，src 中存放的已经是一个栈指针了，没有必要再 lea 了
        if (is_large_alloc_type(unary_expr->operand.type)) {
            // 必须 move target，这同时也是一个类型转换的过程
            OP_PUSH(lir_op_move(target, first));
            return target;
        }

        // super_move 只有 target == null, 此时直接返回 first
        lir_operand_t *src_operand = lea_operand_pointer(m, first);

        if (first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
            lir_indirect_addr_t *indirect_addr = first->value;
            if (indirect_addr->offset == 0) {
                // indirect addr base 就是指针本身
                src_operand = indirect_addr->base;
            }
        }


        return linear_super_move(m, expr.type, target, src_operand);
    }

    // *var
    // int a = *b
    // vec<2> a = *b
    // person_t a = *b
    // 所以 target 真的有足够的空间么？target 默认就是 ptr， 无论是不是超过 8byte!
    // first 是个 ptr
    if (unary_expr->operator == AST_OP_IA) {
        // checking
        if (unary_expr->operand.type.kind == TYPE_RAW_PTR) {
            push_rt_call(m, RT_CALL_RAW_PTR_VALID, NULL, 1, first);
            linear_has_error(m);
        }

        if (!target) {
            target = temp_var_operand_with_alloc(m, expr.type);
        }

        // indirect addr first, and mov
        lir_operand_t *src = first;
        if (!is_large_alloc_type(expr.type)) {
            src = indirect_addr_operand(m, expr.type, first, 0);
        }

        return linear_super_move(m, expr.type, target, src);
    }

    lir_opcode_t type = ast_op_convert[unary_expr->operator];
    lir_op_t *unary = lir_op_new(type, first, NULL, target);
    OP_PUSH(unary);

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

    lir_operand_t *src = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
    push_rt_call(m, RT_CALL_VEC_ELEMENT_ADDR, src, 2, vec_target, index_target);
    if (!is_large_alloc_type(expr.type)) {
        src = indirect_addr_operand(m, expr.type, src, 0);
    }

    // 可能会存在数组越界的错误需要拦截处理
    linear_has_error(m);

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

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
 * @param target
 * @return
 */
static lir_operand_t *linear_vec_new(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_vec_new_t *ast = expr.value;
    type_t t = expr.type;

    lir_operand_t *len_operand = NULL;
    if (ast->len) {
        len_operand = linear_expr(m, *ast->len, NULL);
    }
    lir_operand_t *cap_operand = NULL;
    if (ast->cap) {
        cap_operand = linear_expr(m, *ast->cap, NULL);
    }

    target = linear_default_vec(m, t, len_operand, cap_operand, target);

    if (ast->elements) {
        for (int i = 0; i < ast->elements->length; ++i) {
            ast_expr_t *item_expr = ct_list_value(ast->elements, i);

            lir_operand_t *item_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
            push_rt_call(m, RT_CALL_VEC_ITERATOR, item_target, 1, target);
            if (!is_large_alloc_type(item_expr->type)) {
                item_target = indirect_addr_operand(m, t.vec->element_type, item_target, 0);
            }

            // 空间已经足够，将值放进去即可
            linear_expr(m, *item_expr, item_target);
        }
    }

    return target;
}

/**
 * int a = list[0]
 * list[0] = a
 */
static lir_operand_t *linear_array_access(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_array_access_t *ast = expr.value;

    lir_operand_t *array_target = linear_expr(m, ast->left, NULL);
    lir_operand_t *index_target = linear_expr(m, ast->index, NULL);

    lir_operand_t *array_target_ref = temp_var_operand(m, type_ptrof(ast->left.type));
    OP_PUSH(lir_op_move(array_target_ref, array_target));

    uint64_t rtype_hash = ct_find_rtype_hash(ast->left.type);

    lir_operand_t *item_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
    push_rt_call(m, RT_CALL_ARRAY_ELEMENT_ADDR, item_target, 3, array_target_ref, int_operand(rtype_hash),
                 index_target);

    if (!is_large_alloc_type(expr.type)) {
        item_target = indirect_addr_operand(m, expr.type, item_target, 0);
    }

    // 可能会存在数组越界的错误处理
    linear_has_error(m);

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

    assert(target && target->assert_type == LIR_OPERAND_VAR);

    uint64_t rtype_hash = ct_find_rtype_hash(type_array);

    lir_operand_t *target_ref = temp_var_operand(m, type_ptrof(type_array));
    OP_PUSH(lir_op_move(target_ref, target));

    // TODO 数组不能这么初始化, 应该直接整体初始化为 0， 然后再分别赋值
    for (int i = 0; i < type_array.array->length; ++i) {
        lir_operand_t *item_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
        push_rt_call(m, RT_CALL_ARRAY_ELEMENT_ADDR, item_target, 3, target_ref, int_operand(rtype_hash),
                     int_operand(i));
        if (!is_large_alloc_type(type_array.array->element_type)) {
            item_target = indirect_addr_operand(m, type_array.array->element_type, item_target, 0);
        }

        if (i < ast->elements->length) {
            ast_expr_t *item_expr = ct_list_value(ast->elements, i);
            linear_expr(m, *item_expr, item_target);
        } else {
            // gen zero value, to item_target
            linear_default_operand(m, type_array.array->element_type, item_target);
        }
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
    assertf(m->current_closure->fn_runtime_operand, "have env access, must have fn_runtime_operand");

    ast_env_access_t *ast = expr.value;
    lir_operand_t *index = int_operand(ast->index);
    uint64_t size = type_sizeof(expr.type);

    lir_operand_t *env_addr_ref = temp_var_operand(m, type_kind_new(TYPE_VOID_PTR));
    push_rt_call(m, RT_CALL_ENV_ELEMENT_VALUE, env_addr_ref, 2, m->current_closure->fn_runtime_operand, index);

    // 归还类型，让后续的使用中，可以按照 struct 类型进行使用，尤其是将 env_access 作为参数传递的过程中
    lir_var_t *temp_var = env_addr_ref->value;
    temp_var->type = expr.type;

    // mov [env+x] -> target
    if (!is_large_alloc_type(expr.type) && !kind_in_heap(expr.type.kind)) {
        env_addr_ref = indirect_addr_operand(m, expr.type, env_addr_ref, 0);
    }

    return linear_super_move(m, expr.type, target, env_addr_ref);
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

    lir_operand_t *value_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
    push_rt_call(m, RT_CALL_MAP_ACCESS, value_target, 2, map_target, key_ref);
    if (!is_large_alloc_type(expr.type)) {
        value_target = indirect_addr_operand(m, expr.type, value_target, 0);
    }

    linear_has_error(m);

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

        lir_operand_t *value_ptr_target = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
        push_rt_call(m, RT_CALL_MAP_ASSIGN, value_ptr_target, 2, target, key_ref);

        if (!is_large_alloc_type(map_type.map->value_type)) {
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
    } else if (is_struct_raw_ptr(type_struct)) {
        type_struct = type_struct.ptr->value_type;

        push_rt_call(m, RT_CALL_RAW_PTR_VALID, NULL, 1, struct_target);

        linear_has_error(m);
    }

    assert(type_struct.kind == TYPE_STRUCT);

    uint64_t offset = type_struct_offset(type_struct.struct_, ast->key);
    // 先找到存放地址(可以用 indirect addr 算出来, 也可以直接用加法算出来？)
    // 总之先找到存放数据的 addr(这里直接计算出了)
    lir_operand_t *src = indirect_addr_operand(m, expr.type, struct_target, offset);

    if (is_large_alloc_type(expr.type)) {
        src = lea_operand_pointer(m, src);
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

    lir_operand_t *tuple_target = linear_expr(m, ast->left, NULL);
    type_t t = ast->left.type;
    uint64_t item_size = type_sizeof(ast->element_type);
    uint64_t offset = type_tuple_offset(t.tuple, ast->index);

    lir_operand_t *src = indirect_addr_operand(m, t, tuple_target, offset);
    // 如果是结构体的话，src 中存储的应该是指向的 addr
    if (is_large_alloc_type(t)) {
        src = lea_operand_pointer(m, src);
    }

    return linear_super_move(m, ast->element_type, target, src);
}

/**
 * struct_new 初始化时还无法判断当前 struct 是否会出现逃逸行为，所以无法判断应该在栈上还是堆上分配
 *
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

        table_set(exists, p->key, p);

        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t offset = type_struct_offset(t.struct_, p->key);

        assertf(p->right, "struct new property_expr value empty");
        ast_expr_t *property_expr = p->right;

        lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
        if (is_large_alloc_type(p->type)) {
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
    uint64_t rtype_hash = ct_find_rtype_hash(expr.type);
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
        if (is_large_alloc_type(element->type)) {
            dst = lea_operand_pointer(m, dst);
        }

        linear_expr(m, *element, dst);

        offset += element_size;
    }

    return target;
}

static lir_operand_t *linear_new_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    // 调用 runtime_malloc 进行内存申请，并将申请的结果返回，其中返回的类型是一个 pointer 结构
    if (!target) {
        target = temp_var_operand(m, expr.type); // 必定是一个指针类型
    }

    ast_new_expr_t *new_expr = expr.value;

    uint64_t rtype_hash = ct_find_rtype_hash(new_expr->type);
    push_rt_call(m, RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash));

    // 目前只有 struct 可以 new
    assert(new_expr->type.kind == TYPE_STRUCT);

    // 默认值处理
    type_struct_t *type_struct = new_expr->type.struct_;
    table_t *exists = table_new();
    for (int i = 0; i < new_expr->properties->length; ++i) {
        struct_property_t *p = ct_list_value(new_expr->properties, i);

        table_set(exists, p->key, p);
        // struct 的 key.key 是不允许使用表达式的, 计算偏移，进行 move
        uint64_t offset = type_struct_offset(type_struct, p->key);

        assertf(p->right, "struct new property_expr value empty");
        ast_expr_t *property_expr = p->right;

        lir_operand_t *dst = indirect_addr_operand(m, p->type, target, offset);
        if (is_large_alloc_type(p->type)) {
            // foo.bar = person {}
            dst = lea_operand_pointer(m, dst);
        }

        linear_expr(m, *property_expr, dst);
    }

    linear_struct_fill_default(m, new_expr->type, target, exists);

    return target;
}

static lir_operand_t *linear_default_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    return linear_default_operand(m, expr.type, target);
}

static lir_operand_t *linear_ula_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_macro_ula_expr_t *ula_expr = expr.value;

    lir_operand_t *first = linear_expr(m, ula_expr->src, NULL);

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // 如果是 stack_type, 则直接移动到 target 即可，src 中存放的已经是一个栈指针了，没有必要再 lea 了
    if (is_large_alloc_type(ula_expr->src.type)) {
        // 必须 move target，这同时也是一个类型转换的过程
        OP_PUSH(lir_op_move(target, first));
        return target;
    }

    // super_move 只有 target == null, 此时直接返回 first
    lir_operand_t *src_operand = lea_operand_pointer(m, first);
    return linear_super_move(m, expr.type, target, src_operand);
}

static lir_operand_t *linear_sizeof_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_macro_sizeof_expr_t *ast = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    uint16_t size = type_sizeof(ast->target_type);

    OP_PUSH(lir_op_move(target, int_operand(size)));
    return target;
}

static lir_operand_t *linear_reflect_hash_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_macro_sizeof_expr_t *ast = expr.value;

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    uint64_t hash = reflect_type(ast->target_type).hash;

    OP_PUSH(lir_op_move(target, int_operand(hash)));
    return target;
}

static lir_operand_t *linear_is_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_is_expr_t *is_expr = expr.value;
    assert(is_expr->src.type.kind == TYPE_UNION || is_expr->src.type.kind == TYPE_RAW_PTR);

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    lir_operand_t *operand = linear_expr(m, is_expr->src, NULL);

    if (is_expr->src.type.kind == TYPE_RAW_PTR) {
        // is target 只能只能判断是否为 null
        LINEAR_ASSERTF(is_expr->target_type.kind == TYPE_NULL,
                       "%s is only support null, example: %s is null", type_format(is_expr->src.type),
                       type_format(is_expr->src.type));

        OP_PUSH(lir_op_new(LIR_OPCODE_SEE, operand, int_operand(0), target));
        return target;
    }

    uint64_t target_rtype_hash = ct_find_rtype_hash(is_expr->target_type);
    push_rt_call(m, RT_CALL_UNION_IS, target, 2, operand, int_operand(target_rtype_hash));

    return target;
}

/**
 * @param m
 * @param expr
 * @return
 */
static lir_operand_t *linear_as_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_as_expr_t *as_expr = expr.value;
    lir_operand_t *input = linear_expr(m, as_expr->src, NULL);

    // 如果 src 和 dst 类型一致，则不需要做任何的处理
    if (type_compare(as_expr->src.type, as_expr->target_type, NULL)) {
        return linear_super_move(m, expr.type, target, input);
    }

    if (!target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    uint64_t input_rtype_hash = ct_find_rtype_hash(as_expr->src.type);

    // 数值类型转换
    if (is_number(as_expr->target_type.kind) && is_number(as_expr->src.type.kind)) {
        lir_operand_t *output_rtype = int_operand(ct_find_rtype_hash(as_expr->target_type));

        OP_PUSH(lir_op_nop_def(target)); // 如何清理多余的 nop 指令？
        lir_operand_t *output_ref = lea_operand_pointer(m, target);
        lir_operand_t *input_ref = lea_operand_pointer(m, input);

        push_rt_call(m, RT_CALL_NUMBER_CASTING, NULL, 4, int_operand(input_rtype_hash), input_ref, output_rtype,
                     output_ref);

        return target;
    }

    // bool 类型转换
    if (as_expr->target_type.kind == TYPE_BOOL) {
        push_rt_call(m, RT_CALL_BOOL_CASTING, target, 2, int_operand(input_rtype_hash), input);
        return target;
    }

    // single type to union type
    if (as_expr->target_type.kind == TYPE_UNION) {
        assert(as_expr->src.type.kind != TYPE_UNION);
        lir_operand_t *input_ref = lea_operand_pointer(m, input);
        push_rt_call(m, RT_CALL_UNION_CASTING, target, 2, int_operand(input_rtype_hash), input_ref);
        return target;
    }

    // union assert
    if (as_expr->src.type.kind == TYPE_UNION) {
        assert(as_expr->target_type.kind != TYPE_UNION);
        OP_PUSH(lir_op_nop_def(target));
        lir_operand_t *output_ref = lea_operand_pointer(m, target);
        uint64_t target_rtype_hash = ct_find_rtype_hash(as_expr->target_type);
        push_rt_call(m, RT_CALL_UNION_ASSERT, NULL, 3, input, int_operand(target_rtype_hash), output_ref);
        linear_has_error(m);
        return target;
    }

    // nullable pointer assert
    if (as_expr->src.type.kind == TYPE_RAW_PTR) {
        // raw_ptr<T> as void_ptr
        if (as_expr->target_type.kind == TYPE_VOID_PTR) {
            OP_PUSH(lir_op_move(target, input));
            return target;
        }

        // raw_ptr<T> as ptr<T>
        assert(as_expr->target_type.kind == TYPE_PTR);
        push_rt_call(m, RT_CALL_RAW_PTR_ASSERT, target, 1, input);
        linear_has_error(m);
        return target;
    }

    // string -> list u8
    if (as_expr->src.type.kind == TYPE_STRING && is_list_u8(as_expr->target_type)) {
        OP_PUSH(lir_op_move(target, input));
        return target;
    }

    // list u8 -> string push a '\0'
    if (is_list_u8(as_expr->src.type) && as_expr->target_type.kind == TYPE_STRING) {
        OP_PUSH(lir_op_move(target, input));
        return target;
    }

    // anybody to void_ptr
    if (as_expr->target_type.kind == TYPE_VOID_PTR) {
        // 如果类型长度匹配直接进行 mov 即可
        if (type_sizeof(as_expr->src.type) < POINTER_SIZE) {
            push_rt_call(m, RT_CALL_void_ptr_CASTING, target, 1, input);
        } else {
            OP_PUSH(lir_op_move(target, input));
        }
        return target;
    }

    // void_ptr to anybody without float
    if (as_expr->src.type.kind == TYPE_VOID_PTR) {
        assertf(as_expr->src.type.kind != TYPE_FLOAT64, "void_ptr cannot casting to float");
        assertf(as_expr->src.type.kind != TYPE_FLOAT32, "void_ptr cannot casting to float");

        if (type_sizeof(as_expr->target_type) < POINTER_SIZE) {
            push_rt_call(m, RT_CALL_CASTING_TO_void_ptr, target, 1, input);
        } else {
            OP_PUSH(lir_op_move(target, input));
        }
        return target;
    }

    assertf(false, "not support as_expr type %s to type %s", type_kind_str[as_expr->src.type.kind],
            type_kind_str[as_expr->target_type.kind]);
    exit(1);
}

static lir_operand_t *linear_match_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_match_t *match_expr = expr.value;

    char *match_start_ident = label_ident_with_prefix(m, MATCH_IDENT);
    char *match_end_ident = str_connect(match_start_ident, LABEL_END_SUFFIX);
    OP_PUSH(lir_op_label(match_start_ident, true));


    bool has_ret = expr.target_type.kind != TYPE_VOID;

    table_set(m->current_closure->match_has_ret, match_start_ident, (void *) has_ret);

    if (has_ret && !target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    lir_op_t *match_end = lir_op_label(match_end_ident, true);

    stack_push(m->current_closure->break_labels, match_end->output);
    stack_push(m->current_closure->break_targets, target);


    lir_operand_t *subject_operand = NULL;
    if (match_expr->subject) {
        subject_operand = temp_var_operand_with_alloc(m, match_expr->subject->type);
        linear_expr(m, *match_expr->subject, subject_operand);
        assert(subject_operand);
    }

    SLICE_FOR(match_expr->cases) {
        ast_match_case_t *match_case = SLICE_VALUE(match_expr->cases);
        for (int i = 0; i < match_case->cond_list->length; ++i) {
            ast_expr_t *cond_expr = ct_list_value(match_case->cond_list, i);
            lir_op_t *handle_end = lir_op_label_with_prefix(m, MATCH_CASE_HANDLE_END);
            if (match_case->is_default) {
                goto DEFAULT_HANDLE;
            }

            if (match_expr->subject) {
                assert(subject_operand->assert_type == LIR_OPERAND_VAR);
                lir_var_t *subject_var = subject_operand->value;
                ast_expr_t *subject_new_expr = ast_ident_expr(cond_expr->line, cond_expr->column, subject_var->ident);
                subject_new_expr->type = match_expr->subject->type;

                // 不太好改写, 毕竟一个是 a, 一个是 b, 但是也不算难改写。提取 ident 即可
                if (cond_expr->assert_type == AST_EXPR_MATCH_IS) {
                    ast_match_is_expr_t *cond = cond_expr->value;

                    ast_is_expr_t *is_expr = NEW(ast_is_expr_t);
                    is_expr->src = *subject_new_expr;
                    is_expr->target_type = cond->target_type;

                    cond_expr->assert_type = AST_EXPR_IS;
                    cond_expr->value = is_expr;
                    assert(cond_expr->type.kind == TYPE_BOOL);
                } else {
                    // eq expr
                    ast_binary_expr_t *binary_expr = NEW(ast_binary_expr_t);
                    binary_expr->operator = AST_OP_EE; // ==
                    binary_expr->left = *cond_expr;
                    binary_expr->right = *subject_new_expr;

                    cond_expr->assert_type = AST_EXPR_BINARY;
                    cond_expr->value = binary_expr;
                    cond_expr->type = type_kind_new(TYPE_BOOL);
                }
            }


            lir_operand_t *cond_target = linear_expr(m, *cond_expr, NULL);
            OP_PUSH(lir_op_new(LIR_OPCODE_BEQ, bool_operand(false), cond_target, handle_end->output));

            DEFAULT_HANDLE:
            // exec 逻辑
            if (match_case->handle_expr) {
                linear_expr(m, *match_case->handle_expr, target);
                // break 标识用来做 cfg check
                OP_PUSH(lir_op_new(LIR_OPCODE_BREAK, NULL, NULL, match_end->output));
            } else {
                linear_body(m, match_case->handle_body);
            }

            // 只要运行了 exec， 就直接结束 case 而不是继续执行。
            OP_PUSH(lir_op_bal(match_end->output));

            // default 逻辑, 不需要添加 handle end, 其必定会进入 handle_expr, 然后退出 match
            if (!match_case->is_default) {
                OP_PUSH(handle_end); // case_end
            }
        }
    }

    match_end->line = m->current_line + 1;
    match_end->column = m->current_column;
    OP_PUSH(match_end);
    stack_pop(m->current_closure->break_labels);
    stack_pop(m->current_closure->break_targets);

    return target;
}

static lir_operand_t *linear_catch_expr(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_catch_t *catch_expr = expr.value;

    bool has_ret = expr.target_type.kind != TYPE_VOID;

    if (has_ret && !target) {
        target = temp_var_operand_with_alloc(m, expr.type);
    }

    // 编译 expr, 有异常应该直接就跳转到 catch 了。
    char *catch_error_label = label_ident_with_prefix(m, CATCH_ERROR_IDENT);
    m->current_closure->catch_error_label = catch_error_label;
    linear_expr(m, catch_expr->try_expr, target);
    m->current_closure->catch_error_label = NULL; // 表达式已经编译完成，可以清理标记位了

    // 跳过错误处理部分
    lir_op_t *catch_end_label = lir_op_label_with_prefix(m, CATCH_END_IDENT);
    OP_PUSH(lir_op_bal(catch_end_label->output));

    OP_PUSH(lir_op_label(catch_error_label, true));

    // 零值处理 target
    if (has_ret) {
        linear_default_operand(m, expr.type, target);
    }

    // 为 err 赋值
    lir_operand_t *err_operand = linear_var_decl(m, &catch_expr->catch_err);
    push_rt_call_no_hook(m, RT_CALL_CO_REMOVE_ERROR, err_operand, 0);

    stack_push(m->current_closure->break_labels, catch_end_label->output);
    stack_push(m->current_closure->break_targets, target);

    linear_body(m, catch_expr->catch_body);

    stack_pop(m->current_closure->break_labels);
    stack_pop(m->current_closure->break_targets);

    OP_PUSH(catch_end_label);

    return target;
}

/**
 * @param m
 * @param expr
 * @param target
 * @return
 */
//static lir_operand_t *linear_try(module_t *m, ast_expr_t expr, lir_operand_t *target) {
//    ast_try_t *try = expr.value;
//
//    symbol_t *s = symbol_table_get(ERRORT_TYPE_ALIAS);
//    assert(s);
//    ast_type_alias_stmt_t *errort_alias_stmt = s->ast_value;
//    assertf(errort_alias_stmt->type.status == REDUCTION_STATUS_DONE, "errort type not reduction");
//
//    // 1. 直接调用 rt_call_tuple_new 创建 tuple 空间, 然后再做 mov 操作
//    if (!target) {
//        target = temp_var_operand_with_stack(m, expr.type);
//    }
//
//    if (try->expr.type.kind == TYPE_VOID) {
//        // 包含 catch 则右侧表达式遇到错误时应该跳转到 catch_error_label
//        char *catch_end_label = label_ident_with_prefix(m, CATCH_END_IDENT);
//        m->current_closure->catch_error_label = catch_end_label;
//
//        linear_expr(m, try->expr, NULL);
//
//        m->current_closure->catch_error_label = NULL;// 表达式已经编译完成，可以清理标记位了
//
//        // catch_error_label:  try expr 会跳转到这里
//        OP_PUSH(lir_op_label(catch_end_label, true));
//
//        lir_operand_t *errort_src = temp_var_operand_without_stack(m, errort_alias_stmt->type);
//        push_rt_call_no_hook(m, RT_CALL_CO_REMOVE_ERROR, errort_src, 0);
//
//        return linear_super_move(m, errort_alias_stmt->type, target, errort_src);
//    }
//
//    uint64_t rtype_hash = ct_find_rtype_hash(expr.type);
//    push_rt_call(m, RT_CALL_TUPLE_NEW, target, 1, int_operand(rtype_hash));
//
//    // result handle
//    lir_operand_t *result_operand = indirect_addr_operand(m, try->expr.type, target, 0);
//    if (is_alloc_stack(try->expr.type)) {
//        result_operand = lea_operand_pointer(m, result_operand);
//    }
//
//    // 包含 catch 则右侧表达式遇到错误时应该跳转到 catch_error_label
//    char *catch_error_label = label_ident_with_prefix(m, CATCH_ERROR_IDENT);
//    m->current_closure->catch_error_label = catch_error_label;
//    linear_expr(m, try->expr, result_operand);
//    m->current_closure->catch_error_label = NULL;// 表达式已经编译完成，可以清理标记位了
//
//    // bal to catch_end
//    lir_op_t *catch_end_label = lir_op_label_with_prefix(m, CATCH_END_IDENT);
//    OP_PUSH(lir_op_bal(catch_end_label->output));
//
//    // catch_error_label: ------------------------------------------------------------------------------------------------------
//    OP_PUSH(lir_op_label(catch_error_label, true));
//
//    // result_operand 此时是 null，但是 nature 不允许 null 值，所以需要赋予 zero 值
//    linear_zero_operand(m, try->expr.type, result_operand);
//
//    // catch_end_label: ------------------------------------------------------------------------------------------------------
//    OP_PUSH(catch_end_label);
//
//    // remove errort 返回一个 struct errort, 其可能有值，也能是 0 值
//    // 根据 size + ali计算 offset
//    int64_t offset = type_tuple_offset(expr.type.tuple, 1);
//    lir_operand_t *temp = indirect_addr_operand(m, errort_alias_stmt->type, target, offset);
//    lir_operand_t *errort_target = lea_operand_pointer(m, temp);
//
//    lir_operand_t *errort_src = temp_var_operand_without_stack(m, errort_alias_stmt->type);
//    push_rt_call_no_hook(m, RT_CALL_CO_REMOVE_ERROR, errort_src, 0);
//
//    linear_super_move(m, errort_alias_stmt->type, errort_target, errort_src);
//
//    return target;
//}

/**
 * @param c
 * @param literal
 * @param target  default is empty
 * @return
 */
static lir_operand_t *linear_literal(module_t *m, ast_expr_t expr, lir_operand_t *target) {
    ast_literal_t *literal = expr.value;
    literal->kind = cross_kind_trans(literal->kind);

    if (literal->kind == TYPE_STRING) {
        if (!target) {
            target = temp_var_operand_with_alloc(m, expr.type);
        }

        // 转换成 nature string 对象(基于 string_new), 转换的结果赋值给 target
        lir_operand_t *imm_c_string_operand = string_operand(literal->value);
        lir_operand_t *imm_len_operand = int_operand(strlen(literal->value));
        push_rt_call(m, RT_CALL_STRING_NEW, target, 2, imm_c_string_operand, imm_len_operand);
        return target;
    }

    if (literal->kind == TYPE_NULL) {
        lir_operand_t *src = int_operand(0);
        return linear_super_move(m, expr.type, target, src);
    }

    // 创建一个堆分配的 target, expr.type 是 int/bool 等等类型
    if (expr.type.in_heap && !target) {
        uint64_t rtype_hash = ct_find_rtype_hash(expr.type);
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

    if (is_integer(literal->kind)) {
        char *convert_endptr;
        int64_t i = strtoll(literal->value, &convert_endptr, 0);
        if (*convert_endptr != '\0') {
            LINEAR_ASSERTF(false, "covert '%s' to number failed", literal->value)
        }

        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
        imm_operand->int_value = i;
        lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
        return linear_super_move(m, expr.type, target, src);
    }

    if (literal->kind == TYPE_FLOAT32) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
        imm_operand->f32_value = (float) atof(literal->value);
        lir_operand_t *src = operand_new(LIR_OPERAND_IMM, imm_operand);
        return linear_super_move(m, expr.type, target, src);
    }

    if (literal->kind == TYPE_FLOAT64) {
        lir_imm_t *imm_operand = NEW(lir_imm_t);
        imm_operand->kind = cross_kind_trans(literal->kind);
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
        assertf(m->current_closure->fn_runtime_operand, "have env access, must have fn_runtime_operand");
        ast_env_access_t *env_access = expr->value;
        lir_operand_t *env_value = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
        lir_operand_t *index = int_operand(env_access->index);

        push_rt_call(m, RT_CALL_ENV_ELEMENT_VALUE, env_value,
                     2, m->current_closure->fn_runtime_operand, index);

        return env_value;
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

    if (!fndef->closure_name) {
        if (expr.target_type.kind == 0) {
            return NULL; // 没有表达式需要接收值
        }

        OP_PUSH(lir_op_lea(target, fn_symbol_operand));
        return target;
    }

    assert(!str_equal(fndef->closure_name, ""));

    // 函数引用了外部的环境变量，所以需要编译成一个闭包，closure_name 本质就是一个 temp var, 指向了 fn_new 的结果
    // 即使在 for 循环中，temp var 此时依旧唯一指向 fn_new 的结果。所以 closure_new 存储的数据类型就是一个指针，指向了 heap 中
    // 不需要担心 coroutine 下的协程逃逸问题。

    // make envs
    lir_operand_t *length = int_operand(fndef->capture_exprs->length);
    // rt_call env_new(fndef->name, length)
    lir_operand_t *env_operand = temp_var_operand_with_alloc(m, type_kind_new(TYPE_GC_ENV));

    // 不通过 post hook 推出 rt_call 状态，避免进行抢占, 使 env_new+env_assign+fn_new 成为一个整体，避免被抢占
    push_rt_call_no_hook(m, RT_CALL_ENV_NEW, env_operand, 1, length);

    // fn_new(env)
    lir_operand_t *label_addr_operand = temp_var_operand_with_alloc(m, fndef->type);
    OP_PUSH(lir_op_lea(label_addr_operand, fn_symbol_operand));
    lir_operand_t *result = lir_var_operand(m, fndef->closure_name);
    push_rt_call(m, RT_CALL_FN_NEW, result, 2, label_addr_operand, env_operand);

    // env_assign
    for (int i = 0; i < fndef->capture_exprs->length; ++i) {
        ast_expr_t *item = ct_list_value(fndef->capture_exprs, i);

        // - mov env[0] -> value_arr
        // - mov src_ptr -> value_arr[0]
        // env 中直接存储了堆上的地址，不需要关注 upvalue 是否 close
        lir_operand_t *src_ptr = linear_capture_expr(m, item);

        // dst addr
        lir_operand_t *value_addr = temp_var_operand_with_alloc(m, type_kind_new(TYPE_VOID_PTR));
        OP_PUSH(lir_op_move(value_addr, indirect_addr_operand(m, type_kind_new(TYPE_VOID_PTR), env_operand, 0)));

        lir_operand_t *dst = indirect_addr_operand(m, type_kind_new(TYPE_VOID_PTR), value_addr, i * POINTER_SIZE);
        OP_PUSH(lir_op_move(dst, src_ptr));
    }

    return linear_super_move(m, fndef->type, target, result);
}

static void linear_throw(module_t *m, ast_throw_stmt_t *stmt) {
    // msg to errort
    symbol_t *symbol = symbol_table_get(ERRORT_TYPE_ALIAS);

    assert(stmt->error.type.kind == TYPE_STRING);
    lir_operand_t *msg_operand = linear_expr(m, stmt->error, NULL);
    lir_operand_t *path_operand = string_operand(m->rel_path);
    lir_operand_t *fn_name_operand = string_operand(m->current_closure->fndef->symbol_name);
    lir_operand_t *line_operand = int_operand(m->current_line);
    lir_operand_t *column_operand = int_operand(m->current_column);

    // attach errort to processor
    push_rt_call_no_hook(m, RT_CALL_CO_THROW_ERROR, NULL, 5, msg_operand, path_operand, fn_name_operand,
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
            linear_fn_decl(m,
                           (ast_expr_t) {
                                   .line = stmt->line, .assert_type = stmt->assert_type, .value = stmt->value,
                                   .target_type = NULL
                           },
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
        [AST_EXPR_ARRAY_NEW] = linear_array_new,
        [AST_EXPR_ARRAY_ACCESS] = linear_array_access,
        [AST_EXPR_VEC_NEW] = linear_vec_new,
        [AST_EXPR_VEC_ACCESS] = linear_vec_access,
        [AST_EXPR_MAP_NEW] = linear_map_new,
        [AST_EXPR_MAP_ACCESS] = linear_map_access,
        [AST_EXPR_STRUCT_NEW] = linear_struct_new,
        [AST_EXPR_STRUCT_SELECT] = linear_struct_select,
        [AST_EXPR_TUPLE_NEW] = linear_tuple_new,
        [AST_EXPR_TUPLE_ACCESS] = linear_tuple_access,
        [AST_EXPR_SET_NEW] = linear_set_new,
        [AST_CALL] = linear_call,
        [AST_FNDEF] = linear_fn_decl,
        [AST_EXPR_AS] = linear_as_expr,
        [AST_EXPR_IS] = linear_is_expr,
        [AST_MACRO_EXPR_ULA] = linear_ula_expr,
        [AST_MACRO_EXPR_DEFAULT] = linear_default_expr,
        [AST_MACRO_EXPR_SIZEOF] = linear_sizeof_expr,
        [AST_MACRO_EXPR_REFLECT_HASH] = linear_reflect_hash_expr,
        [AST_EXPR_NEW] = linear_new_expr,
        [AST_CATCH] = linear_catch_expr,
        [AST_MATCH] = linear_match_expr,
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
    if (fndef->closure_name) {
        // 直接使用 fn->closure_name 作为 runtime name?
        lir_operand_t *fn_runtime_operand = lir_var_operand(m, fndef->closure_name);
        slice_push(params, fn_runtime_operand->value);
        c->fn_runtime_operand = fn_runtime_operand;
    }


    OP_PUSH(lir_op_output(LIR_OPCODE_FN_BEGIN, operand_new(LIR_OPERAND_PARAMS, params)));

    // 参数 escape rewrite
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *var_decl = ct_list_value(fndef->params, i);
        assert(var_decl->type.status == REDUCTION_STATUS_DONE);
        linear_escape_rewrite(m, var_decl, true);
    }

    // 返回值 operand 也 push 到 params1 里面，方便处理
    if (fndef->return_type.kind != TYPE_VOID) {
        lir_operand_t *return_operand = unique_var_operand(m, fndef->return_type, TEMP_RESULT);
        lir_op_t *op = lir_op_output(LIR_OPCODE_NOP, return_operand);
        OP_PUSH(op);
        // 这里直接引用了 op->output->value, 在 ssa rename 时，c->return_operand 可以联动改名
        c->return_operand = op->output;
    }

    linear_body(m, fndef->body);

    // bal end_label
    OP_PUSH(lir_op_bal(lir_label_operand(c->end_label, true)));

    OP_PUSH(lir_op_label(c->error_label, true));
    OP_PUSH(lir_op_new(LIR_OPCODE_RETURN, NULL, NULL, NULL)); // 方便 return check
    OP_PUSH(lir_op_bal(lir_label_operand(c->end_label, true))); // bal end

    OP_PUSH(lir_op_label(c->end_label, true));

    // lower 的时候需要进行特殊的处理(return_operand 为了让 ssa use-def 链条完整)
    OP_PUSH(lir_op_new(LIR_OPCODE_FN_END, c->return_operand, NULL, NULL));

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
