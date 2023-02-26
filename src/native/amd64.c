#include "amd64.h"
#include "src/type.h"
#include "src/register/register.h"
#include "src/register/amd64.h"
#include "utils/error.h"
#include "utils/helper.h"
#include "src/symbol/symbol.h"
#include "src/native/native.h"
#include "src/debug/debug.h"
#include <assert.h>

static bool asm_operand_equal(asm_operand_t *a, asm_operand_t *b) {
    if (a->type != b->type) {
        return false;
    }
    if (a->size != b->size) {
        return false;
    }

    if (a->type == ASM_OPERAND_TYPE_REG) {
        reg_t *reg_a = a->value;
        reg_t *reg_b = b->value;
        return str_equal(reg_a->name, reg_b->name);
    }

    return false;
}

static void asm_mov(slice_t *operations, asm_operand_t *dst, asm_operand_t *src) {
    if (asm_operand_equal(dst, src)) {
        return;
    }

    asm_operation_t *operation = ASM_INST("mov", { dst, src });
    slice_push(operations, operation);
}

/**
 * lir_operand 中不能直接转换为 asm_operand 的参数
 * type_string/lir_operand_memory
 * @param operand
 * @param asm_operand
 * @param used_regs
 * @return
 */
static asm_operand_t *lir_operand_transform(closure_t *c, slice_t *operations, lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return REG(reg);
    }

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        return DISP_REG(rbp, stack->slot, stack->size);
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *v = operand->value;
        if (v->type == TYPE_STRING_RAW) {
            // 生成符号表(通常用于 .data 段)
            char *unique_name = ASM_VAR_DECL_UNIQUE_NAME(c->module);
            asm_var_decl_t *decl = NEW(asm_var_decl_t);
            decl->name = unique_name;
            decl->size = strlen(v->string_value) + 1; // + 1 表示 \0
            decl->value = (uint8_t *) v->string_value;
            slice_push(c->asm_var_decls, decl);

            // asm_copy
            return SYMBOL(unique_name, false);
        } else if (v->type == TYPE_FLOAT) {
            char *unique_name = ASM_VAR_DECL_UNIQUE_NAME(c->module);
            asm_var_decl_t *decl = NEW(asm_var_decl_t);
            decl->name = unique_name;
            decl->size = QWORD;
            decl->value = (uint8_t *) &v->float_value; // float to uint8
//            decl->code = ASM_VAR_DECL_TYPE_FLOAT;
            slice_push(c->asm_var_decls, decl);

            return SYMBOL(unique_name, false);
        } else if (v->type == TYPE_INT) {
            return UINT(v->int_value); // 默认使用 UINT32,v->int_value 真的大于 32 位时使用 64
        } else if (v->type == TYPE_INT8) {
            return UINT8(v->int_value);
        } else if (v->type == TYPE_INT16) {
            return UINT16(v->int_value);
        } else if (v->type == TYPE_INT32) {
            return UINT32(v->int_value);
        } else if (v->type == TYPE_INT64) {
            return UINT64(v->int_value);
        } else if (v->type == TYPE_FLOAT) {
            return FLOAT32(v->float_value);
        } else if (v->type == TYPE_BOOL) {
            return UINT8(v->bool_value);
        }
        assert(false && "code immediate not expected");
    }

    // string_t/map_t/array_t
    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *v = operand->value;
        lir_operand_t *base = v->base;
        assertf(base->assert_type == LIR_OPERAND_REG, "indirect addr base must be reg");

        reg_t *reg = base->value;
        asm_operand_t *asm_operand = INDIRECT_REG(reg, type_base_sizeof(v->type.kind));
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *v = operand->value;
        asm_operand_t *asm_operand = SYMBOL(v->ident, false);
        // symbol 也要有 size, 不然无法选择合适的寄存器进行 mov!
        asm_operand->size = type_base_sizeof(v->type);
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return SYMBOL(v->ident, false);
    }

    assert(false && "operand type not expected");
}


static asm_operation_t *reg_cleanup(reg_t *reg) {
    // TODO ah/bh/ch/dh 不能这么清理

    reg_t *r = (reg_t *) reg_find(reg->index, QWORD);
    assert(r && "reg not found");
    return ASM_INST("xor", { REG(r), REG(r) });
}

/**
 * op->result must reg
 * @param c
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_mov(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type != LIR_OPERAND_VAR && op->first->assert_type != LIR_OPERAND_VAR);
    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);
    slice_t *operations = slice_new();
    asm_operand_t *first = lir_operand_transform(c, operations, op->first);
    asm_operand_t *output = lir_operand_transform(c, operations, op->output);

    asm_mov(operations, output, first);
    return operations;
}

/**
 * 核心问题是: 在结构体作为返回值时，当外部调用将函数的返回地址作为参数 rdi 传递给函数时，
 * 根据 ABI 规定，函数操作的第一步就是对 rdi 入栈，但是当 native return 时,我并不知道 rdi 被存储在了栈的什么位置？
 * 但是实际上是能够知道的，包括初始化时 sub rbp,n 的 n 也是可以在寄存器分配阶段就确定下来的。
 * n 的信息作为 closure_t 的属性存储在 closure_t 中，如何将相关信息传递给 native ?, 参数 1 改成 closure_t?
 * 在结构体中 temp_var 存储的是结构体的起始地址。不能直接 return 起始地址，大数据会随着函数栈帧消亡。 而是将结构体作为整个值传递。
 * 既然已经知道了结构体的起始位置，以及隐藏参数的所在栈帧。 就可以直接进行结构体返回值的构建。
 * @param c
 * @param ast
 * @return
 */
