#include "amd64.h"
#include "src/debug/debug.h"
#include "src/register/arch/amd64.h"
#include <assert.h>

static char *asm_setcc_integer_trans[] = {
    [LIR_OPCODE_SLT] = "setl",
    [LIR_OPCODE_USLT] = "setb",
    [LIR_OPCODE_SLE] = "setle",
    [LIR_OPCODE_USLE] = "setbe",
    [LIR_OPCODE_SGT] = "setg",
    [LIR_OPCODE_USGT] = "seta",
    [LIR_OPCODE_SGE] = "setge",
    [LIR_OPCODE_USGE] = "setae",
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


static char *asm_bcc_integer_trans[] = {
    [LIR_OPCODE_BLT] = "jl",
    [LIR_OPCODE_BLE] = "jle",
    [LIR_OPCODE_BGT] = "jg",
    [LIR_OPCODE_BGE] = "jge",
    [LIR_OPCODE_BULT] = "jb",
    [LIR_OPCODE_BULE] = "jbe",
    [LIR_OPCODE_BUGT] = "ja",
    [LIR_OPCODE_BUGE] = "jae",
    [LIR_OPCODE_BEE] = "je",
    [LIR_OPCODE_BNE] = "jne",
};

static char *asm_bcc_float_trans[] = {
    [LIR_OPCODE_BLT] = "jb",
    [LIR_OPCODE_BLE] = "jbe",
    [LIR_OPCODE_BGT] = "ja",
    [LIR_OPCODE_BGE] = "jae",
    [LIR_OPCODE_BEE] = "je",
    [LIR_OPCODE_BNE] = "jne",
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
        return AMD64_UINT8(number);
    }
    if (size == WORD) {
        return AMD64_UINT16(number);
    }
    if (size == DWORD) {
        return AMD64_UINT32(number);
    }
    if (size == QWORD) {
        return AMD64_UINT64(number);
    }

    assert(false);
}

