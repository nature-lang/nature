#ifndef NATURE_SRC_LIR_H_
#define NATURE_SRC_LIR_H_

#include "src/module.h"
#include "src/register/register.h"
#include "src/symbol/symbol.h"
#include "src/types.h"
#include "utils/bitmap.h"
#include "utils/helper.h"
#include "utils/linked.h"
#include "utils/table.h"

#define VEC_TYPE_IDENT "vec"

#define TEMP_RESULT "@result"
#define TEMP_IDENT "t"
#define TEMP_VAR_IDENT "v"
#define TEMP_LABEL ".L"
#define ITERATOR_CURSOR "cursor"

#define LABEL_END_SUFFIX ".end"
#define LABEL_ERROR_SUFFIX ".error"

#define BLOCK_IDENT ".@BLOCK"
#define CATCH_IDENT ".@CATCH"
#define MATCH_IDENT ".@MATCH"// 需要用到字符串匹配，所以给定特殊字符避免后续干扰
#define CASE_HANDLE ".CASE_HANDLE"
#define SELECT_IDENT ".@SELECT"// 需要用到字符串匹配，所以给定特殊字符避免后续干扰

#define FOR_CONTINUE_IDENT ".FOR_CONTINUE"
#define FOR_UPDATE_IDENT ".FOR_UPDATE"
#define FOR_END_IDENT ".FOR_END"
#define FOR_COND_IDENT ".FOR_COND"
#define FOR_TRADITION_IDENT ".FOR_TRADITION"
#define FOR_ITERATOR_IDENT ".FOR_ITERATOR"
#define IF_IDENT ".IF"
#define IF_END_IDENT ".END"
#define IF_ALTERNATE_IDENT ".ALTERNATE"
#define IF_CONTINUE_IDENT ".CONTINUE"
#define LOGICAL_OR_IDENT ".LOGICAL_OR_END"
#define LOGICAL_AND_IDENT ".LOGICAL_AND_END"


#define ERRORT_MSG_IDENT "msg"
#define ERRORT_IS_IDENT "is"

#define FLOAT_NEG_MASK_IDENT "float_neg_mask"// -0

#define BUILTIN_REF_KEY "ref"// list.ref()
// #define BUILTIN_LEN_KEY "len" // list.len()
// #define BUILTIN_CAP_KEY "cap" // list.cap()
#define BUILTIN_DEL_KEY "del"// list.del()

#define VEC_PUSH_KEY "push"
#define VEC_POP_KEY "pop"
#define VEC_SLICE_KEY "slice"
#define VEC_CONCAT_KEY "concat"
#define VEC_LENGTH_KEY "len"
#define VEC_CAPACITY_KEY "cap"

#define MAP_DELETE_KEY "del"
// #define MAP_LENGTH_KEY "len"

#define SET_CONTAINS_KEY "contains"
#define SET_ADD_KEY "add"
#define SET_DELETE_KEY "del"

#define RT_CALL_DEFAULT_FN "zero_fn"

// RT = runtime
// CT = compile time
#define RT_CALL_VEC_NEW "rt_vec_new"
#define RT_CALL_VEC_ELEMENT_ADDR "rt_vec_element_addr"
#define RT_CALL_VEC_SLICE "rt_vec_slice"
#define RT_CALL_VEC_REF "rt_vec_ref"
#define RT_CALL_VEC_LENGTH "rt_vec_length"
#define RT_CALL_VEC_CAPACITY "rt_vec_capacity"
#define RT_CALL_VEC_PUSH "rt_vec_push"
#define RT_CALL_VEC_ITERATOR "rt_vec_iterator"
#define RT_CALL_VEC_CONCAT "rt_vec_concat"

#define RT_CALL_WRITE_BARRIER "write_barrier"

#define RT_CALL_ARRAY_ELEMENT_ADDR "array_element_addr"
#define RT_CALL_RAWPTR_VALID "rawptr_valid"

#define RT_CALL_MAP_NEW "rt_map_new"
#define RT_CALL_MAP_ACCESS "rt_map_access"
#define RT_CALL_MAP_ASSIGN "rt_map_assign"
#define RT_CALL_MAP_LENGTH "rt_map_length"
#define RT_CALL_MAP_DELETE "rt_map_delete"

#define RT_CALL_SET_NEW "rt_set_new"
#define RT_CALL_SET_ADD "rt_set_add"          // 往集合中添加元素
#define RT_CALL_SET_CONTAINS "rt_set_contains"// s.contain()
#define RT_CALL_SET_DELETE "rt_set_delete"    // 将元素从 set 中移除

// 参考 python, tuple 不允许单独赋值，必须初始化时进行赋值
#define RT_CALL_TUPLE_NEW "tuple_new"

#define RT_CALL_BOOL_CASTING "bool_casting"
#define RT_CALL_NUMBER_CASTING "number_casting"
#define RT_CALL_ANYPTR_CASTING "anyptr_casting"
#define RT_CALL_CASTING_TO_ANYPTR "casting_to_anyptr"

/**
 * 将 single 类型转换为 union 类型
 */
#define RT_CALL_UNION_CASTING "union_casting"

#define RT_CALL_INTERFACE_CASTING "interface_casting"

#define RT_CALL_INTERFACE_IS "interface_is"

#define RT_CALL_UNION_IS "union_is"

/**
 * 将 union 断言为 single 类型
 */
#define RT_CALL_UNION_ASSERT "union_assert"

#define RT_CALL_INTERFACE_ASSERT "interface_assert"

#define RT_CALL_RAWPTR_ASSERT "rawptr_assert"

#define RT_CALL_ITERATOR_NEXT_KEY "iterator_next_key"

#define RT_CALL_ITERATOR_NEXT_VALUE "iterator_next_value"

