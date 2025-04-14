#include "amd64.h"
#include "src/debug/debug.h"
#include "src/register/arch/amd64.h"
#include <assert.h>

static char *asm_setcc_integer_trans[] = {
        [LIR_OPCODE_SLT] = "setl",
        [LIR_OPCODE_USLT] = "setb",
        [LIR_OPCODE_SLE] = "setle",
        [LIR_OPCODE_SGT] = "setg",
        [LIR_OPCODE_SGE] = "setge",
        [LIR_OPCODE_SEE] = "sete",
        [LIR_OPCODE_SNE] = "setne",
};

static char *asm_setcc_float_trans[] = {
        [LIR_OPCODE_SLT] = "setb",
        [LIR_OPCODE_SLE] = "setbe",
        [LIR_OPCODE_SGT] = "seta",
        [LIR_OPCODE_SGE] = "setae",
        [LIR_OPCODE_SEE] = "sete",
        [LIR_OPCODE_SNE] = "setne",
};

static slice_t *amd64_native_block(closure_t *c, basic_block_t *block);

typedef slice_t *(*amd64_native_fn)(closure_t *c, lir_op_t *op);

/**
 * 分发入口, 基于 op->code 做选择(包含 label code)
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_op(closure_t *c, lir_op_t *op);


static amd64_asm_operand_t *amd64_fit_number(uint8_t size, int64_t number) {
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

static bool amd64_is_integer_operand(lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return reg->flag & FLAG(LIR_FLAG_ALLOC_INT);
    }

    return is_integer(operand_type_kind(operand));
}

static bool asm_operand_equal(amd64_asm_operand_t *a, amd64_asm_operand_t *b) {
    if (a->type != b->type) {
        return false;
    }
    if (a->size != b->size) {
        return false;
    }

    if (a->type == AMD64_ASM_OPERAND_TYPE_REG || a->type == AMD64_ASM_OPERAND_TYPE_FREG) {
        reg_t *reg_a = a->value;
        reg_t *reg_b = b->value;
        return str_equal(reg_a->name, reg_b->name);
    }

    return false;
}

static void asm_mov(slice_t *operations, lir_op_t *op, amd64_asm_operand_t *dst, amd64_asm_operand_t *src) {
    if (asm_operand_equal(dst, src)) {
        return;
    }

    amd64_asm_inst_t *operation = AMD64_INST("mov", dst, src);
    slice_push(operations, operation);
}

/**
 * lir_operand 中不能直接转换为 asm_operand 的参数
 * type_string/lir_operand_memory
 * @param operand
 * @param asm_operand
 * @return
 */
static amd64_asm_operand_t *lir_operand_trans_amd64(closure_t *c, lir_op_t *op, lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return AMD64_REG(reg);
    }

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        return DISP_REG(rbp, stack->slot, stack->size);
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *v = operand->value;
        assert(v->kind != TYPE_RAW_STRING && v->kind != TYPE_FLOAT);
        if (v->kind == TYPE_INT || v->kind == TYPE_UINT || v->kind == TYPE_INT64 || v->kind == TYPE_UINT64 ||
            v->kind == TYPE_ANYPTR) {
            // mov r64,imm64 转换成 mov rm64,imm32
            if (v->int_value > INT32_MAX || v->int_value < INT32_MIN) {
                return UINT64(v->uint_value);
            }
            return UINT32(v->uint_value);
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8) {
            return UINT8(v->uint_value);
        } else if (v->kind == TYPE_INT16 || v->kind == TYPE_UINT16) {
            return UINT16(v->uint_value);
        } else if (v->kind == TYPE_INT32 || v->kind == TYPE_UINT32) {
            return UINT32(v->uint_value);
        } else if (v->kind == TYPE_FLOAT32) {
            return FLOAT32(v->f64_value);
        } else if (v->kind == TYPE_FLOAT || v->kind == TYPE_FLOAT64) {
            return FLOAT64(v->f64_value);
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

        // rbp 特殊编码
        if (base->assert_type == LIR_OPERAND_STACK) {
            assert(op->code == LIR_OPCODE_LEA);
            lir_stack_t *stack = base->value;
            stack->slot += indirect->offset;
            return DISP_REG(rbp, stack->slot, stack->size);
        }

        assertf(base->assert_type == LIR_OPERAND_REG, "indirect addr base must be reg");

        // 虽然栈的增长方向是相反的，但是数据存储总是正向的
        reg_t *reg = base->value;
        amd64_asm_operand_t *asm_operand;

        // r12/rsp 特殊编码
        if (reg->index == rsp->index || reg->index == r12->index) {
            // 对于 RSP 和 R12，总是使用 SIB 编码
            return SIB_REG(reg, NULL, 0, indirect->offset, type_kind_sizeof(indirect->type.kind));
        }

        // TODO rbp/ebp 等寄存器不能使用 indirect_reg, 例如: 0:  4c 8b 45 00    mov    r8,QWORD PTR [rbp+0x0]
        if (indirect->offset == 0) {
            asm_operand = INDIRECT_REG(reg, type_kind_sizeof(indirect->type.kind));
        } else {
            asm_operand = DISP_REG(reg, indirect->offset, type_kind_sizeof(indirect->type.kind));
        }

        assert(asm_operand->size > 0);
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *v = operand->value;
        amd64_asm_operand_t *asm_operand = AMD64_SYMBOL(v->ident, false);
        // symbol 也要有 size, 不然无法选择合适的寄存器进行 mov!
        asm_operand->size = type_kind_sizeof(v->kind);
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return AMD64_SYMBOL(v->ident, false);
    }

    assert(false && "operand type not expected");
}


