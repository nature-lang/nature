#include "amd64.h"
#include "src/register/amd64.h"
#include "src/debug/debug.h"
#include <assert.h>

static char *asm_setcc_trans[] = {
        [LIR_OPCODE_SLT] = "setl",
        [LIR_OPCODE_SLE] = "setle",
        [LIR_OPCODE_SGT] = "setg",
        [LIR_OPCODE_SGE] = "setge",
        [LIR_OPCODE_SEE] = "sete",
        [LIR_OPCODE_SNE] = "setne",
};

static asm_operand_t *amd64_fit_number(uint8_t size, int64_t number) {
    if (size == BYTE) {
        return UINT8(number);
    }
    if (size == WORD) {
        return UINT16(number);
    }
    if (size == DWORD) {
        return UINT32(number);
    }
    if (size == QWORD) {
        return UINT64(number);
    }

    assert(false);
}

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
static asm_operand_t *lir_operand_trans(closure_t *c, slice_t *operations, lir_operand_t *operand) {
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
        assert(v->kind != TYPE_RAW_STRING && v->kind != TYPE_FLOAT);
        if (v->kind == TYPE_INT || v->kind == TYPE_UINT) {
            return UINT(v->int_value); // 默认使用 UINT32,v->int_value 真的大于 32 位时使用 64
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8) {
            return UINT8(v->int_value);
        } else if (v->kind == TYPE_INT16 || v->kind == TYPE_UINT16) {
            return UINT16(v->int_value);
        } else if (v->kind == TYPE_INT32 || v->kind == TYPE_UINT64) {
            return UINT32(v->int_value);
        } else if (v->kind == TYPE_INT64 || v->kind == TYPE_UINT64) {
            return UINT64(v->int_value);
        } else if (v->kind == TYPE_FLOAT || v->kind == TYPE_FLOAT32) {
            return FLOAT32(v->float_value);
        } else if (v->kind == TYPE_FLOAT64) {
            return FLOAT64(v->float_value);
        } else if (v->kind == TYPE_BOOL) {
            return UINT8(v->bool_value);
        }
        assert(false && "code immediate not expected");
    }

    // mov $1, %rax 是将 $1 写入到 rax 寄存器
    // mov $1, (%rax)  // rax 中必须存储一个合法的虚拟地址，将 $1 写入到 rax 存储的地址处，最常用的地方就是 rbp 栈栈中的使用
    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *indirect = operand->value;
        lir_operand_t *base = indirect->base;
        assertf(base->assert_type == LIR_OPERAND_REG, "indirect addr base must be reg");

        // 虽然栈的增长方向是相反的，但是数据存储总是正向的
        reg_t *reg = base->value;
        asm_operand_t *asm_operand;
        if (indirect->offset == 0) {
            asm_operand = INDIRECT_REG(reg, type_kind_sizeof(indirect->type.kind));
        } else {
            asm_operand = DISP_REG(reg, indirect->offset, type_kind_sizeof(indirect->type.kind));
        }

        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *v = operand->value;
        asm_operand_t *asm_operand = SYMBOL(v->ident, false);
        // symbol 也要有 size, 不然无法选择合适的寄存器进行 mov!
        asm_operand->size = type_kind_sizeof(v->type);
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
//    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);
    slice_t *operations = slice_new();
    asm_operand_t *first = lir_operand_trans(c, operations, op->first);
    asm_operand_t *output = lir_operand_trans(c, operations, op->output);

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
//static slice_t *amd64_native_return(closure_t *c, lir_op_t *op) {
//    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
//    slice_t *operations = slice_new();
//
//    // compiler 阶段已经附加了 bal end fn 的指令
//    // lower 阶段已经将返回值放在了 rax/xmm0 中
//    // 所以这里什么都不用做
//
//    return operations;
//}

static slice_t *amd64_native_push(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    asm_operand_t *first = lir_operand_trans(c, operations, op->first);
    slice_push(operations, ASM_INST("push", { first }));
    return operations;
}

static slice_t *amd64_native_bal(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);

    slice_t *operations = slice_new();
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);
    slice_push(operations, ASM_INST("jmp", { result }));
    return operations;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_clv(closure_t *c, lir_op_t *op) {
    lir_operand_t *output = op->output;
    assert(output->assert_type == LIR_OPERAND_REG || output->assert_type == LIR_OPERAND_STACK);
    assert(output);

    slice_t *operations = slice_new();
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);

    if (output->assert_type == LIR_OPERAND_REG) {
        slice_push(operations, ASM_INST("xor", { result, result }));
        return operations;
    }

    if (output->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = output->value;
        assertf(stack->size <= AMD64_PTR_SIZE, "only can clv size <= %d, actual=%d", AMD64_PTR_SIZE, stack->size);
        // amd64 目前仅支持
        // MOV r/m8, imm8
        // MOV r/m8***, imm8
        // MOV r/m16, imm16
        // MOV r/m32, imm32
        uint8_t size = stack->size > DWORD ? DWORD : stack->size;
        slice_push(operations, ASM_INST("mov", { result, amd64_fit_number(size, 0) }));
        return operations;
    }

    assert(false);
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
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);
    slice_push(operations, ASM_INST("xor", { result, result }));

    return operations;
}