#define RT_CALL_ITERATOR_TAKE_VALUE "iterator_take_value"

#define RT_CALL_FN_NEW "fn_new"

#define RT_CALL_ENV_NEW "env_new"
#define RT_CALL_ENV_CLOSURE "env_closure"
#define RT_CALL_ENV_ASSIGN_REF "env_assign_ref"      // 实际代码位置对 env 的访问
#define RT_CALL_ENV_ELEMENT_VALUE "env_element_value"// heap addr

#define RT_CALL_STRING_NEW "string_new"

#define RT_CALL_STRING_CONCAT "string_concat"
#define RT_CALL_STRING_EE "string_ee"
#define RT_CALL_STRING_NE "string_ne"
#define RT_CALL_STRING_LT "string_lt"
#define RT_CALL_STRING_LE "string_le"
#define RT_CALL_STRING_GT "string_gt"
#define RT_CALL_STRING_GE "string_ge"
#define RT_CALL_STRING_LENGTH "rt_string_length"
#define RT_CALL_STRING_REF "rt_string_ref"// 默认引用传递

#define RT_CALL_PRE_TPLCALL_HOOK "pre_tplcall_hook"
#define RT_CALL_POST_TPLCALL_HOOK "post_tplcall_hook"

#define RT_CALL_POST_RTCALL_HOOK "post_rtcall_hook"

#define RT_CALL_GC_MALLOC "gc_malloc"

#define RT_CALL_RUNTIME_EVAL_GC "runtime_eval_gc"

#define RT_CALL_COROUTINE_ASYNC "rt_coroutine_async"
#define RT_CALL_COROUTINE_RETURN "rt_coroutine_return"

#define RT_CALL_CO_THROW_ERROR "co_throw_error"
#define RT_CALL_CO_REMOVE_ERROR "co_remove_error"
#define RT_CALL_CO_HAS_ERROR "co_has_error"
#define RT_CALL_CO_HAS_PANIC "co_has_panic"

#define RT_CALL_SELECT_BLOCK "rt_select_block"

#define RT_CALL_CHAN_SELECT "rt_chan_select"

#define RT_CALL_PROCESSOR_SET_EXIT "processor_set_exit"

#define FN_COROUTINE_RETURN_VAR "result"

#define BUILTIN_CALL_ASYNC "async"
#define BUILTIN_CALL_CO_RETURN "co_return"


#define DEFAULT_IDENT "_"

#define OP(_node) ((lir_op_t *) _node->value)

#define OP_PUSH(_op)                                         \
    do {                                                     \
        lir_op_t *_value = _op;                              \
        _value->line = m->current_line;                      \
        _value->column = m->current_column;                  \
        linked_push(m->current_closure->operations, _value); \
    } while (0)

/**
 * mov DWORD 0x1,[rbp-8] 假设 rbp = 100, 则表示将 0x1 存储在 92 ~ 96 之间
 * 也就是 stack 是向下增长，但是存储数据是从低地址往搞地址存储
 */
typedef struct {
    int64_t slot;
    uint64_t size;
    type_kind kind;
} lir_stack_t;

/**
 * 如果是地址之间的移动操作直接使用 var 即可，indirect_addr_t 对应 native 中的 indirect_addr
 * var t1 = [] =>  call new_array -> t1
 * t1[0] = 24 => mov 24 -> indirect_addr(t1, 0)
 */
typedef struct {
    lir_operand_t *base;// compiler 完成后为 var,  reg alloc 后为 reg
    int64_t offset;     // 偏移量是可以计算出来的, 默认为 0, 单位字节
    type_t type;        // lir 为了保证通用性，只能有类型，不能有 size, 指向地址存储的数据的类型
} lir_indirect_addr_t;

typedef struct {
    lir_operand_t *base;
    lir_operand_t *offset;
    type_t type;
} lir_indexed_addr_t;

typedef struct {
    char *ident;
    bool is_local;// 是否为局部符号, 否则就是 global, 可以被链接器链接
} lir_symbol_label_t;

typedef struct {
    string ident;
    type_kind kind;
} lir_symbol_var_t;// 外部符号引用, 外部符号引用

typedef struct {
    union {
        uint64_t uint_value;// 8bit, 负数使用补码存储
        int64_t int_value;  // 8bit, 负数使用补码存储
        double f64_value;   // 8bit
        float f32_value;    // 4bit
        bool bool_value;    // 1bit
        string string_value;// 8bit
    };

    type_kind kind;
} lir_imm_t;

struct lir_operand_t {
    lir_operand_type_t assert_type;
    void *value;
    lir_flag_t pos;// 在 opcode 中的位置信息
};

