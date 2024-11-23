#include "arm64.h"
#include "src/debug/debug.h"
#include "src/register/arch/arm64.h"
#include <assert.h>

static slice_t *arm64_native_block(closure_t *c, basic_block_t *block);

typedef slice_t *(*arm64_native_fn)(closure_t *c, lir_op_t *op);

/**
 * 分发入口, 基于 op->code 做选择(包含 label code)
 * @param c
 * @param op
 * @return
 */
static slice_t *arm64_native_op(closure_t *c, lir_op_t *op);


static bool arm64_is_integer_operand(lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return reg->flag & FLAG(LIR_FLAG_ALLOC_INT);
    }

    return is_integer(operand_type_kind(operand));
}


/**
 * lir_operand 中不能直接转换为 asm_operand 的参数
 * type_string/lir_operand_memory
 * @param operand
 * @param asm_operand
 * @return
 */
static arm64_asm_operand_t *
lir_operand_trans_arm64(closure_t *c, lir_op_t *op, lir_operand_t *operand, slice_t *operations) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return ARM64_REG(reg);
    }

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        // ARM64 使用 [fp, #offset] 形式访问栈
        return ARM64_INDIRECT(fp, stack->slot, 0);// 0 表示非 pre/post index
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *v = operand->value;
        assert(v->kind != TYPE_RAW_STRING && v->kind != TYPE_FLOAT);

        // 根据不同类型返回立即数
        if (v->kind == TYPE_INT || v->kind == TYPE_UINT ||
            v->kind == TYPE_INT64 || v->kind == TYPE_UINT64 ||
            v->kind == TYPE_VOID_PTR) {
            return ARM64_IMM(v->uint_value);
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8 ||
                   v->kind == TYPE_INT16 || v->kind == TYPE_UINT16 ||
                   v->kind == TYPE_INT32 || v->kind == TYPE_UINT32) {
            return ARM64_IMM(v->uint_value);
        } else if (v->kind == TYPE_FLOAT32 || v->kind == TYPE_FLOAT ||
                   v->kind == TYPE_FLOAT64) {
            return ARM64_IMM(*(uint64_t *) &v->f64_value);
        } else if (v->kind == TYPE_BOOL) {
            return ARM64_IMM(v->bool_value);
        }
        assert(false && "unsupported immediate type");
    }

    if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *indirect = operand->value;
        lir_operand_t *base = indirect->base;

        // 处理栈基址
        if (base->assert_type == LIR_OPERAND_STACK) {
            assert(op->code == LIR_OPCODE_LEA);
            lir_stack_t *stack = base->value;
            stack->slot += indirect->offset;
            return ARM64_INDIRECT(fp, stack->slot, 0);
        }

        assert(base->assert_type == LIR_OPERAND_REG);
        reg_t *reg = base->value;

        if (indirect->offset == 0) {
            return ARM64_INDIRECT(reg, 0, 0);
        } else {
            return ARM64_INDIRECT(reg, indirect->offset, 0);
        }
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // sym value 特殊处理，拆解成 adrp + add 定位具体的符号位置

        lir_symbol_var_t *v = operand->value;
        return ARM64_SYM(v->ident, false, 0, 0);
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return ARM64_SYM(v->ident, v->is_local, 0, 0);
    }

    assert(false && "unsupported operand type");
    return NULL;
}


/**
 * op->result must reg
 * @param c
 * @param op
 * @param count
 * @return
 */