static bool amd64_is_integer_operand(lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return reg->flag & FLAG(LIR_FLAG_ALLOC_INT);
    }

    return is_integer_or_anyptr(lir_operand_type(operand).map_imm_kind);
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
        int64_t sp_offset = c->stack_offset - (-stack->slot);
        return SIB_REG(rsp, NULL, 0, sp_offset, stack->size);

        //        return DISP_REG(rbp, stack->slot, stack->size);
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *v = operand->value;
        assert(v->kind != TYPE_RAW_STRING && v->kind != TYPE_FLOAT);
        if (v->kind == TYPE_INT || v->kind == TYPE_UINT || v->kind == TYPE_INT64 || v->kind == TYPE_UINT64 ||
            v->kind == TYPE_ANYPTR) {
            // mov r64,imm64 转换成 mov rm64,imm32
            if (v->int_value > INT32_MAX || v->int_value < INT32_MIN) {
                return AMD64_UINT64(v->uint_value);
            }
            return AMD64_UINT32(v->uint_value);
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8) {
            return AMD64_UINT8(v->uint_value);
        } else if (v->kind == TYPE_INT16 || v->kind == TYPE_UINT16) {
            return AMD64_UINT16(v->uint_value);
        } else if (v->kind == TYPE_INT32 || v->kind == TYPE_UINT32) {
            return AMD64_UINT32(v->uint_value);
        } else if (v->kind == TYPE_FLOAT32) {
            return AMD64_FLOAT32(v->f64_value);
        } else if (v->kind == TYPE_FLOAT || v->kind == TYPE_FLOAT64) {
            return AMD64_FLOAT64(v->f64_value);
        } else if (v->kind == TYPE_BOOL) {
            return AMD64_UINT8(v->bool_value);
        }
        assert(false && "code immediate not expected");
    }

    // mov $1, %rax 是将 $1 写入到 rax 寄存器
    // mov $1, (%rax)  // rax 中必须存储一个合法的虚拟地址，将 $1 写入到 rax 存储的地址处，最常用的地方就是 rbp 栈栈中的使用
    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *indirect = operand->value;
        lir_operand_t *base = indirect->base;

        // LEA 融合场景: base 为 NULL，只有 index 和 scale
        // 生成 [index*scale + disp]
        if (base == NULL && indirect->index) {
            assertf(indirect->index->assert_type == LIR_OPERAND_REG,
                    "indirect addr index must be reg for LEA fusion");
            reg_t *index_reg = indirect->index->value;
            return SIB_REG(NULL, index_reg, indirect->scale,
                           indirect->offset, type_kind_sizeof(indirect->type.map_imm_kind));
        }

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

        // 如果有 index，使用 SIB 编码 [base + index*scale + disp]
        if (indirect->index) {
            assertf(indirect->index->assert_type == LIR_OPERAND_REG,
                    "indirect addr index must be reg");
            reg_t *index_reg = indirect->index->value;
            return SIB_REG(reg, index_reg, indirect->scale,
                           indirect->offset, type_kind_sizeof(indirect->type.map_imm_kind));
        }

        // r12/rsp 特殊编码
        if (reg->index == rsp->index || reg->index == r12->index) {
            // 对于 RSP 和 R12，总是使用 SIB 编码
            return SIB_REG(reg, NULL, 0, indirect->offset, type_kind_sizeof(indirect->type.map_imm_kind));
        }

        // TODO rbp/ebp 等寄存器不能使用 indirect_reg, 例如: 0:  4c 8b 45 00    mov    r8,QWORD PTR [rbp+0x0]
        if (indirect->offset == 0) {
            asm_operand = INDIRECT_REG(reg, type_kind_sizeof(indirect->type.map_imm_kind));
        } else {
            asm_operand = DISP_REG(reg, indirect->offset, type_kind_sizeof(indirect->type.map_imm_kind));
        }

        assert(asm_operand->size > 0);
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *v = operand->value;
        amd64_asm_operand_t *asm_operand = AMD64_SYMBOL(v->ident, false);
        // symbol 也要有 size, 不然无法选择合适的寄存器进行 mov!
        asm_operand->size = v->t.storage_size;
        return asm_operand;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return AMD64_SYMBOL(v->ident, false);
    }

    assert(false && "operand type not expected");
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
//static slice_t *amd64_native_fn_end(closure_t *c, lir_op_t *op) {
//    // 编译时总是使用了一个 temp var 来作为 target, 所以这里进行简单转换即可
//    slice_t *operations = slice_new();
//
//    // compiler 阶段已经附加了 bal end fn 的指令
//    // lower 阶段已经将返回值放在了 rax/xmm0 中
//    // 所以这里什么都不用做
//
//    return operations;
//}

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

    if (need_eliminate_bal_fn_end(c, op)) {
        return operations;
    }

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
        uint64_t size = temp->type.storage_size;

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
        assertf(op->first->assert_type == LIR_OPERAND_REGS, "div op first must reg");
        assertf(op->second->assert_type != LIR_OPERAND_IMM, "div op second cannot imm");

        reg_t *output_reg = op->output->value;
        assertf(output_reg->index == rax->index || output_reg->index == rdx->index || output_reg->index == ah->index,
                "div op output reg must rax/rdx");

        amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);

        if (op->code == LIR_OPCODE_SDIV) {
            slice_push(operations, AMD64_INST("idiv", second));
        } else {
            slice_push(operations, AMD64_INST("div", second));
        }
        return operations;
    }

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    assert(!asm_operand_equal(second, result));
    // 浮点数是 amd64 常见的双操作数指令 mulss rm -> reg
    asm_mov(operations, op, result, first);
    slice_push(operations, AMD64_INST("fdiv", result, second));
    return operations;
}