static inline bool is_rtcall(string target) {
    if (!target || strlen(target) == 0) {
        return false;
    }

    return str_equal(target, RT_CALL_SET_ADD) || str_equal(target, RT_CALL_SET_DELETE) ||
           str_equal(target, RT_CALL_SET_CONTAINS) || str_equal(target, RT_CALL_SET_NEW) ||
           str_equal(target, RT_CALL_VEC_NEW) || str_equal(target, RT_CALL_VEC_ELEMENT_ADDR) ||
           str_equal(target, RT_CALL_VEC_SLICE) || str_equal(target, RT_CALL_VEC_REF) ||
           str_equal(target, RT_CALL_VEC_LENGTH) || str_equal(target, RT_CALL_VEC_CAPACITY) ||
           str_equal(target, RT_CALL_VEC_PUSH) || str_equal(target, RT_CALL_VEC_ITERATOR) ||
           str_equal(target, RT_CALL_VEC_CONCAT) || str_equal(target, RT_CALL_WRITE_BARRIER) ||
           str_equal(target, RT_CALL_ARRAY_ELEMENT_ADDR) || str_equal(target, RT_CALL_RAWPTR_VALID) ||
           str_equal(target, RT_CALL_MAP_NEW) || str_equal(target, RT_CALL_MAP_ACCESS) ||
           str_equal(target, RT_CALL_MAP_ASSIGN) || str_equal(target, RT_CALL_MAP_LENGTH) ||
           str_equal(target, RT_CALL_MAP_DELETE) || str_equal(target, RT_CALL_TUPLE_NEW) ||
           str_equal(target, RT_CALL_BOOL_CASTING) || str_equal(target, RT_CALL_NUMBER_CASTING) ||
           str_equal(target, RT_CALL_ANYPTR_CASTING) || str_equal(target, RT_CALL_CASTING_TO_ANYPTR) ||
           str_equal(target, RT_CALL_UNION_CASTING) || str_equal(target, RT_CALL_UNION_IS) ||
           str_equal(target, RT_CALL_UNION_ASSERT) || str_equal(target, RT_CALL_RAWPTR_ASSERT) ||
           str_equal(target, RT_CALL_ITERATOR_NEXT_KEY) || str_equal(target, RT_CALL_ITERATOR_NEXT_VALUE) ||
           str_equal(target, RT_CALL_ITERATOR_TAKE_VALUE) || str_equal(target, RT_CALL_FN_NEW) ||
           str_equal(target, RT_CALL_ENV_NEW) || str_equal(target, RT_CALL_ENV_CLOSURE) ||
           str_equal(target, RT_CALL_ENV_ASSIGN_REF) || str_equal(target, RT_CALL_ENV_ELEMENT_VALUE) ||
           str_equal(target, RT_CALL_STRING_NEW) || str_equal(target, RT_CALL_STRING_CONCAT) ||
           str_equal(target, RT_CALL_STRING_EE) || str_equal(target, RT_CALL_STRING_NE) ||
           str_equal(target, RT_CALL_STRING_LT) || str_equal(target, RT_CALL_STRING_LE) ||
           str_equal(target, RT_CALL_STRING_GT) || str_equal(target, RT_CALL_STRING_GE) ||
           str_equal(target, RT_CALL_STRING_LENGTH) || str_equal(target, RT_CALL_STRING_REF) ||
           str_equal(target, RT_CALL_PRE_TPLCALL_HOOK) || str_equal(target, RT_CALL_POST_TPLCALL_HOOK) ||
           str_equal(target, RT_CALL_POST_RTCALL_HOOK) || str_equal(target, RT_CALL_GC_MALLOC) ||
           str_equal(target, RT_CALL_RUNTIME_EVAL_GC) || str_equal(target, RT_CALL_COROUTINE_ASYNC) ||
           str_equal(target, RT_CALL_COROUTINE_RETURN) || str_equal(target, RT_CALL_CO_THROW_ERROR) ||
           str_equal(target, RT_CALL_CO_REMOVE_ERROR) || str_equal(target, RT_CALL_CO_HAS_ERROR) ||
           str_equal(target, RT_CALL_CO_HAS_PANIC) || str_equal(target, RT_CALL_PROCESSOR_SET_EXIT);
}

static inline lir_operand_t *int64_operand(uint64_t val) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_INT64;
    imm_operand->uint_value = val;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *int16_operand(uint64_t val) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_INT16;
    imm_operand->uint_value = val;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *int_operand(uint64_t val) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_INT64;
    imm_operand->uint_value = val;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *bool_operand(bool val) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_BOOL;
    imm_operand->bool_value = val;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *float_operand(double val) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_FLOAT64;
    imm_operand->f64_value = val;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *string_operand(char *str) {
    lir_imm_t *imm_operand = NEW(lir_imm_t);
    imm_operand->kind = TYPE_RAW_STRING;
    imm_operand->string_value = str;
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = LIR_OPERAND_IMM;
    operand->value = imm_operand;
    return operand;
}

static inline lir_operand_t *operand_new(lir_operand_type_t type, void *value) {
    lir_operand_t *operand = NEW(lir_operand_t);
    operand->assert_type = type;
    operand->value = value;
    return operand;
}

/**
 * 符号使用
 * @param ident
 * @param type
 * @return
 */
static inline lir_var_t *lir_var_new(module_t *m, char *ident) {
    lir_var_t *var = NEW(lir_var_t);
    var->ident = ident;
    var->old = ident;
    var->flag = 0;

    symbol_t *s = symbol_table_get(ident);
    assertf(s, "notfound symbol=%s", ident);
    assertf(s->type == SYMBOL_VAR, "symbol=%s type not var", ident);

    ast_var_decl_t *global_var = s->ast_value;
    var->type = global_var->type;
    var->flag |= type_kind_trans_alloc(global_var->type.kind);

    return var;
}

lir_operand_t *lir_reg_operand(uint8_t index, type_kind kind);

static inline lir_operand_t *lir_stack_operand(module_t *m, int64_t slot, uint64_t size, type_kind kind) {
    lir_stack_t *stack = NEW(lir_stack_t);
    stack->slot = slot;
    stack->size = size;
    stack->kind = kind;

    return operand_new(LIR_OPERAND_STACK, stack);
}

static inline lir_operand_t *lir_var_operand(module_t *m, char *ident) {
    lir_var_t *var = lir_var_new(m, ident);
    return operand_new(LIR_OPERAND_VAR, var);
}

static inline lir_operand_t *lir_label_operand(char *ident, bool is_local) {
    lir_symbol_label_t *label = NEW(lir_symbol_label_t);
    label->ident = ident;
    label->is_local = is_local;
    return operand_new(LIR_OPERAND_SYMBOL_LABEL, label);
}