static slice_t *arm64_native_mov(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 检查源操作数类型
    if (op->first->assert_type == LIR_OPERAND_REG) {
        if (arm64_is_integer_operand(op->first)) {
            slice_push(operations, ARM64_ASM(R_MOV, result, source));
        } else {
            slice_push(operations, ARM64_ASM(R_FMOV, result, source));
        }
    } else if (op->first->assert_type == LIR_OPERAND_IMM) {
        // 立即数到寄存器的移动
        lir_imm_t *imm = op->first->value;
        assert(is_number(imm->kind) || imm->kind == TYPE_BOOL);

        int64_t value = imm->int_value;

        if (value >= 0 && value <= 0xFFFF) {
            // 小的正整数可以直接用 MOV
            slice_push(operations, ARM64_ASM(R_MOV, result, ARM64_IMM(value)));
        } else {
            // 大数或负数需要使用 MOV, MOVK 组合
            slice_push(operations, ARM64_ASM(R_MOV, result, ARM64_IMM(value & 0xFFFF)));

            if (value > 0xFFFF) {
                slice_push(operations, ARM64_ASM(R_MOVK, result, ARM64_IMM((value >> 16) & 0xFFFF), ARM64_IMM(16)));
            }
            if (value > 0xFFFFFFFF) {
                slice_push(operations, ARM64_ASM(R_MOVK, result, ARM64_IMM((value >> 32) & 0xFFFF), ARM64_IMM(32)));
                slice_push(operations, ARM64_ASM(R_MOVK, result, ARM64_IMM((value >> 48) & 0xFFFF), ARM64_IMM(48)));
            }
        }
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        // 内存到寄存器的移动
        slice_push(operations, ARM64_ASM(R_LDR, result, source));
    } else {
        assert(false && "Unsupported operand type for MOV");
    }

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
static slice_t *arm64_native_return(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    return operations;
}

static slice_t *arm64_native_skip(closure_t *c, lir_op_t *op) {
    return slice_new();
}

static slice_t *arm64_native_nop(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    return operations;
}

static slice_t *arm64_native_push(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *value = lir_operand_trans_arm64(c, op, op->first, operations);

    // 先减少 SP
    slice_push(operations, ARM64_ASM(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(value->size)));

    // 将值存储到栈上
    slice_push(operations, ARM64_ASM(R_STR, value, ARM64_INDIRECT(sp, 0, 0)));
    return operations;
}

static slice_t *arm64_native_bal(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 获取跳转目标标签
    arm64_asm_operand_t *target = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 B 指令进行无条件跳转
    slice_push(operations, ARM64_ASM(R_B, target));

    return operations;
}

/**
 * -0x18(%rbp) = indirect addr
 * @param op
 * @param count
 * @return
 */
static slice_t *arm64_native_clv(closure_t *c, lir_op_t *op) {
    lir_operand_t *output = op->output;
    assert(output->assert_type == LIR_OPERAND_REG ||
           output->assert_type == LIR_OPERAND_STACK ||
           output->assert_type == LIR_OPERAND_INDIRECT_ADDR);
    assert(output);

    slice_t *operations = slice_new();
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 对于寄存器类型，使用 EOR 指令将寄存器清零
    if (output->assert_type == LIR_OPERAND_REG) {
        slice_push(operations, ARM64_ASM(R_EOR, result, result, result));
        return operations;
    }

    // 对于栈变量
    if (output->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = output->value;
        assertf(stack->size <= QWORD, "only can clv size <= %d, actual=%d", QWORD, stack->size);

        // ARM64 使用 MOV 指令加载立即数 0
        slice_push(operations, ARM64_ASM(R_MOV, result, ARM64_IMM(0)));
        return operations;
    }

    // 对于间接地址
    if (output->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indexed_addr_t *temp = output->value;
        uint16_t size = type_sizeof(temp->type);

        assertf(size <= QWORD, "only can clv size <= %d, actual=%d", QWORD, size);

        // ARM64 使用 MOV 指令加载立即数 0
        slice_push(operations, ARM64_ASM(R_MOV, result, ARM64_IMM(0)));
        return operations;
    }

    assert(false);
    return operations;
}


static slice_t *arm64_native_clr(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    slice_push(operations, ARM64_ASM(R_EOR, result, result, result));
    return operations;
}


static slice_t *arm64_native_div(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *dividend = lir_operand_trans_arm64(c, op, op->first, operations);// 被除数
    arm64_asm_operand_t *divisor = lir_operand_trans_arm64(c, op, op->second, operations);// 除数
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations); // 结果

    // 判断是否为整数除法
    if (arm64_is_integer_operand(op->first)) {
        // 使用 SDIV 指令进行有符号整数除法
        slice_push(operations, ARM64_ASM(R_SDIV, result, dividend, divisor));
    } else {
        // 使用 FDIV 指令进行浮点数除法
        slice_push(operations, ARM64_ASM(R_FDIV, result, dividend, divisor));
    }

    return operations;
}


static slice_t *arm64_native_mul(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 判断是否为整数乘法
    if (arm64_is_integer_operand(op->first)) {
        // 使用 MUL 指令进行整数乘法
        slice_push(operations, ARM64_ASM(R_MUL, result, first, second));
    } else {
        // 使用 FMUL 指令进行浮点数乘法
        slice_push(operations, ARM64_ASM(R_FMUL, result, first, second));
    }

    return operations;
}

static slice_t *arm64_native_neg(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 判断是否为整数取负
    if (arm64_is_integer_operand(op->first)) {
        // 使用 SUB 指令实现整数取负：result = 0 - source
        slice_push(operations, ARM64_ASM(R_NEG, result, source));
    } else {
        // 使用 FNEG 指令进行浮点数取负
        slice_push(operations, ARM64_ASM(R_FNEG, result, source));
    }

    return operations;
}


static slice_t *arm64_native_xor(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 EOR 指令实现异或操作
    slice_push(operations, ARM64_ASM(R_EOR, result, first, second));

    return operations;
}