//static asm_operation_t *reg_cleanup(reg_t *reg) {
//    // TODO ah/bh/ch/dh 不能这么清理
//
//    reg_t *r = (reg_t *) reg_find(reg->index, QWORD);
//    assert(r && "reg not found");
//    return ASM_INST("xor", { REG(r), REG(r) });
//}

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
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    asm_mov(operations, op, output, first);
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

static slice_t *amd64_native_skip(closure_t *c, lir_op_t *op) {
    return slice_new();
}

static slice_t *amd64_native_push(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    slice_push(operations, AMD64_INST("push", first));
    return operations;
}

static slice_t *amd64_native_bal(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);

    slice_t *operations = slice_new();
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);
    slice_push(operations, AMD64_INST("jmp", result)); // result is symbol label
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
    assert(output->assert_type == LIR_OPERAND_REG ||
           output->assert_type == LIR_OPERAND_STACK ||
           output->assert_type == LIR_OPERAND_INDIRECT_ADDR);
    assert(output);

    slice_t *operations = slice_new();
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    if (output->assert_type == LIR_OPERAND_REG) {
        slice_push(operations, AMD64_INST("xor", result, result));
        return operations;
    }

    if (output->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = output->value;
        assertf(stack->size <= QWORD, "only can clv size <= %d, actual=%d", QWORD, stack->size);
        // amd64 目前仅支持
        // MOV rm8, imm8
        // MOV rm8***, imm8
        // MOV rm16, imm16
        // MOV rm32, imm32
        // MOV rm64, imm32
        uint8_t size = stack->size > DWORD ? DWORD : stack->size;
        slice_push(operations, AMD64_INST("mov", result, amd64_fit_number(size, 0)));
        return operations;
    }

    if (output->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *temp = output->value;
        uint16_t size = type_sizeof(temp->type);

        assertf(size <= QWORD, "only can clv size <= %d, actual=%d", QWORD, size);
        // amd64 目前仅支持
        // MOV rm8, imm8
        // MOV rm8***, imm8
        // MOV rm16, imm16
        // MOV rm32, imm32
        // MOV rm64, imm32
        size = size > DWORD ? DWORD : size;
        slice_push(operations, AMD64_INST("mov", result, amd64_fit_number(size, 0)));
        return operations;
    }


    assert(false);
}

static slice_t *amd64_native_nop(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
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
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);
    slice_push(operations, AMD64_INST("xor", result, result));

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
    assertf(op->output->assert_type == LIR_OPERAND_REG, "div op output must reg");

    if (amd64_is_integer_operand(op->output)) {
        assertf(op->first->assert_type == LIR_OPERAND_REG, "div op first must reg");
        assertf(op->second->assert_type != LIR_OPERAND_IMM, "div op second cannot imm");
        reg_t *first_reg = op->first->value;
        reg_t *output_reg = op->output->value;
        assertf(first_reg->index == rax->index, "div op first reg must rax");
        assertf(output_reg->index == rax->index || output_reg->index == rdx->index,
                "div op output reg must rax/rdx");

        amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);

        slice_push(operations, AMD64_INST("idiv", second));
        return operations;
    }

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    assert(!asm_operand_equal(second, result));
    // 浮点数是 amd64 常见的双操作数指令 mulss rm -> reg
    asm_mov(operations, op, result, first);
    slice_push(operations, AMD64_INST("div", result, second));
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
    assertf(op->output->assert_type == LIR_OPERAND_REGS || op->output->assert_type == LIR_OPERAND_REG, "mul op output must reg");

    if (op->output->assert_type == LIR_OPERAND_REGS) {
        assertf(op->first->assert_type == LIR_OPERAND_REG, "mul op first must reg");
        assertf(op->second->assert_type != LIR_OPERAND_IMM, "mul op second cannot imm");

        reg_t *first_reg = op->first->value;
        reg_t *output_reg = op->output->value;

        assertf(first_reg->index == rax->index, "mul op first reg must rax");

        amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
        slice_push(operations, AMD64_INST("imul", second));
        return operations;
    }

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    assert(!asm_operand_equal(second, result));
    // 浮点数是 amd64 常见的双操作数指令 mulss rm -> reg
    asm_mov(operations, op, result, first);
    slice_push(operations, AMD64_INST("mul", result, second));
    return operations;
}