/**
 * 经过 lower, 这里处理简单乘法 (只需低位结果):
 * 1. 如果是浮点数, 使用 fmul
 * 2. 如果是整数 + IMM, 使用三操作数 IMUL
 * 3. 如果是整数 + var, 使用双操作数 IMUL
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_mul(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    assertf(op->output->assert_type == LIR_OPERAND_REG, "mul op output must reg");

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);
    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // 浮点数乘法
    if (!amd64_is_integer_operand(op->output)) {
        assert(!asm_operand_equal(second, result));
        asm_mov(operations, op, result, first);
        slice_push(operations, AMD64_INST("fmul", result, second));
        return operations;
    }

    // byte 类型: imul 不支持 8 位操作数，使用单操作数 mul
    // lower 阶段已保证 first 在 al 寄存器中
    if (result->size == BYTE) {
        assert(asm_operand_equal(first, result));
        slice_push(operations, AMD64_INST("imul", second));
        return operations;
    }


    // 整数乘法: 三操作数形式 IMUL dst, src, imm
    if (op->second->assert_type == LIR_OPERAND_IMM) {
        slice_push(operations, AMD64_INST("imul", result, first, second));
        return operations;
    }

    // 整数乘法: 双操作数形式 IMUL dst, src (dst = dst * src)
    // lower 阶段已保证 first == output
    assert(asm_operand_equal(first, result));
    slice_push(operations, AMD64_INST("imul", result, second));

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
    if (op->code == LIR_OPCODE_USHR) {
        opcode = "shr";
    } else if (op->code == LIR_OPCODE_SSHR) {
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

    // 优化: add $1, %reg -> inc %reg
    if (op->second->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = op->second->value;
        if (imm->uint_value == 1) {
            slice_push(operations, AMD64_INST("inc", result));
            return operations;
        }
    }

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

    // 优化: sub $1, %reg -> dec %reg
    if (op->second->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *imm = op->second->value;
        if (imm->uint_value == 1) {
            slice_push(operations, AMD64_INST("dec", result));
            return operations;
        }
    }

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

    assert(op->output->assert_type == LIR_OPERAND_REG && op->first->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);
    int64_t output_size = op->output->size;
    int64_t first_size = op->first->size;
    assert(output_size < first_size); // 截断操作，输出尺寸应该小于输入尺寸

    type_kind output_kind;
    if (output_size == WORD) {
        output_kind = TYPE_UINT16;
    } else if (output_size == BYTE) {
        output_kind = TYPE_UINT8;
    } else {
        output_kind = TYPE_UINT32;
    }

    reg_t *first_reg = first->value;
    amd64_asm_operand_t *fit_first = AMD64_REG(reg_select(first_reg->index, output_kind));


    asm_mov(operations, op, output, fit_first);

    return operations;
}


/**
 * 浮点数截断 (double -> float)
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_ftrunc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    // cvtsd2ss xmm_dst, xmm_src (double to float)
    slice_push(operations, AMD64_INST("cvtsd2ss", output, first));

    return operations;
}

/**
 * 浮点数扩展 (float -> double)
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_fext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    // cvtss2sd xmm_dst, xmm_src (float to double)
    slice_push(operations, AMD64_INST("cvtss2sd", output, first));

    return operations;
}

/**
 * 浮点数转有符号整数
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_ftosi(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);


    if (output->size < DWORD) {
        reg_t *output_reg = output->value;
        amd64_asm_operand_t *new_output = AMD64_REG(reg_select(output_reg->index, TYPE_UINT32));
        slice_push(operations, AMD64_INST("movzx", new_output, output));
        output = new_output;
    }

    if (first->size == QWORD) {
        // cvttsd2si reg64, xmm (double to int64)
        slice_push(operations, AMD64_INST("cvttsd2si", output, first));
    } else {
        // cvttss2si reg32, xmm (float to int32)
        slice_push(operations, AMD64_INST("cvttss2si", output, first));
    }

    return operations;
}

/**
 * 浮点数转无符号整数
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_ftoui(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    int first_size = first->size;
    int output_size = output->size;

    reg_t *output_reg = output->value;
    if (output_size < DWORD) {
        amd64_asm_operand_t *new_output = AMD64_REG(reg_select(output_reg->index, TYPE_UINT32));
        slice_push(operations, AMD64_INST("movzx", new_output, output));
        output = new_output;
    } else if (output_size == DWORD) {
        amd64_asm_operand_t *new_output = AMD64_REG(reg_select(output_reg->index, TYPE_UINT64));
        slice_push(operations, AMD64_INST("movzx", new_output, output));
        output = new_output;
    }

    if (first_size == DWORD) {
        slice_push(operations, AMD64_INST("cvttss2si", output, first));
    } else {
        slice_push(operations, AMD64_INST("cvttsd2si", output, first));
    }

    return operations;
}

/**
 * 有符号整数转浮点数
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_sitof(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG);
    assert(op->first->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);

    if (first->size < DWORD) {
        reg_t *first_reg = first->value;
        amd64_asm_operand_t *new_first = AMD64_REG(reg_select(first_reg->index, TYPE_UINT32));

        // 清空寄存器的高位部分
        slice_push(operations, AMD64_INST("movzx", new_first, first));
        first = new_first;
    }

    // 根据输出类型选择指令
    // 先清零目标寄存器，消除 cvtsi2ss/cvtsi2sd 对旧值高位的假依赖
    slice_push(operations, AMD64_INST("xor", output, output));
    if (output->size == DWORD) {
        // cvtsi2ss xmm, reg (int to float)
        slice_push(operations, AMD64_INST("cvtsi2ss", output, first));
    } else {
        // cvtsi2sd xmm, reg (int to double)
        slice_push(operations, AMD64_INST("cvtsi2sd", output, first));
    }

    return operations;
}

/**
 * 无符号整数转浮点数
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_uitof(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);
    amd64_asm_operand_t *output = lir_operand_trans_amd64(c, op, op->output);
    reg_t *first_reg = first->value;
    int first_size = first->size;

    if (first_size < DWORD) {
        amd64_asm_operand_t *new_first = AMD64_REG(reg_select(first_reg->index, TYPE_UINT32));
        slice_push(operations, AMD64_INST("movzx", new_first, first));
        first = new_first;
    } else if (first_size == DWORD) {
        amd64_asm_operand_t *new_first = AMD64_REG(reg_select(first_reg->index, TYPE_UINT64));
        slice_push(operations, AMD64_INST("movzx", new_first, first));
        first = new_first;
    }

    // 先清零目标寄存器，消除 cvtsi2ss/cvtsi2sd 对旧值高位的假依赖
    slice_push(operations, AMD64_INST("xor", output, output));

    if (first_size <= DWORD) {
        // 32位及以下直接转换
        if (output->size == DWORD) {
            slice_push(operations, AMD64_INST("cvtsi2ss", output, first));
        } else {
            slice_push(operations, AMD64_INST("cvtsi2sd", output, first));
        }
    } else {
        // 64位：统一使用右移-转换-乘2
        slice_push(operations, AMD64_INST("shr", first, AMD64_UINT8(1)));
        if (output->size == DWORD) {
            slice_push(operations, AMD64_INST("cvtsi2ss", output, first));
            slice_push(operations, AMD64_INST("add", output, output));
        } else {
            slice_push(operations, AMD64_INST("cvtsi2sd", output, first));
            slice_push(operations, AMD64_INST("add", output, output));
        }
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

    // callee-saved 寄存器占用的栈空间
    int64_t callee_saved_size = c->callee_saved->count * QWORD;
    offset += callee_saved_size;

    // 进行最终的对齐, linux amd64 中栈一般都是是按 16byte 对齐的
    offset = align_up(offset, AMD64_STACK_ALIGN_SIZE);

    slice_push(operations, AMD64_INST("push", AMD64_REG(rbp))); // push 会移动 rsp 至臻，所以不需要再次处理
    slice_push(operations, AMD64_INST("mov", AMD64_REG(rbp), AMD64_REG(rsp))); // 保存栈指针
    if (offset != 0) {
        // offset 范围处理
        slice_push(operations, AMD64_INST("sub", AMD64_REG(rsp), AMD64_UINT32(offset)));
    }

    // 保存 callee-saved 寄存器
    for (int i = 0; i < c->callee_saved->count; ++i) {
        reg_t *reg = c->callee_saved->take[i];
        slice_push(operations, AMD64_INST("push", AMD64_REG(reg)));
    }

    //    c->stack_offset = offset;
    // gc_bits 补 0
    if (c->call_stack_max_offset) {
        uint64_t bits_start = c->stack_offset / POINTER_SIZE;
        uint64_t bits_count = c->call_stack_max_offset / POINTER_SIZE;
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
slice_t *amd64_native_return(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 恢复 callee-saved 寄存器 (逆序)
    for (int i = c->callee_saved->count - 1; i >= 0; --i) {
        reg_t *reg = c->callee_saved->take[i];
        slice_push(operations, AMD64_INST("pop", AMD64_REG(reg)));
    }

    //    slice_push(operations, AMD64_INST("mov", AMD64_REG(rsp), AMD64_REG(rbp)));
    if (c->stack_offset != 0) {
        slice_push(operations, AMD64_INST("add", AMD64_REG(rsp), AMD64_UINT32(c->stack_offset)));
    }
    slice_push(operations, AMD64_INST("pop", AMD64_REG(rbp)));
    slice_push(operations, AMD64_INST("ret"));
    return operations;
}


static slice_t *amd64_native_fn_end(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    if (c->fndef->return_type.kind == TYPE_VOID) {
        operations = amd64_native_return(c, op);
    }

    if (!c->fndef->is_fx) {
        // assist preempt label
        char *preempt_ident = local_sym_with_fn(c, ".preempt");
        slice_push(operations, AMD64_INST("label", AMD64_SYMBOL(preempt_ident, true)));
        slice_push(operations, AMD64_INST("call", AMD64_SYMBOL(ASSIST_PREEMPT_YIELD_IDENT, false)));


        char *safepoint_ident = local_sym_with_fn(c, ".sp.end");
        slice_push(operations, AMD64_INST("jmp", AMD64_SYMBOL(safepoint_ident, true)));
    }


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
 * 通用的条件分支指令处理函数
 * @param c
 * @param op
 * @return
 */