static inline lir_operand_t *symbol_label_operand(module_t *m, char *ident) {
    assert(ident);

    symbol_t *s = symbol_table_get(ident);
    assertf(s, "notfound symbol=%s", ident);
    assertf(s->type == SYMBOL_FN, "symbol=%s type not fn", ident);

    // 构造 label
    return lir_label_operand(ident, s->is_local);
}

static inline lir_operand_t *symbol_var_operand(char *ident, type_kind kind) {
    lir_symbol_var_t *var = NEW(lir_symbol_var_t);
    var->ident = ident;
    var->kind = kind;
    return operand_new(LIR_OPERAND_SYMBOL_VAR, var);
}

static inline lir_operand_t *lir_copy_label_operand(lir_operand_t *l) {
    lir_symbol_label_t *label = l->value;
    return lir_label_operand(label->ident, label->is_local);
}

static inline slice_t *recursion_extract_operands(lir_operand_t *operand, uint64_t flag) {
    slice_t *result = slice_new();
    if (!operand) {
        return result;
    }

    if (flag & FLAG(operand->assert_type)) {
        slice_push(result, operand);
        return result;
    }

    // TODO 这不能算 def, 只有算 use 才对
    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        if (FLAG(addr->base->assert_type) & flag) {
            slice_push(result, addr->base);
        }
        return result;
    }

    if (operand->assert_type == LIR_OPERAND_ARGS) {
        slice_t *args = operand->value;
        for (int i = 0; i < args->count; ++i) {
            lir_operand_t *o = args->take[i];
            assert(o->assert_type == LIR_OPERAND_VAR || o->assert_type == LIR_OPERAND_SYMBOL_VAR ||
                   o->assert_type == LIR_OPERAND_IMM ||
                   o->assert_type == LIR_OPERAND_STACK || o->assert_type == LIR_OPERAND_REG ||
                   o->assert_type == LIR_OPERAND_INDIRECT_ADDR);
            slice_concat(result, recursion_extract_operands(o, flag));
        }
        return result;
    }

    if (flag & FLAG(LIR_OPERAND_VAR) &&
        (operand->assert_type == LIR_OPERAND_PHI_BODY || operand->assert_type == LIR_OPERAND_VARS ||
         operand->assert_type == LIR_OPERAND_PARAMS)) {
        slice_t *vars = operand->value;
        for (int i = 0; i < vars->count; ++i) {
            lir_var_t *var = vars->take[i];
            slice_push(result, operand_new(LIR_OPERAND_VAR, var));
        }
    }

    if (flag & FLAG(LIR_OPERAND_REG) && operand->assert_type == LIR_OPERAND_REGS) {
        slice_t *regs = operand->value;
        for (int i = 0; i < regs->count; ++i) {
            reg_t *reg = regs->take[i];
            slice_push(result, operand_new(LIR_OPERAND_REG, reg));
        }
    }

    return result;
}

static inline slice_t *extract_all_operands(lir_op_t *op, uint64_t operand_flag) {
    slice_t *result = recursion_extract_operands(op->output, operand_flag);
    slice_concat(result, recursion_extract_operands(op->first, operand_flag));
    slice_concat(result, recursion_extract_operands(op->second, operand_flag));

    return result;
}

// 目前仅支持 var 的 copy
static inline lir_operand_t *lir_operand_copy(lir_operand_t *operand) {
    if (!operand) {
        return NULL;
    }

    lir_operand_t *new_operand = NEW(lir_operand_t);
    new_operand->assert_type = operand->assert_type;
    new_operand->value = operand->value;

    if (new_operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        return lir_copy_label_operand(new_operand);
    }

    if (new_operand->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *var = new_operand->value;
        lir_var_t *new_var = NEW(lir_var_t);
        new_var->ident = var->ident;
        new_var->old = var->old;
        new_var->type = var->type;
        new_var->flag = 0;// 即使是同一个 var 在不同的位置承担的 flag 也是不同的
        new_operand->value = new_var;
        return new_operand;
    }

    if (new_operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = new_operand->value;
        reg_t *new_reg = NEW(reg_t);
        new_reg->name = reg->name;
        new_reg->index = reg->index;
        new_reg->size = reg->size;
        new_reg->alloc_id = reg->alloc_id;

        // 保留原始类型值
        flag_t mask = 0;
        mask |= FLAG(LIR_FLAG_ALLOC_INT);
        mask |= FLAG(LIR_FLAG_ALLOC_FLOAT);
        new_reg->flag = (reg->flag & mask);
        new_operand->value = new_reg;
        return new_operand;
    }

    if (new_operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = new_operand->value;
        lir_indirect_addr_t *new_addr = NEW(lir_indirect_addr_t);
        new_addr->base = lir_operand_copy(addr->base);
        new_addr->offset = addr->offset;
        new_addr->type = addr->type;
        new_operand->value = new_addr;
        return new_operand;
    }

    if (new_operand->assert_type == LIR_OPERAND_ARGS) {
        slice_t *new_value = slice_new();

        slice_t *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand_t *o = operands->take[i];
            slice_push(new_value, lir_operand_copy(o));
        }
        new_operand->value = new_value;
        return new_operand;
    }

    return new_operand;
}

/**
 * TODO 并不是 intput 就一定是 use, 比如 memmove(var_dst, var_src) 中都 dst 就属于 def
 * @param operand
 */