static slice_t *arm64_native_or(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 ORR 指令实现或操作
    slice_push(operations, ARM64_ASM(R_ORR, result, first, second));

    return operations;
}

static slice_t *arm64_native_and(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 AND 指令实现与操作
    slice_push(operations, ARM64_ASM(R_AND, result, first, second));

    return operations;
}

static slice_t *arm64_native_shift(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *shift_amount = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 根据 op->code 选择合适的移位指令
    switch (op->code) {
        case LIR_OPCODE_SHL:
            slice_push(operations, ARM64_ASM(R_LSL, result, source, shift_amount));
            break;
        case LIR_OPCODE_SHR:
            slice_push(operations, ARM64_ASM(R_LSR, result, source, shift_amount));
            break;
        default:
            assert(false && "Unsupported shift operation");
    }

    return operations;
}

static slice_t *arm64_native_not(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 EOR 指令实现 NOT 操作
    slice_push(operations, ARM64_ASM(R_EOR, result, source, ARM64_IMM(~0)));

    return operations;
}


static slice_t *arm64_native_add(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *left = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *right = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 ADD 指令进行加法
    slice_push(operations, ARM64_ASM(R_ADD, result, left, right));

    return operations;
}

static slice_t *arm64_native_sub(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *left = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *right = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 SUB 指令进行减法
    slice_push(operations, ARM64_ASM(R_SUB, result, left, right));

    return operations;
}


static slice_t *arm64_native_call(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 获取函数地址操作数
    arm64_asm_operand_t *func_address = lir_operand_trans_arm64(c, op, op->first, operations);

    // 使用 BL 指令进行函数调用
    slice_push(operations, ARM64_ASM(R_BL, func_address));


    return operations;
}


static slice_t *arm64_native_scc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 假设 op->first 和 op->second 是需要比较的操作数
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 进行比较
    slice_push(operations, ARM64_ASM(R_CMP, first, second));

    // 根据 op->code 设置条件码
    arm64_asm_cond_type cond;
    switch (op->code) {
        case LIR_OPCODE_SGT:
            cond = ARM64_COND_GT;
            break;
        case LIR_OPCODE_SGE:
            cond = ARM64_COND_GE;
            break;
        case LIR_OPCODE_SLT:
            cond = ARM64_COND_LT;
            break;
        case LIR_OPCODE_SLE:
            cond = ARM64_COND_LE;
            break;
        case LIR_OPCODE_SEE:
            cond = ARM64_COND_EQ;
            break;
        case LIR_OPCODE_SNE:
            cond = ARM64_COND_NE;
            break;
        default:
            assert(false && "Unsupported SCC operation");
    }

    // 设置条件码
    slice_push(operations, ARM64_ASM(R_CSET, result, ARM64_COND(cond)));

    return operations;
}


static slice_t *arm64_native_label(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    lir_symbol_label_t *label_operand = op->output->value;

    // 虚拟指令，接下来的汇编器实现会解析该指令
    slice_push(operations, ARM64_ASM(R_LABEL, ARM64_SYM(label_operand->ident, label_operand->is_local, 0, 0)));

    return operations;
}

static slice_t *arm64_native_fn_begin(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    int64_t offset = c->stack_offset;
    offset += c->call_stack_max_offset;

    // 进行最终的对齐, linux arm64 中栈一般都是是按 16byte 对齐的
    offset = align_up(offset, ARM64_STACK_ALIGN_SIZE);

    // 保存当前的帧指针和链接寄存器
    slice_push(operations, ARM64_ASM(R_STP, ARM64_REG(fp), ARM64_REG(lr), ARM64_INDIRECT(sp, 16, 0)));
    // 更新帧指针
    slice_push(operations, ARM64_ASM(R_MOV, ARM64_REG(fp), ARM64_REG(sp)));

    // 如果需要，调整栈指针以分配栈空间
    if (offset != 0) {
        slice_push(operations, ARM64_ASM(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(offset)));
    }


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


slice_t *arm64_native_fn_end(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    // 恢复 fp(x29) 和 lr(x30)
    slice_push(operations, ARM64_ASM(R_LDP, ARM64_REG(fp), ARM64_REG(lr), ARM64_INDIRECT(sp, 16, 0)));// [sp, #16]

    // 恢复栈指针，相当于 AMD64 的 mov rsp, rbp 和 pop rbp 的组合效果
    slice_push(operations, ARM64_ASM(R_ADD, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(32)));// add sp, sp, #32

    // 返回
    slice_push(operations, ARM64_ASM(R_RET));
    return operations;
}

static slice_t *arm64_native_lea(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    if (op->first->assert_type == LIR_OPERAND_SYMBOL_LABEL || op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // 对于符号地址，使用 ADRP 指令加载页地址
        // ADRP x0, symbol        ; 加载符号的页地址
        // ADD x0, x0, :lo12:symbol  ; 加载低12位偏移
        slice_push(operations, ARM64_ASM(R_ADRP, result, first));

        arm64_asm_operand_t *lo12_symbol_operand = ARM64_SYM(first->symbol.name, first->symbol.is_local,
                                                             first->symbol.offset, ARM64_RELOC_LO12);
        slice_push(operations, ARM64_ASM(R_ADD, result, result, lo12_symbol_operand));
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        // 对于栈地址或间接地址，直接使用 ADD 指令
        slice_push(operations, ARM64_ASM(R_ADD, result, first, ARM64_IMM(0)));
    }
    return operations;
}


static slice_t *arm64_native_beq(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);
    // arm64 lower 阶段必须确保所有的指令的第一个操作数是寄存器
    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->second->assert_type == LIR_OPERAND_REG || op->second->assert_type == LIR_OPERAND_IMM);

    slice_t *operations = slice_new();

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    arm64_asm_operand_t *first_reg = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);

    arm64_asm_operand_t *result_label = lir_operand_trans_arm64(c, op, op->output, operations);

    // cmp 指令比较
    slice_push(operations, ARM64_ASM(R_CMP, first_reg, second));

    // 如果相等则跳转(beq)到标签
    slice_push(operations, ARM64_ASM(R_BEQ, result_label));

    return operations;
}