/**
 * 经过 lower, 这里的指令是 div rax,operand -> rax
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_div(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    assertf(op->first->assert_type == LIR_OPERAND_REG, "div op first must reg");
    assertf(op->second->assert_type != LIR_OPERAND_IMM, "div op second cannot imm");
    assertf(op->output->assert_type == LIR_OPERAND_REG, "div op output must reg");
    reg_t *first_reg = op->first->value;
    reg_t *output_reg = op->output->value;
    assertf(first_reg->index == rax->index, "div op first reg must rax");
    assertf(output_reg->index == rax->index, "div op output reg must rax");

    asm_operand_t *second = lir_operand_trans(c, operations, op->second);

    slice_push(operations, ASM_INST("idiv", { second }));
    return operations;
}

/**
 * 经过 lower, 这里的指令是 div rax,operand -> rax
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_mul(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    assertf(op->first->assert_type == LIR_OPERAND_REG, "mul op first must reg");
    assertf(op->second->assert_type != LIR_OPERAND_IMM, "mul op second cannot imm");
    assertf(op->output->assert_type == LIR_OPERAND_REG, "mul op output must reg");
    reg_t *first_reg = op->first->value;
    reg_t *output_reg = op->output->value;
    assertf(first_reg->index == rax->index, "mul op first reg must rax");
    assertf(output_reg->index == rax->index, "mul op output reg must rax");


    asm_operand_t *second = lir_operand_trans(c, operations, op->second);
    slice_push(operations, ASM_INST("imul", { second }));
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
    asm_operand_t *first = lir_operand_trans(c, operations, op->first);
    asm_operand_t *second = lir_operand_trans(c, operations, op->second);
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);

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
    asm_operand_t *first = lir_operand_trans(c, operations, op->first); // rcx
    asm_operand_t *second = lir_operand_trans(c, operations, op->second); // rax
    asm_operand_t *result = lir_operand_trans(c, operations, op->output); // rax


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
    asm_operand_t *first = lir_operand_trans(c, operations, op->first);

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
static slice_t *amd64_native_scc(closure_t *c, lir_op_t *op) {
    assert(op->first->assert_type == LIR_OPERAND_REG);
    slice_t *operations = slice_new();

    asm_operand_t *first = lir_operand_trans(c, operations, op->first);
    asm_operand_t *second = lir_operand_trans(c, operations, op->second);
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);
    assert(result->size == BYTE);

    // bool = int64 > int64
    slice_push(operations, ASM_INST("cmp", { first, second }));

    // setg r/m8
    char *asm_code = asm_setcc_trans[op->code];
    assertf(asm_code, "not found op->code map asm->code");
    slice_push(operations, ASM_INST(asm_code, { result }));

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
    // 进行最终的对齐, linux amd64 中栈一般都是是按 16byte 对齐的
    c->stack_offset = align(c->stack_offset, ALIGN_SIZE);

    slice_push(operations, ASM_INST("push", { REG(rbp) }));
    slice_push(operations, ASM_INST("mov", { REG(rbp), REG(rsp) })); // 保存栈指针
    if (c->stack_offset != 0) {
        slice_push(operations, ASM_INST("sub", { REG(rsp), UINT32(c->stack_offset) }));
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
 * examples:
 * lea imm(string_raw), reg
 * lea var,  reg
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_lea(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    asm_operand_t *first = lir_operand_trans(c, operations, op->first);
    asm_operand_t *result = lir_operand_trans(c, operations, op->output);

    // lea 取的是 first 的地址，amd64 下一定是 8byte
    first->size = QWORD;

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
    asm_operand_t *first = lir_operand_trans(c, operations, op->first);

    asm_operand_t *second = lir_operand_trans(c, operations, op->second);

    asm_operand_t *result = lir_operand_trans(c, operations, op->output);

    // cmp 指令比较
    slice_push(operations, ASM_INST("cmp", { first, second }));
    slice_push(operations, ASM_INST("je", { result }));

    return operations;
}


amd64_native_fn amd64_native_table[] = {
        [LIR_OPCODE_CLR] = amd64_native_clr,
        [LIR_OPCODE_CLV] = amd64_native_clv,
        [LIR_OPCODE_ADD] = amd64_native_add,
        [LIR_OPCODE_SUB] = amd64_native_sub,
        [LIR_OPCODE_DIV] = amd64_native_div,
        [LIR_OPCODE_MUL] = amd64_native_mul,
        [LIR_OPCODE_CALL] = amd64_native_call,
        [LIR_OPCODE_RT_CALL] = amd64_native_call,
        [LIR_OPCODE_LABEL] = amd64_native_label,
        [LIR_OPCODE_PUSH] = amd64_native_push,
//        [LIR_OPCODE_RETURN] = amd64_native_return,
        [LIR_OPCODE_BEQ] = amd64_native_beq,
        [LIR_OPCODE_BAL] = amd64_native_bal,
        // 逻辑相关运算符
        [LIR_OPCODE_SGT] = amd64_native_scc,
        [LIR_OPCODE_SGE] = amd64_native_scc,
        [LIR_OPCODE_SLT] = amd64_native_scc,
        [LIR_OPCODE_SLE] = amd64_native_scc,
        [LIR_OPCODE_SEE] = amd64_native_scc,
        [LIR_OPCODE_SNE] = amd64_native_scc,

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
    linked_node *current = linked_first(block->operations);
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