static slice_t *amd64_native_return(closure_t *c, lir_op_t *op) {
    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
    slice_t *operations = slice_new();

    // compiler 阶段已经附加了 bal end fn 的指令
    // lower 阶段已经将返回值放在了 rax/xmm0 中
    // 所以这里什么都不用做

    return operations;
}

static slice_t *amd64_native_push(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    asm_operand_t *first = lir_operand_transform(c, operations, op->first);
    slice_push(operations, ASM_INST("push", { first }));
    return operations;
}

static slice_t *amd64_native_bal(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);

    slice_t *operations = slice_new();
    asm_operand_t *result = lir_operand_transform(c, operations, op->output);
    slice_push(operations, ASM_INST("jmp", { result }));
    return operations;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_clr(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 参数转换
    asm_operand_t *result = lir_operand_transform(c, operations, op->output);
    slice_push(operations, ASM_INST("xor", { result, result }));

    return operations;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_add(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 参数转换
    asm_operand_t *first = lir_operand_transform(c, operations, op->first);
    asm_operand_t *second = lir_operand_transform(c, operations, op->second);
    asm_operand_t *result = lir_operand_transform(c, operations, op->output);

    // 如果 first is reg, 则有
    // add second -> first
    // mov first -> result
    // 例如: rax + rcx = rax
    // add rcx -> rax, mov ra-> rax
    // 例如：rcx + rax = rax
    // add rax -> rcx, mov rcx -> rax
    // 完全不用考虑寄存器覆盖的问题
    slice_push(operations, ASM_INST("add", { first, second }));
    asm_mov(operations, result, first);

    return operations;
}

/**
 * // sub rax, 1 -> rcx
 * // ↓
 * // mov rax -> rcx
 * // sub 1 -> rcx = sub rcx - 1 -> rcx
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_sub(closure_t *c, lir_op_t *op) {
    assert(op->first->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 参数转换
    asm_operand_t *first = lir_operand_transform(c, operations, op->first); // rcx
    asm_operand_t *second = lir_operand_transform(c, operations, op->second); // rax
    asm_operand_t *result = lir_operand_transform(c, operations, op->output); // rax


    // 在 first 是 reg 的前提下，则有
    // sub second -> first, 此时结果存储在 first, 且 first 为寄存器, mov first -> result 即可
    // 原始： rcx - rax = rax
    // sub rax -> rcx, mov rcx -> rax
    // 原始: rax - rcx = rax
    // sub rcx -> rax, mov rax -> rax
    slice_push(operations, ASM_INST("sub", { first, second }));
    asm_mov(operations, result, first);

    return operations;
}


/**
 * 经过寄存器分配，call 阶段，所有的物理寄存器都可以任意使用
 * LIR_OPCODE_CALL base, param => result
 * base 有哪些形态？
 * http.get() // 外部符号引用,无非就是是否在符号表中
 * sum.n() // 内部符号引用, sum.n 是否属于 var? 所有的符号都被记录为 var
 * test[0]() // 经过计算后的左值，其值最终存储在了一个临时变量中 => temp var
 * 所以 base 最终只有一个形态，那就是 var, 如果 var 在当前 closure_t 有定义，那其至少会被
 * - 会被堆栈分配,此时如何调用 call 指令？
 * asm call + stack[1]([indirect addr]) or asm call + reg ?
 * 没啥可以担心的，这都是被支持的
 *
 * - 但是如果其如果没有被寄存器分配，就属于外部符号(label)
 * asm call + SYMBOL 即可
 * 所以最终
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_call(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // first is fn label or addr
    asm_operand_t *first = lir_operand_transform(c, operations, op->first);

    // 2. 参数处理  lir_ope code->second;
    assert(((slice_t *) op->second->value)->count == 0);
    // lower 阶段已经处理过了

//    // TODO 调用变长参数函数之前，需要将 rax 置为 0, 如何判断调用目标是否为变长参数函数？
//    if (false)
//        slice_push(operations, ASM_INST("mov", { REG(rax), UINT32(0) }));
//    }

    // 3. 调用 call 指令(处理地址), 响应的结果在 rax 中
    slice_push(operations, ASM_INST("call", { first }));

    // 4. 响应处理(取出响应值传递给 result), result 已经固定分配了 rax/xmm0 寄存器,所以 move result to rax 不是必要的
    if (op->output != NULL) {
        assert(op->output->assert_type == LIR_OPERAND_REG);
        reg_t *reg = op->output->value;
        assert(reg->index == 0); // rax or xmm0 index == 0
    }

    return operations;
}

// lir GT foo,bar => result
static slice_t *amd64_native_sgt(closure_t *c, lir_op_t *op) {
    assert(op->first->assert_type == LIR_OPERAND_REG);
    slice_t *operations = slice_new();

    asm_operand_t *first = lir_operand_transform(c, operations, op->first);
    asm_operand_t *second = lir_operand_transform(c, operations, op->second);
    asm_operand_t *result = lir_operand_transform(c, operations, op->output);
    assert(result->size == BYTE);

    // bool = int64 > int64
    slice_push(operations, ASM_INST("cmp", { first, second }));

    // setg r/m8
    slice_push(operations, ASM_INST("setg", { result }));

    return operations;
}


static slice_t *amd64_native_label(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    lir_symbol_label_t *label_operand = op->output->value;
    slice_push(operations, ASM_INST("label", { SYMBOL(label_operand->ident, label_operand->is_local) }));
    return operations;
}

/**
 * 形参的处理已经在 lower 阶段完成，这里只需要做一下 rsp 保存等栈帧工作即可
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_fn_begin(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    // c->stack_slot 为负数值(spill 期间负数增长, 所以次数需要取 abs 进行操作)
    int stack_slot = abs(c->stack_offset);
    stack_slot = memory_align(stack_slot, ALIGN_SIZE);

    slice_push(operations, ASM_INST("push", { REG(rbp) }));
    slice_push(operations, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    if (c->stack_offset != 0) {
        slice_push(operations, ASM_INST("sub", { REG(rsp), UINT32(stack_slot) }));
    }

    return operations;
}


/**
 * 拼接在函数尾部，实现结尾块自然退出。
 * 齐操作和 fn begin 相反
 * @param c
 * @return
 */
slice_t *amd64_native_fn_end(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    slice_push(operations, ASM_INST("mov", { REG(rsp), REG(rbp) }));
    slice_push(operations, ASM_INST("pop", { REG(rbp) }));
    slice_push(operations, ASM_INST("ret", {}));
    return operations;
}

/**
 * example:
 * lea imm(string_raw), reg
 * lea var,  reg
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_lea(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    asm_operand_t *first = lir_operand_transform(c, operations, op->first);

    asm_operand_t *result = lir_operand_transform(c, operations, op->output);

    slice_push(operations, ASM_INST("lea", { result, first }));
    return operations;
}

/**
 * 比较 first 和 second 的值，如果相等就调整到 output label
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_beq(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);
    assert(op->first->assert_type == LIR_OPERAND_REG || op->second->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    asm_operand_t *first = lir_operand_transform(c, operations, op->first);

    asm_operand_t *second = lir_operand_transform(c, operations, op->second);

    asm_operand_t *result = lir_operand_transform(c, operations, op->output);

    // cmp 指令比较
    slice_push(operations, ASM_INST("cmp", { first, second }));
    slice_push(operations, ASM_INST("je", { result }));

    return operations;
}


amd64_native_fn amd64_native_table[] = {
        [LIR_OPCODE_CLR] = amd64_native_clr,
        [LIR_OPCODE_ADD] = amd64_native_add,
        [LIR_OPCODE_SUB] = amd64_native_sub,
        [LIR_OPCODE_CALL] = amd64_native_call,
        [LIR_OPCODE_BUILTIN_CALL] = amd64_native_call,
        [LIR_OPCODE_RUNTIME_CALL] = amd64_native_call,
        [LIR_OPCODE_LABEL] = amd64_native_label,
        [LIR_OPCODE_PUSH] = amd64_native_push,
        [LIR_OPCODE_RETURN] = amd64_native_return,
        [LIR_OPCODE_BEQ] = amd64_native_beq,
        [LIR_OPCODE_BAL] = amd64_native_bal,
        [LIR_OPCODE_SGT] = amd64_native_sgt,
        [LIR_OPCODE_MOVE] = amd64_native_mov,
        [LIR_OPCODE_LEA] = amd64_native_lea,
        [LIR_OPCODE_FN_BEGIN] = amd64_native_fn_begin,
        [LIR_OPCODE_FN_END] = amd64_native_fn_end,
};


slice_t *amd64_native_op(closure_t *c, lir_op_t *op) {
    amd64_native_fn fn = amd64_native_table[op->code];
    assert(fn && "amd64 native op not support");
    return fn(c, op);
}

slice_t *amd64_native_block(closure_t *c, basic_block_t *block) {
    slice_t *operations = slice_new();
    list_node *current = list_first(block->operations);
    while (current->value != NULL) {
        lir_op_t *op = current->value;
        slice_concat(operations, amd64_native_op(c, op));
        current = current->succ;
    }
    return operations;
}


void amd64_native(closure_t *c) {
    assert(c->module);

    // 遍历 block
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        slice_concat(c->asm_operations, amd64_native_block(c, block));
    }
}