static inline void set_operand_flag(lir_operand_t *operand) {
    if (!operand) {
        return;
    }

    if (operand->assert_type == LIR_OPERAND_VAR) {
        // 仅 output 且 indirect_addr = false 才配置 def
        lir_var_t *var = operand->value;
        var->flag |= FLAG(operand->pos);// 冗余 operand 的位置信息
        if (operand->pos == LIR_FLAG_OUTPUT) {
            var->flag |= FLAG(LIR_FLAG_DEF);
        } else {
            var->flag |= FLAG(LIR_FLAG_USE);
        }

        return;
    }

    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        reg->flag |= FLAG(operand->pos);
        if (operand->pos == LIR_FLAG_OUTPUT) {
            reg->flag |= FLAG(LIR_FLAG_DEF);
        } else {
            reg->flag |= FLAG(LIR_FLAG_USE);
        }
        return;
    }

    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        if (addr->base->assert_type == LIR_OPERAND_VAR) {
            lir_var_t *var = addr->base->value;
            var->flag |= FLAG(LIR_FLAG_USE);
            var->flag |= FLAG(LIR_FLAG_INDIRECT_ADDR_BASE);
        }
        return;
    }

    if (operand->assert_type == LIR_OPERAND_PARAMS) {
        slice_t *formals = operand->value;
        for (int i = 0; i < formals->count; ++i) {
            // 这里都是 def flag
            lir_var_t *var = formals->take[i];
            var->flag |= FLAG(LIR_FLAG_DEF);
        }
        return;
    }

    // 剩下的都是 use 直接提取出来即可
    slice_t *operands = recursion_extract_operands(operand, FLAG(LIR_OPERAND_VAR) | FLAG(LIR_OPERAND_REG));
    for (int i = 0; i < operands->count; ++i) {
        lir_operand_t *o = operands->take[i];
        o->pos = operand->pos;// 继承父级的 pos
        set_operand_flag(o);  // 符合嵌入的全部定义成 USE
    }
}

static inline lir_op_t *
lir_op_new(lir_opcode_t code, lir_operand_t *first, lir_operand_t *second, lir_operand_t *result) {
    lir_op_t *op = NEW(lir_op_t);
    op->code = code;
    op->first = lir_operand_copy(first);// 这里的 copy 并不深度，而是 copy 了指针！
    op->second = lir_operand_copy(second);
    op->output = lir_operand_copy(result);

    op->first && ((op->first->pos = LIR_FLAG_FIRST));
    op->second && ((op->second->pos = LIR_FLAG_SECOND));
    op->output && ((op->output->pos = LIR_FLAG_OUTPUT));

    set_operand_flag(op->first);
    set_operand_flag(op->second);
    set_operand_flag(op->output);

    return op;
}

static inline lir_op_t *lir_op_with_pos(lir_opcode_t code, lir_operand_t *first, lir_operand_t *second,
                                        lir_operand_t *result, int line, int column) {
    lir_op_t *op = lir_op_new(code, first, second, result);
    op->line = line;
    op->column = column;
    return op;
}

static inline lir_op_t *lir_op_output(lir_opcode_t code, lir_operand_t *result) {
    return lir_op_new(code, NULL, NULL, result);
}

static inline lir_op_t *lir_op_nop_def(lir_operand_t *def) {
    return lir_op_new(LIR_OPCODE_NOP, NULL, NULL, def);
}

static inline lir_op_t *lir_op_nop_use(lir_operand_t *use) {
    return lir_op_new(LIR_OPCODE_NOP, use, NULL, NULL);
}

static inline lir_op_t *lir_op_label(char *ident, bool is_local) {
    return lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, lir_label_operand(ident, is_local));
}

static inline lir_op_t *lir_op_label_with_prefix(module_t *m, char *ident) {
    char *unique_ident = label_ident_with_prefix(m, ident);
    return lir_op_label(unique_ident, true);
}

static inline lir_op_t *lir_op_local_label(module_t *m, char *ident) {
    char *unique_ident = label_ident_with_unique(m, ident);
    return lir_op_label(unique_ident, true);
}

static inline lir_op_t *lir_op_bal(lir_operand_t *label) {
    return lir_op_new(LIR_OPCODE_BAL, NULL, NULL, lir_copy_label_operand(label));
}

static inline lir_op_t *lir_op_move(lir_operand_t *dst, lir_operand_t *src) {
    return lir_op_new(LIR_OPCODE_MOVE, src, NULL, dst);
}

static inline lir_op_t *lir_op_lea(lir_operand_t *dst, lir_operand_t *src) {
    return lir_op_new(LIR_OPCODE_LEA, src, NULL, dst);
}

static inline type_t lir_operand_type(lir_operand_t *operand) {
    assert(operand->assert_type != LIR_OPERAND_REG);

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        if (stack->kind == 0) {
            return type_kind_new(TYPE_UNKNOWN);
        }
        return type_kind_new(stack->kind);
    }

    if (operand->assert_type == LIR_OPERAND_VAR) {
        lir_var_t *var = operand->value;
        return var->type;
    }

    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *addr = operand->value;
        return addr->type;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *s = operand->value;
        return type_kind_new(s->kind);
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = operand->value;
        return type_kind_new(imm->kind);
    }

    return type_kind_new(TYPE_UNKNOWN);
}

static inline type_kind operand_type_kind(lir_operand_t *operand) {
    return lir_operand_type(operand).kind;
}

/**
 * operand 此时并没有 pos 需要替换成新的 operand,替换的过程中需要重新设定
 * @param operand
 * @param pos
 * @return
 */
static inline lir_operand_t *lir_reset_operand(lir_operand_t *operand, uint8_t pos) {
    lir_operand_t *temp = lir_operand_copy(operand);
    temp->pos = pos;
    set_operand_flag(temp);
    return temp;
}

/**
 * 已经经过了 ssa 的处理，才 first op 需要排除 label 和 phi
 * @param block
 */
static inline void lir_set_quick_op(basic_block_t *block) {
    linked_node *current = linked_first(block->operations)->succ;
    while (current->value != NULL && OP(current)->code == LIR_OPCODE_PHI) {
        current = current->succ;
    }
    assert(current);
    // current code not opcode phi
    block->first_op = current;
    block->last_op = linked_last(block->operations);
}