static slice_t *amd64_native_bcc(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);
    assert(op->first->assert_type == LIR_OPERAND_REG || op->second->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    amd64_asm_operand_t *first = lir_operand_trans_amd64(c, op, op->first);

    amd64_asm_operand_t *second = lir_operand_trans_amd64(c, op, op->second);

    amd64_asm_operand_t *result = lir_operand_trans_amd64(c, op, op->output);

    // cmp 指令比较
    slice_push(operations, AMD64_INST("cmp", first, second));

    char *asm_code = NULL;
    if (amd64_is_integer_operand(op->first)) {
        asm_code = asm_bcc_integer_trans[op->code];
    } else {
        asm_code = asm_bcc_float_trans[op->code];
    }
    assert(asm_code);

    // 添加条件跳转指令
    slice_push(operations, AMD64_INST(asm_code, result));

    return operations;
}


static slice_t *amd64_native_safepoint(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    amd64_asm_operand_t *global_safepoint_operand = AMD64_SYMBOL(GLOBAL_SAFEPOINT_IDENT, false);
    global_safepoint_operand->size = QWORD;

    //    slice_push(operations, AMD64_INST("cmp", global_safepoint_operand, AMD64_UINT32(0))); // 5.20s

    slice_push(operations, AMD64_INST("mov", AMD64_REG(r11), global_safepoint_operand)); // 5.0s
    slice_push(operations, AMD64_INST("test", AMD64_REG(r11), AMD64_REG(r11)));

    char *preempt_ident = local_sym_with_fn(c, ".preempt");
    slice_push(operations, AMD64_INST("jne", AMD64_SYMBOL(preempt_ident, true)));

    char *safepoint_ident = local_sym_with_fn(c, ".sp.end");
    slice_push(operations, AMD64_INST("label", AMD64_SYMBOL(safepoint_ident, true)));

    // je 如何跳过 当前指令 和 call rel32 指令
    // je 跳过 call rel32 指令
    //    slice_push(operations, AMD64_INST("je", AMD64_UINT8(5))); // 5字节(call)
    //    slice_push(operations, AMD64_INST("call", AMD64_SYMBOL(ASSIST_PREEMPT_YIELD_IDENT, false)));

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
    [LIR_OPCODE_RETURN] = amd64_native_return,
    [LIR_OPCODE_RET] = amd64_native_nop,
    [LIR_OPCODE_BEE] = amd64_native_bcc,
    [LIR_OPCODE_BNE] = amd64_native_bcc,
    [LIR_OPCODE_BLT] = amd64_native_bcc,
    [LIR_OPCODE_BLE] = amd64_native_bcc,
    [LIR_OPCODE_BGT] = amd64_native_bcc,
    [LIR_OPCODE_BGE] = amd64_native_bcc,
    [LIR_OPCODE_BULT] = amd64_native_bcc,
    [LIR_OPCODE_BULE] = amd64_native_bcc,
    [LIR_OPCODE_BUGT] = amd64_native_bcc,
    [LIR_OPCODE_BUGE] = amd64_native_bcc,
    [LIR_OPCODE_BAL] = amd64_native_bal,

    // 类型扩展
    [LIR_OPCODE_UEXT] = amd64_native_zext,
    [LIR_OPCODE_SEXT] = amd64_native_sext,
    [LIR_OPCODE_TRUNC] = amd64_native_trunc,
    [LIR_OPCODE_FTRUNC] = amd64_native_ftrunc,
    [LIR_OPCODE_FEXT] = amd64_native_fext,
    [LIR_OPCODE_FTOSI] = amd64_native_ftosi,
    [LIR_OPCODE_FTOUI] = amd64_native_ftoui,
    [LIR_OPCODE_SITOF] = amd64_native_sitof,
    [LIR_OPCODE_UITOF] = amd64_native_uitof,

    [LIR_OPCODE_SAFEPOINT] = amd64_native_safepoint,

    // 一元运算符
    [LIR_OPCODE_NEG] = amd64_native_neg,

    // 位运算
    [LIR_OPCODE_XOR] = amd64_native_xor,
    [LIR_OPCODE_NOT] = amd64_native_not,
    [LIR_OPCODE_OR] = amd64_native_or,
    [LIR_OPCODE_AND] = amd64_native_and,
    [LIR_OPCODE_USHR] = amd64_native_shift,
    [LIR_OPCODE_SSHR] = amd64_native_shift,
    [LIR_OPCODE_USHL] = amd64_native_shift,

    // 算数运算
    [LIR_OPCODE_ADD] = amd64_native_add,
    [LIR_OPCODE_SUB] = amd64_native_sub,
    [LIR_OPCODE_UDIV] = amd64_native_div,
    [LIR_OPCODE_SDIV] = amd64_native_div,
    [LIR_OPCODE_MUL] = amd64_native_mul,
    // 逻辑相关运算符
    [LIR_OPCODE_SGT] = amd64_native_scc,
    [LIR_OPCODE_SGE] = amd64_native_scc,
    [LIR_OPCODE_SLT] = amd64_native_scc,
    [LIR_OPCODE_SLE] = amd64_native_scc,
    [LIR_OPCODE_SEE] = amd64_native_scc,
    [LIR_OPCODE_SNE] = amd64_native_scc,

    [LIR_OPCODE_USLT] = amd64_native_scc,
    [LIR_OPCODE_USLE] = amd64_native_scc,
    [LIR_OPCODE_USGT] = amd64_native_scc,
    [LIR_OPCODE_USGE] = amd64_native_scc,

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