arm64_native_fn arm64_native_table[] = {
        [LIR_OPCODE_CLR] = arm64_native_clr,
        [LIR_OPCODE_CLV] = arm64_native_clv,
        [LIR_OPCODE_NOP] = arm64_native_nop,
        [LIR_OPCODE_CALL] = arm64_native_call,
        [LIR_OPCODE_RT_CALL] = arm64_native_call,
        [LIR_OPCODE_LABEL] = arm64_native_label,
        [LIR_OPCODE_PUSH] = arm64_native_push,
        [LIR_OPCODE_RETURN] = arm64_native_nop,
        [LIR_OPCODE_BREAK] = arm64_native_nop,
        [LIR_OPCODE_BEQ] = arm64_native_beq,
        [LIR_OPCODE_BAL] = arm64_native_bal,

        // 一元运算符
        [LIR_OPCODE_NEG] = arm64_native_neg,

        // 位运算
        [LIR_OPCODE_XOR] = arm64_native_xor,
        [LIR_OPCODE_NOT] = arm64_native_not,
        [LIR_OPCODE_OR] = arm64_native_or,
        [LIR_OPCODE_AND] = arm64_native_and,
        [LIR_OPCODE_SHR] = arm64_native_shift,
        [LIR_OPCODE_SHL] = arm64_native_shift,

        // 算数运算
        [LIR_OPCODE_ADD] = arm64_native_add,
        [LIR_OPCODE_SUB] = arm64_native_sub,
        [LIR_OPCODE_DIV] = arm64_native_div,
        [LIR_OPCODE_MUL] = arm64_native_mul,
        // 逻辑相关运算符
        [LIR_OPCODE_SGT] = arm64_native_scc,
        [LIR_OPCODE_SGE] = arm64_native_scc,
        [LIR_OPCODE_SLT] = arm64_native_scc,
        [LIR_OPCODE_SLE] = arm64_native_scc,
        [LIR_OPCODE_SEE] = arm64_native_scc,
        [LIR_OPCODE_SNE] = arm64_native_scc,

        [LIR_OPCODE_MOVE] = arm64_native_mov,
        [LIR_OPCODE_LEA] = arm64_native_lea,
        [LIR_OPCODE_FN_BEGIN] = arm64_native_fn_begin,
        [LIR_OPCODE_FN_END] = arm64_native_fn_end,

        // 伪指令，直接忽略即可
        //        [LIR_OPCODE_ENV_CLOSURE] = arm64_native_skip,
};


slice_t *arm64_native_op(closure_t *c, lir_op_t *op) {
    arm64_native_fn fn = arm64_native_table[op->code];
    assert(fn && "arm64 native op not support");
    return fn(c, op);
}

static slice_t *arm64_native_block(closure_t *c, basic_block_t *block) {
    slice_t *operations = slice_new();
    linked_node *current = linked_first(block->operations);
    while (current->value != NULL) {
        lir_op_t *op = current->value;
        slice_concat(operations, arm64_native_op(c, op));
        current = current->succ;
    }
    return operations;
}

void arm64_native(closure_t *c) {
    assert(c->module);

    // 遍历 block
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        slice_concat(c->asm_operations, arm64_native_block(c, block));
    }
}