static inline lir_op_t *push_rt_call_no_hook(module_t *m, char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *operand_args = slice_new();

    va_list args;
    va_start(args, arg_count);// 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t *);
        slice_push(operand_args, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = operand_new(LIR_OPERAND_ARGS, operand_args);

    OP_PUSH(lir_op_new(LIR_OPCODE_RT_CALL, lir_label_operand(name, false), call_params_operand, result));
}

static inline lir_op_t *push_rt_call(module_t *m, char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *operand_args = slice_new();

    va_list args;
    va_start(args, arg_count);// 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t *);
        slice_push(operand_args, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = operand_new(LIR_OPERAND_ARGS, operand_args);

    OP_PUSH(lir_op_new(LIR_OPCODE_RT_CALL, lir_label_operand(name, false), call_params_operand, result));

    push_rt_call_no_hook(m, RT_CALL_POST_RTCALL_HOOK, NULL, 1, string_operand(name));
}

static inline lir_op_t *lir_rtcall(char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count);// 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t *);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = operand_new(LIR_OPERAND_ARGS, params_operand);
    return lir_op_new(LIR_OPCODE_RT_CALL, lir_label_operand(name, false), call_params_operand, result);
}

static inline lir_op_t *lir_call(char *name, lir_operand_t *result, int arg_count, ...) {
    slice_t *params_operand = slice_new();

    va_list args;
    va_start(args, arg_count);// 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand_t *param = va_arg(args, lir_operand_t *);
        slice_push(params_operand, param);
    }
    va_end(args);
    lir_operand_t *call_params_operand = operand_new(LIR_OPERAND_ARGS, params_operand);
    return lir_op_new(LIR_OPCODE_CALL, lir_label_operand(name, false), call_params_operand, result);
}

/**
 * 返回 stack, 可以用来直接 push 值进去
 * @param c
 * @param t
 * @param dst_operand
 * @return
 */
static inline lir_op_t *lir_stack_alloc(closure_t *c, type_t t, lir_operand_t *dst_operand) {
    module_t *m = c->module;
    assert(dst_operand->assert_type == LIR_OPERAND_VAR);
    assert(is_stack_ref_big_type(t));

    uint16_t size = type_sizeof(t);

    // 为了方便和寄存器进行交换，这里总是按照指针地址对齐
    c->stack_offset += size;
    c->stack_offset = align_up(c->stack_offset, POINTER_SIZE);// 按照 8byte 对齐

    rtype_t rtype = reflect_type(t);
    assert(rtype.size == size);

    // 16, 0, 1
    uint16_t bit_index_end = (c->stack_offset - 1) / POINTER_SIZE;
    uint16_t bit_index_count = size / POINTER_SIZE;
    if (bit_index_count == 0) {
        bit_index_count = 1;
    }
    for (int i = 0; i < bit_index_count; ++i) {
        bool test = bitmap_test(rtype.malloc_gc_bits, i);
        bitmap_grow_set(c->stack_gc_bits, bit_index_end - i, test);
    }

    lir_operand_t *src_operand = lir_stack_operand(m, -c->stack_offset, size, t.kind);
    return lir_op_lea(dst_operand, src_operand);
}

static inline lir_operand_t *temp_var_operand(module_t *m, type_t type) {
    assert(type.kind > 0);

    string result = var_unique_ident(m, TEMP_IDENT);

    // 注册到符号表
    symbol_table_set_var(result, type);

    lir_operand_t *target = operand_new(LIR_OPERAND_VAR, lir_var_new(m, result));

    return target;
}

/**
 * @param type
 * @return
 */
static inline lir_operand_t *temp_var_operand_with_alloc(module_t *m, type_t type) {
    assert(type.kind > 0);

    string unique_ident = var_unique_ident(m, TEMP_IDENT);
    symbol_table_set_var(unique_ident, type);

    lir_var_t *lir_var = lir_var_new(m, unique_ident);
    lir_operand_t *target = operand_new(LIR_OPERAND_VAR, lir_var);

    // 如果 type 是一个 struct, 则为 struct 申请足够的空间
    if (is_stack_ref_big_type(type)) {
        if (type.in_heap) {
            lir_var->type = type_kind_new(TYPE_ANYPTR);

            uint64_t rtype_hash = ct_find_rtype_hash(type);
            OP_PUSH(lir_rtcall(RT_CALL_GC_MALLOC, target, 1, int_operand(rtype_hash)));
            OP_PUSH(lir_rtcall(RT_CALL_POST_RTCALL_HOOK, NULL, 1, string_operand(RT_CALL_GC_MALLOC)));
        } else {
            OP_PUSH(lir_stack_alloc(m->current_closure, type, target));
        }
    }

    return target;
}

static inline lir_operand_t *lower_temp_var_operand(closure_t *c, linked_t *list, type_t type) {
    assert(type.kind > 0);

    string unique_ident = var_unique_ident(c->module, TEMP_IDENT);
    symbol_table_set_var(unique_ident, type);

    lir_var_t *lir_var = lir_var_new(c->module, unique_ident);
    lir_operand_t *target = operand_new(LIR_OPERAND_VAR, lir_var);

    // 如果 type 是一个 struct, 则为 struct 申请足够的空间
    if (is_stack_ref_big_type(type)) {
        linked_push(list, lir_stack_alloc(c, type, target));
    }

    return target;
}

/**
 * @param m
 * @param operand
 * @param offset
 * @param type
 * @return
 */