/**
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_neg(closure_t *c, lir_op_t *op) {
    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 必须先将 result 中存储目标值，在基于 result 做 neg, 这样才不会破坏 first 中的值
    asm_mov(operations, op, result, first);
    slice_push(operations, AMD64_INST("neg", result));

    return operations;
}


/**
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_xor(closure_t *c, lir_op_t *op) {
    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second); // float mask
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 必须先将 result 中存储目标值，在基于 result 做 neg, 这样才不会破坏 first 中的值
    assert(asm_operand_equal(first, result));

    slice_push(operations, AMD64_INST("xor", result, second));

    return operations;
}

/**
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_or(closure_t *c, lir_op_t *op) {
    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second); // float mask
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 必须先将 result 中存储目标值，在基于 result 做 neg, 这样才不会破坏 first 中的值
    assert(asm_operand_equal(first, result));

    slice_push(operations, AMD64_INST("or", result, second));

    return operations;
}

/**
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_and(closure_t *c, lir_op_t *op) {
    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second); // float mask
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 必须先将 result 中存储目标值，在基于 result 做 neg, 这样才不会破坏 first 中的值
    assert(asm_operand_equal(first, result));

    slice_push(operations, AMD64_INST("and", result, second));

    return operations;
}

/**
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_shift(closure_t *c, lir_op_t *op) {
    assert(op->second->assert_type == LIR_OPERAND_REG);
    reg_t *second_reg = op->second->value;
    assert(str_equal(second_reg->name, "cl"));

    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second); // float mask
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    assert(asm_operand_equal(first, result));

    char *opcode;
    if (op->code == LIR_OPCODE_SHR) {
        opcode = "sar";
    } else {
        opcode = "sal";
    }

    slice_push(operations, AMD64_INST(opcode, result, second));

    return operations;
}

/**
 * not first ->  output
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_not(closure_t *c, lir_op_t *op) {
    assert(op->first->assert_type == LIR_OPERAND_REG || op->output->assert_type == LIR_OPERAND_REG);

    // 由于存在 mov 操作，所以必须有一个操作数分配到寄存器
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 必须先将 result 中存储目标值，在基于 result 做 neg, 这样才不会破坏 first 中的值
    asm_mov(operations, op, result, first);

    slice_push(operations, AMD64_INST("not", result));

    return operations;
}


/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
static slice_t *amd64_native_add(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 由于需要 first -> result 进行覆盖，所以 second 和 result 不允许是统一地址或者 reg
    assert(asm_operand_equal(first, result));

    slice_push(operations, AMD64_INST("add", result, second));

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
    slice_t *operations = slice_new();

    // 参数转换
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first); // rcx
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second); // rax
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output); // rax


    assert(asm_operand_equal(first, result));

    //  sub imm -> result
    slice_push(operations, AMD64_INST("sub", result, second));

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

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);

    slice_push(operations, AMD64_INST("call", first));

    return operations;
}

static slice_t *amd64_native_trunc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);
    int64_t size = op->output->size;
    assert(size > 0);

    // 使用 and 指令进行截断
    if (output->size == BYTE) {
        slice_push(operations, AMD64_INST("and", output, UINT32(0xff)));
    } else if (output->size == WORD) {
        slice_push(operations, AMD64_INST("and", output, UINT32(0xffff)));
    } else if (output->size == DWORD) {
        slice_push(operations, AMD64_INST("and", output, UINT32(0xffffffff)));
    }

    return operations;
}

static slice_t *amd64_native_sext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    slice_push(operations, AMD64_INST("movsx", output, first));
    return operations;
}

static slice_t *amd64_native_zext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    slice_push(operations, AMD64_INST("movzx", output, first));

    return operations;
}


// result = foo > bar
// lir GT foo,bar => result // foo GT bar // foo > bar // foo - bar > 0
//  foo(dst) - bar(src)
static slice_t *amd64_native_scc(closure_t *c, lir_op_t *op) {
    assert(op->first->assert_type == LIR_OPERAND_REG || op->second->assert_type == LIR_OPERAND_REG);
    slice_t *operations = slice_new();

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);
    assert(result->size == BYTE);

    // cmp dst, src 也就是 dst - src 的结果
    slice_push(operations, AMD64_INST("cmp", first, second));

    char *asm_code;
    if (amd64_is_integer_operand(op->first)) {
        // seta r/m8, dst - src > 0 也就是 dst > src 时, cf = zf = 0, seta 就是这两个为 0 时将结果设置为 1
        // 也就是 dst > src 时，将 1 写入到结果中
        asm_code = asm_setcc_integer_trans[op->code];
    } else {
        asm_code = asm_setcc_float_trans[op->code];
    }

    assertf(asm_code, "not found op_code map asm_code");
    slice_push(operations, AMD64_INST(asm_code, result));

    return operations;
}


static slice_t *amd64_native_label(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    lir_symbol_label_t *label_operand = op->output->value;
    slice_push(operations, AMD64_INST("label", AMD64_SYMBOL(label_operand->ident, label_operand->is_local)));
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

    int64_t offset = c->stack_offset;
    offset += c->call_stack_max_offset;

    // 进行最终的对齐, linux amd64 中栈一般都是是按 16byte 对齐的
    offset = align_up(offset, AMD64_STACK_ALIGN_SIZE);

    slice_push(operations, AMD64_INST("push", AMD64_REG(rbp))); // push 会移动 rsp 至臻，所以不需要再次处理
    slice_push(operations, AMD64_INST("mov", AMD64_REG(rbp), AMD64_REG(rsp))); // 保存栈指针
    if (offset != 0) {
        slice_push(operations, AMD64_INST("sub", AMD64_REG(rsp), UINT32(offset)));
    }

    //    c->stack_offset = offset;
    // gc_bits 补 0
    if (c->call_stack_max_offset) {
        uint16_t bits_start = c->stack_offset / POINTER_SIZE;
        uint16_t bits_count = c->call_stack_max_offset / POINTER_SIZE;
        for (int i = 0; i < bits_count; ++i) {
            bitmap_grow_set(c->stack_gc_bits, bits_start + i, 0);
        }
    }

    c->stack_offset = offset;
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
    slice_push(operations, AMD64_INST("mov", AMD64_REG(rsp), AMD64_REG(rbp)));
    slice_push(operations, AMD64_INST("pop", AMD64_REG(rbp)));
    slice_push(operations, AMD64_INST("ret"));
    return operations;
}

/**
 * examples:
 * lea imm(string_ref), reg
 * lea var,  reg
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_lea(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // lea 取的是 first 的地址，amd64 下一定是 8byte
    first->size = QWORD;

    slice_push(operations, AMD64_INST("lea", result, first));
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
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);

    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);

    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // cmp 指令比较
    slice_push(operations, AMD64_INST("cmp", first, second));
    slice_push(operations, AMD64_INST("je", result));

    return operations;
}


amd64_native_fn amd64_native_table[] = {
        [LIR_OPCODE_CLR] = amd64_native_clr,
        [LIR_OPCODE_CLV] = amd64_native_clv,
        [LIR_OPCODE_NOP] = amd64_native_nop,
        [LIR_OPCODE_CALL] = amd64_native_call,
        [LIR_OPCODE_RT_CALL] = amd64_native_call,
        [LIR_OPCODE_LABEL] = amd64_native_label,
        [LIR_OPCODE_PUSH] = amd64_native_push,
        [LIR_OPCODE_RETURN] = amd64_native_nop,
        [LIR_OPCODE_BREAK] = amd64_native_nop,
        [LIR_OPCODE_BEQ] = amd64_native_beq,
        [LIR_OPCODE_BAL] = amd64_native_bal,

        // 类型扩展
        [LIR_OPCODE_ZEXT] = amd64_native_zext,
        [LIR_OPCODE_SEXT] = amd64_native_sext,
        [LIR_OPCODE_TRUNC] = amd64_native_trunc,

        // 一元运算符
        [LIR_OPCODE_NEG] = amd64_native_neg,

        // 位运算
        [LIR_OPCODE_XOR] = amd64_native_xor,
        [LIR_OPCODE_NOT] = amd64_native_not,
        [LIR_OPCODE_OR] = amd64_native_or,
        [LIR_OPCODE_AND] = amd64_native_and,
        [LIR_OPCODE_SHR] = amd64_native_shift,
        [LIR_OPCODE_SHL] = amd64_native_shift,

        // 算数运算
        [LIR_OPCODE_ADD] = amd64_native_add,
        [LIR_OPCODE_SUB] = amd64_native_sub,
        [LIR_OPCODE_DIV] = amd64_native_div,
        [LIR_OPCODE_MUL] = amd64_native_mul,
        // 逻辑相关运算符
        [LIR_OPCODE_SGT] = amd64_native_scc,
        [LIR_OPCODE_SGE] = amd64_native_scc,
        [LIR_OPCODE_SLT] = amd64_native_scc,
        [LIR_OPCODE_SLE] = amd64_native_scc,
        [LIR_OPCODE_SEE] = amd64_native_scc,
        [LIR_OPCODE_SNE] = amd64_native_scc,

        [LIR_OPCODE_USLT] = amd64_native_scc,

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

static slice_t *amd64_native_block(closure_t *c, basic_block_t *block) {
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