static inline lir_operand_t *indirect_addr_operand(module_t *m, type_t type, lir_operand_t *base, int64_t offset) {
    assert(type.kind > 0);
    assert(type.kind != TYPE_VOID);

    if (base->assert_type == LIR_OPERAND_INDIRECT_ADDR || base->assert_type == LIR_OPERAND_STACK) {
        type_t base_type = lir_operand_type(base);
        lir_operand_t *temp = temp_var_operand(m, base_type);
        OP_PUSH(lir_op_move(temp, base));
        base = temp;
    }


    // 如果 base 是 global symbol var, 则加载 symbol addr 作为 indirect base
    //    if (base->assert_type == LIR_OPERAND_SYMBOL_VAR) {
    //        lir_symbol_var_t *symbol_var = base->value;
    //        lir_operand_t *temp = temp_var_operand(m, type_kind_new(TYPE_ANYPTR));
    //        OP_PUSH(lir_op_lea(temp, base));
    //        base = temp;
    //    }

    assertf(base->assert_type == LIR_OPERAND_VAR || base->assert_type == LIR_OPERAND_REG,
            "indirect addr only support var operand");
    lir_indirect_addr_t *addr = NEW(lir_indirect_addr_t);
    addr->base = base;
    addr->offset = offset;
    addr->type = type;

    return operand_new(LIR_OPERAND_INDIRECT_ADDR, addr);
}

/**
 * @param m
 * @param operand
 * @param offset
 * @param type
 * @return
 */
static inline lir_operand_t *
indexed_addr_operand(module_t *m, type_t type, lir_operand_t *base, lir_operand_t *offset) {
    assert(base->assert_type == LIR_OPERAND_VAR || base->assert_type == LIR_OPERAND_REG);
    assert(offset->assert_type == LIR_OPERAND_VAR || base->assert_type == LIR_OPERAND_REG);
    assert(type.kind > 0);
    lir_indexed_addr_t *addr = NEW(lir_indexed_addr_t);
    addr->base = base;
    addr->offset = offset;
    addr->type = type;

    return operand_new(LIR_OPERAND_INDIRECT_ADDR, addr);
}

static inline lir_operand_t *unique_var_operand_no_module(module_t *m, type_t type, char *ident) {
    string result = var_ident_with_index(m, ident);

    symbol_table_set_var(result, type);

    return operand_new(LIR_OPERAND_VAR, lir_var_new(m, result));
}

/**
 * 临时变量是否影响变量入栈？
 * @param type
 * @return
 */
static inline lir_operand_t *unique_var_operand(module_t *m, type_t type, char *ident) {
    string result = var_unique_ident(m, ident);

    symbol_table_set_var(result, type);

    return operand_new(LIR_OPERAND_VAR, lir_var_new(m, result));
}

/**
 * @param m
 * @param operand
 * @return
 */
static inline lir_operand_t *lea_operand_pointer(module_t *m, lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = operand->value;
        // 确保参数入栈
        lir_operand_t *temp_operand = temp_var_operand_with_alloc(m, type_kind_new(imm->kind));
        OP_PUSH(lir_op_move(temp_operand, operand));
        operand = temp_operand;
    }

    // symbol label 是一个指针，memory_fn_t 中存储的就是这个指针的值，所以需要将其取出来，然后再复制给一个栈临时变量。
    assert(operand->assert_type != LIR_OPERAND_SYMBOL_LABEL);

    assertf(operand->assert_type == LIR_OPERAND_VAR || operand->assert_type == LIR_OPERAND_INDIRECT_ADDR ||
                    operand->assert_type == LIR_OPERAND_SYMBOL_LABEL || operand->assert_type == LIR_OPERAND_SYMBOL_VAR,
            "only support lea var/symbol/addr, actual=%d", operand->assert_type);

    type_t t = lir_operand_type(operand);
    t = type_ptrof(t);

    lir_operand_t *temp_ref = temp_var_operand(m, t);
    OP_PUSH(lir_op_lea(temp_ref, operand));
    return temp_ref;
}

closure_t *lir_closure_new(ast_fndef_t *fndef);

static inline basic_block_t *lir_new_block(char *name, uint16_t label_index) {
    basic_block_t *basic_block = NEW(basic_block_t);
    basic_block->name = name;
    basic_block->id = label_index;

    basic_block->operations = linked_new();
    basic_block->preds = slice_new();
    basic_block->succs = slice_new();

    basic_block->forward_succs = slice_new();
    basic_block->incoming_forward_count = 0;
    basic_block->use = slice_new();
    basic_block->def = slice_new();
    basic_block->loop_ends = slice_new();
    basic_block->temp_live_in = slice_new();
    basic_block->live_in = slice_new();
    basic_block->live_out = slice_new();
    basic_block->domers = slice_new();
    basic_block->df = slice_new();
    basic_block->imm_domees = slice_new();
    basic_block->loop.index = -1;
    memset(basic_block->loop.index_map, 0, sizeof(basic_block->loop.index_map));
    basic_block->loop.depth = 0;
    basic_block->loop.visited = false;
    basic_block->loop.active = false;
    basic_block->loop.header = false;
    basic_block->loop.end = false;

    return basic_block;
}

static inline bool lir_blocks_contains(slice_t *blocks, uint16_t id) {
    for (int i = 0; i < blocks->count; ++i) {
        basic_block_t *block = blocks->take[i];
        if (block->id == id) {
            return true;
        }
    }
    return false;
}

static inline lir_operand_t *lir_new_phi_body(module_t *m, lir_var_t *var, uint8_t count) {
    lir_operand_t *operand = NEW(lir_operand_t);

    slice_t *phi_body = slice_new();
    for (int i = 0; i < count; ++i) {
        slice_push(phi_body, lir_var_new(m, var->ident));
    }

    operand->assert_type = LIR_OPERAND_PHI_BODY;
    operand->value = phi_body;
    return operand;
}

static inline bool lir_op_branch_cmp(lir_op_t *op) {
    return op->code == LIR_OPCODE_BEQ;
}

static inline bool lir_op_branch(lir_op_t *op) {
    return op->code == LIR_OPCODE_BAL || op->code == LIR_OPCODE_BEQ;
}

static inline bool lir_op_call(lir_op_t *op) {
    return op->code == LIR_OPCODE_CALL || op->code == LIR_OPCODE_RT_CALL;
}

static inline bool lir_operand_equal(lir_operand_t *a, lir_operand_t *b) {
    if (a->assert_type != b->assert_type) {
        return false;
    }

    if (a->assert_type == LIR_OPERAND_REG) {
        reg_t *reg_a = a->value;
        reg_t *reg_b = b->value;
        return reg_a->index == reg_b->index;
    }

    if (a->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack_a = a->value;
        lir_stack_t *stack_b = b->value;
        return stack_a->slot == stack_b->slot;
    }

    return false;
}

static inline bool lir_op_contain_cmp(lir_op_t *op) {
    return (op->code == LIR_OPCODE_BEQ || op->code == LIR_OPCODE_SGT || op->code == LIR_OPCODE_SGE ||
            op->code == LIR_OPCODE_SEE ||
            op->code == LIR_OPCODE_SNE || op->code == LIR_OPCODE_SLT || op->code == LIR_OPCODE_SLE);
}

static inline bool lir_op_term(lir_op_t *op) {
    return (op->code == LIR_OPCODE_ADD || op->code == LIR_OPCODE_SUB);
}


static inline bool lir_op_ternary(lir_op_t *op) {
    return op->code == LIR_OPCODE_ADD || op->code == LIR_OPCODE_SUB || op->code == LIR_OPCODE_MUL ||
           op->code == LIR_OPCODE_DIV ||
           op->code == LIR_OPCODE_REM || op->code == LIR_OPCODE_SHR || op->code == LIR_OPCODE_SHL ||
           op->code == LIR_OPCODE_AND ||
           op->code == LIR_OPCODE_OR || op->code == LIR_OPCODE_XOR;
}

static inline bool lir_op_scc(lir_op_t *op) {
    return op->code == LIR_OPCODE_SLT ||
           op->code == LIR_OPCODE_SLE ||
           op->code == LIR_OPCODE_SGT ||
           op->code == LIR_OPCODE_SGE ||
           op->code == LIR_OPCODE_SEE ||
           op->code == LIR_OPCODE_SNE;
}


static inline bool lir_op_factor(lir_op_t *op) {
    return op->code == LIR_OPCODE_DIV || op->code == LIR_OPCODE_MUL || op->code == LIR_OPCODE_REM;
}

static inline slice_t *extract_op_operands(lir_op_t *op, flag_t operand_flag, flag_t vr_flag, bool extract_value) {
    slice_t *temps = extract_all_operands(op, operand_flag);

    slice_t *results = slice_new();
    for (int i = 0; i < temps->count; ++i) {
        lir_operand_t *operand = temps->take[i];
        assertf(FLAG(operand->assert_type) & operand_flag, "operand type is not and operand flag");

        // 只有 var 或者 reg 现需要进行 vr 校验
        if (operand->assert_type == LIR_OPERAND_VAR) {
            lir_var_t *var = operand->value;
            // def or use
            if (!(var->flag & vr_flag)) {
                continue;
            }
        } else if (operand->assert_type == LIR_OPERAND_REG) {
            reg_t *reg = operand->value;
            if (!(reg->flag & vr_flag)) {
                continue;
            }
        }

        if (extract_value) {
            slice_push(results, operand->value);
        } else {
            slice_push(results, operand);
        }
    }

    return results;
}

/**
 * @param op
 * @param vr_flag  use or def
 * @return lir_var_t
 */
static inline slice_t *extract_var_operands(lir_op_t *op, flag_t vr_flag) {
    return extract_op_operands(op, FLAG(LIR_OPERAND_VAR), vr_flag, true);
}

static inline bool lir_is_mem(lir_operand_t *operand) {
    return operand->assert_type == LIR_OPERAND_STACK || operand->assert_type == LIR_OPERAND_INDIRECT_ADDR;
}

/**
 * 该函数能够成立的依据是，即使同一个 var 在不同的生命周期下会有不同的 reg, 但是在所有生命周期下共享一个 stack_slot
 * 一旦溢出，则一定在该 stack 中
 * @param c
 * @return
 */
static inline int64_t var_stack_slot(closure_t *c, lir_var_t *var) {
    interval_t *i = table_get(c->interval_table, var->ident);
    assert(i);
    assert(i->stack_slot);
    assert(*i->stack_slot != 0);
    return *i->stack_slot;
}

static inline lir_operand_t *lir_stack_offset(module_t *m, lir_operand_t *operand, int64_t offset) {
    assert(operand->assert_type == LIR_OPERAND_STACK);
    lir_stack_t *stack = operand->value;
    return lir_stack_operand(m, stack->slot + offset, stack->size, 0);
}

/**
 * 必须包含一个寄存器
 * @param op
 * @return
 */
static inline bool lir_can_mov(lir_op_t *op) {
    if (op->first && op->first->assert_type == LIR_OPERAND_VAR) {
        return true;
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_VAR) {
        return true;
    }

    if (op->first && op->first->assert_type == LIR_OPERAND_REG) {
        return true;
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_REG) {
        return true;
    }

    return false;
}

static inline bool lir_can_lea(lir_op_t *op) {
    if (op->output && op->output->assert_type == LIR_OPERAND_VAR) {
        return true;
    }

    if (op->output && op->output->assert_type == LIR_OPERAND_REG) {
        return true;
    }

    return false;
}

linked_t *lir_memory_mov(module_t *m, uint16_t size, lir_operand_t *dst, lir_operand_t *src);

#endif// NATURE_SRC_LIR_H_
