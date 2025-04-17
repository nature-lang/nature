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

/**
 * 处理大数值的移动操作
 * @param operations 操作序列
 * @param result 目标寄存器操作数
 * @param value 需要移动的值
 */
static void arm64_mov_imm(lir_op_t *op, slice_t *operations, arm64_asm_operand_t *result, int64_t value) {
    if (value >= -0XFFFF && value <= 0xFFFF) {
        slice_push(operations, ARM64_INST(R_MOV, result, ARM64_IMM(value)));
        return;
    }

    // 超过 16位的数处理, result 可能是 32位的 w 寄存器，统一转换成 64位的 x 寄存器处理
    assert(result->type == ARM64_ASM_OPERAND_REG);
    result = ARM64_REG(reg_select(result->reg.index, TYPE_INT64));

    // 大数或负数需要使用 MOVZ, MOVK 组合
    uint64_t uvalue = value;
    // 处理低 0 - 16 位
    slice_push(operations, ARM64_INST(R_MOV, result, ARM64_IMM(uvalue & 0xFFFF)));

    if ((uvalue >> 16) & 0xFFFF) {
        // 16 - 32
        slice_push(operations, ARM64_INST(R_MOVK, result, ARM64_IMM((uvalue >> 16) & 0xFFFF), ARM64_SHIFT(16)));
    }
    if ((uvalue >> 32) & 0xFFFF) {
        // 32 - 48
        slice_push(operations, ARM64_INST(R_MOVK, result, ARM64_IMM((uvalue >> 32) & 0xFFFF), ARM64_SHIFT(32)));
    }
    if ((uvalue >> 48) & 0xFFFF) {
        // 48 - 64
        slice_push(operations, ARM64_INST(R_MOVK, result, ARM64_IMM((uvalue >> 48) & 0xFFFF), ARM64_SHIFT(48)));
    }
}

static bool arm64_is_integer_operand(lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return reg->flag & FLAG(LIR_FLAG_ALLOC_INT);
    }

    // bool 也被失败成 int 类型
    return !is_float(operand_type_kind(operand));
}

static void arm64_gen_cmp(lir_op_t *op, slice_t *operations, arm64_asm_operand_t *source, arm64_asm_operand_t *dest,
                          bool is_int) {
    if (is_int) {
        arm64_asm_raw_opcode_t opcode;
        if (source->type == ARM64_ASM_OPERAND_IMMEDIATE && source->immediate < 0) {
            source->immediate = -source->immediate;
            opcode = R_CMN;
        } else {
            opcode = R_CMP;
        }

        if (source->type == ARM64_ASM_OPERAND_IMMEDIATE && source->immediate > 4095) {
            // 使用 x16 进行中转，然后再进行比较
            arm64_asm_operand_t *free_reg;
            if (source->size <= 4) {
                free_reg = ARM64_REG(w16);
            } else {
                free_reg = ARM64_REG(x16);
            }

            // mov
            arm64_mov_imm(op, operations, free_reg, source->immediate);
            source = free_reg;
        }


        slice_push(operations, ARM64_INST(opcode, dest, source));
    } else {
        assert(dest->type == ARM64_ASM_OPERAND_FREG);
        slice_push(operations, ARM64_INST(R_FCMP, dest, source));
    }
}

static arm64_asm_operand_t *convert_operand_to_free_reg(lir_op_t *op, slice_t *operations, arm64_asm_operand_t *source,
                                                        bool is_int,
                                                        uint8_t size) {
    arm64_asm_operand_t *free_reg;
    if (is_int) {
        if (size <= 4) {
            free_reg = ARM64_REG(w16);
        } else {
            free_reg = ARM64_REG(x16);
        }

        // 基于 x16/w16/d16/s16 临时寄存器做一个 ldr mem -> reg, return reg
        if (size == BYTE) {
            slice_push(operations, ARM64_INST(R_LDRB, free_reg, source));
        } else if (size == WORD) {
            slice_push(operations, ARM64_INST(R_LDRH, free_reg, source));
        } else {
            slice_push(operations, ARM64_INST(R_LDR, free_reg, source));
        }
    } else {
        if (size == 4) {
            free_reg = ARM64_REG(s16);
        } else {
            free_reg = ARM64_REG(d16);
        }

        slice_push(operations, ARM64_INST(R_LDR, free_reg, source));
    }

    free_reg->size = size;
    return free_reg;
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
    arm64_asm_operand_t *result = NULL;
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        result = ARM64_REG(reg);
        result->size = reg->size;
        return result;
    }

    uint32_t mem_size = 0;

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        mem_size = stack->size;
        // ARM64 使用 [fp, #offset] 形式访问栈
        result = ARM64_INDIRECT(fp, stack->slot, 0, mem_size); // 0 表示非 pre/post index
    } else if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *indirect = operand->value;
        lir_operand_t *base = indirect->base;
        mem_size = type_kind_sizeof(indirect->type.kind);

        // 处理栈基址
        if (base->assert_type == LIR_OPERAND_STACK) {
            assert(op->code == LIR_OPCODE_LEA);
            lir_stack_t *stack = base->value;
            stack->slot += indirect->offset;
            result = ARM64_INDIRECT(fp, stack->slot, 0, stack->size);
        }

        assert(base->assert_type == LIR_OPERAND_REG);

        reg_t *reg = base->value;
        result = ARM64_INDIRECT(reg, indirect->offset, 0, mem_size);
    }

    // indirect 有符号偏移范围 [-256, 255], 如果超出范围，需要借助临时寄存器 x16 进行转换(此处转换不需要考虑 float 类型)
    if (result && result->type == ARM64_ASM_OPERAND_INDIRECT && result->indirect.offset < -256) {
        int64_t offset = result->indirect.offset * -1;
        slice_push(operations, ARM64_INST(R_SUB, ARM64_REG(x16), ARM64_REG(result->indirect.reg), ARM64_IMM(offset)));

        // 基于 x16 重新生成 indirect
        result->indirect.reg = x16;
        result->indirect.offset = 0;
    }

    // 只有 MOVE(str/ldr) 指令可以操作内存地址
    if (operand->pos == LIR_FLAG_SECOND && lir_is_mem(operand) && op->code != LIR_OPCODE_MOVE) {
        return convert_operand_to_free_reg(op, operations, result, arm64_is_integer_operand(operand), mem_size);
    }

    if (result) {
        return result;
    }

    if (operand->assert_type == LIR_OPERAND_IMM) {
        lir_imm_t *v = operand->value;
        assert(v->kind != TYPE_RAW_STRING && v->kind != TYPE_FLOAT);

        // 根据不同类型返回立即数
        if (v->kind == TYPE_INT || v->kind == TYPE_UINT ||
            v->kind == TYPE_INT64 || v->kind == TYPE_UINT64 ||
            v->kind == TYPE_ANYPTR) {
            result = ARM64_IMM(v->uint_value);
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8 ||
                   v->kind == TYPE_INT16 || v->kind == TYPE_UINT16 ||
                   v->kind == TYPE_INT32 || v->kind == TYPE_UINT32) {
            result = ARM64_IMM(v->uint_value);
        } else if (v->kind == TYPE_FLOAT32 || v->kind == TYPE_FLOAT ||
                   v->kind == TYPE_FLOAT64) {
            result = ARM64_IMM(*(uint64_t *) &v->f64_value);
        } else if (v->kind == TYPE_BOOL) {
            result = ARM64_IMM(v->bool_value);
        }

        assertf(result, "unsupported immediate type");

        result->size = type_kind_sizeof(v->kind);

        // 三元运算符的 second 如果是 imm，imm 有数字大小限制不能超过 4095, 如果超过 4095 则需要使用 x16 寄存器进行中转
        if (operand->pos == LIR_FLAG_SECOND && lir_op_ternary(op) && result->immediate > 4095) {
            // 使用 x16 进行中转，然后再进行比较
            arm64_asm_operand_t *free_reg;
            if (result->size <= 4) {
                free_reg = ARM64_REG(w16);
            } else {
                free_reg = ARM64_REG(x16);
            }

            arm64_mov_imm(op, operations, free_reg, result->immediate);
            result = free_reg;
        }


        return result;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // sym value 特殊处理，拆解成 adrp + add 定位具体的符号位置

        lir_symbol_var_t *v = operand->value;
        result = ARM64_SYM(v->ident, false, 0, 0);
        result->size = type_kind_sizeof(v->kind);
        return result;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return ARM64_SYM(v->ident, v->is_local, 0, 0);
    }

    assert(false && "unsupported operand type");
    return NULL;
}

static slice_t *arm64_native_trunc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    int64_t size = op->output->size;
    assert(size > 0);

    // 先将源操作数移动到目标寄存器
    slice_push(operations, ARM64_INST(R_MOV, result, source));

    // 根据目标寄存器的大小应用适当的掩码进行截断
    if (size == BYTE) {
        // 截断为 8 位 (1 字节)
        slice_push(operations, ARM64_INST(R_AND, result, result, ARM64_IMM(0xFF)));
    } else if (size == WORD) {
        // 截断为 16 位 (2 字节)
        slice_push(operations, ARM64_INST(R_AND, result, result, ARM64_IMM(0xFFFF)));
    } else if (size == DWORD) {
        // 截断为 32 位 (4 字节)
        // 对于 W 寄存器，高 32 位自动清零，不需要额外的 AND 操作
        if (result->type == ARM64_ASM_OPERAND_REG && result->reg.size == QWORD) {
            // 如果是 X 寄存器，需要显式截断高 32 位
            slice_push(operations, ARM64_INST(R_AND, result, result, ARM64_IMM(0xFFFFFFFF)));
        }
    }

    return operations;
}

static slice_t *arm64_native_sext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    if (source->size == DWORD && result->size == QWORD) {
        slice_push(operations, ARM64_INST(R_SXTW, result, source));
    } else {
        slice_push(operations, ARM64_INST(R_MOV, result, source));
    }

    return operations;
}

static slice_t *arm64_native_zext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    if (source->size == DWORD && result->size == QWORD) {
        slice_push(operations, ARM64_INST(R_UXTW, result, source));
    } else {
        slice_push(operations, ARM64_INST(R_MOV, result, source));
    }

    return operations;
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

    // 必须存在一个寄存器操作数, reg 可能是浮点型
    // mov REG(first) -> REG(output)
    // mov REG -> MEM
    // mov IMM -> REG
    // mov MEM(stack/indirect_addr/symbol_var) -> REG

    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 检查源操作数类型
    if (op->first->assert_type == LIR_OPERAND_REG) {
        if (arm64_is_integer_operand(op->first)) {
            if (op->output->assert_type == LIR_OPERAND_REG) {
                // REG -> REG
                slice_push(operations, ARM64_INST(R_MOV, result, source));
            } else {
                // REG -> MEM
                if (result->size == BYTE) {
                    slice_push(operations, ARM64_INST(R_STRB, source, result));
                } else if (result->size == WORD) {
                    slice_push(operations, ARM64_INST(R_STRH, source, result));
                } else {
                    slice_push(operations, ARM64_INST(R_STR, source, result));
                }
            }
        } else {
            if (op->output->assert_type == LIR_OPERAND_REG) {
                // FREG -> FREG
                slice_push(operations, ARM64_INST(R_FMOV, result, source));
            } else {
                // FREG -> MEM
                slice_push(operations, ARM64_INST(R_STR, source, result));
            }
        }
    } else if (op->first->assert_type == LIR_OPERAND_IMM) {
        // move imm -> reg
        lir_imm_t *imm = op->first->value;
        assert(is_number(imm->kind) || imm->kind == TYPE_BOOL);
        int64_t value = imm->int_value;

        arm64_mov_imm(op, operations, result, value);
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR ||
               op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // 内存到寄存器的移动
        // MEM -> REG
        if (source->size == BYTE) {
            // 检查是否为有符号类型
            if (is_signed(operand_type_kind(op->first))) {
                slice_push(operations, ARM64_INST(R_LDRSB, result, source)); // 有符号加载
            } else {
                slice_push(operations, ARM64_INST(R_LDRB, result, source)); // 无符号加载
            }
        } else if (source->size == WORD) {
            if (is_signed(operand_type_kind(op->first))) {
                slice_push(operations, ARM64_INST(R_LDRSH, result, source)); // 有符号加载
            } else {
                slice_push(operations, ARM64_INST(R_LDRH, result, source)); // 无符号加载
            }
        } else {
            slice_push(operations, ARM64_INST(R_LDR, result, source));
        }
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
    slice_push(operations, ARM64_INST(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(value->size)));

    // 将值存储到栈上
    assert(value->size >= 4);
    slice_push(operations, ARM64_INST(R_STR, value, ARM64_INDIRECT(sp, 0, 0, value->size)));
    return operations;
}

static slice_t *arm64_native_bal(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 获取跳转目标标签
    arm64_asm_operand_t *target = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 B 指令进行无条件跳转
    slice_push(operations, ARM64_INST(R_B, target));

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
        if (result->type == ARM64_ASM_OPERAND_REG) {
            slice_push(operations, ARM64_INST(R_EOR, result, result, result));
        } else {
            if (result->size == QWORD) {
                slice_push(operations, ARM64_INST(R_FMOV, result, ARM64_REG(xzr)));
            } else {
                slice_push(operations, ARM64_INST(R_FMOV, result, ARM64_REG(wzr)));
            }
        }
        return operations;
    }

    if (result->type == ARM64_ASM_OPERAND_INDIRECT) {
        uint8_t size = result->size;
        assertf(size <= QWORD, "only can clv size <= %d, actual=%d", QWORD, size);

        if (size == BYTE) {
            slice_push(operations, ARM64_INST(R_STRB, ARM64_REG(wzr), result));
        } else if (size == WORD) {
            slice_push(operations, ARM64_INST(R_STRH, ARM64_REG(wzr), result));
        } else if (size == DWORD) {
            slice_push(operations, ARM64_INST(R_STR, ARM64_REG(wzr), result));
        } else {
            slice_push(operations, ARM64_INST(R_STR, ARM64_REG(xzr), result));
        }

        return operations;
    }

    assert(false);
    return operations;
}


static slice_t *arm64_native_clr(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);
    if (result->type == ARM64_ASM_OPERAND_REG) {
        slice_push(operations, ARM64_INST(R_EOR, result, result, result));
    } else {
        if (result->size == QWORD) {
            slice_push(operations, ARM64_INST(R_FMOV, result, ARM64_REG(xzr)));
        } else {
            slice_push(operations, ARM64_INST(R_FMOV, result, ARM64_REG(wzr)));
        }
    }
    return operations;
}


static slice_t *arm64_native_div(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *dividend = lir_operand_trans_arm64(c, op, op->first, operations); // 被除数
    arm64_asm_operand_t *divisor = lir_operand_trans_arm64(c, op, op->second, operations); // 除数
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations); // 结果

    // 判断是否为整数除法
    if (arm64_is_integer_operand(op->first)) {
        // 使用 SDIV 指令进行有符号整数除法
        slice_push(operations, ARM64_INST(R_SDIV, result, dividend, divisor));
    } else {
        // 使用 FDIV 指令进行浮点数除法
        slice_push(operations, ARM64_INST(R_FDIV, result, dividend, divisor));
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
    if (arm64_is_integer_operand(op->output)) {
        // 使用 MUL 指令进行整数乘法
        slice_push(operations, ARM64_INST(R_MUL, result, first, second));
    } else {
        // 使用 FMUL 指令进行浮点数乘法
        slice_push(operations, ARM64_INST(R_FMUL, result, first, second));
    }

    return operations;
}

static slice_t *arm64_native_rem(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->second->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    // 参数转换
    arm64_asm_operand_t *dividend = lir_operand_trans_arm64(c, op, op->first, operations); // 被除数
    arm64_asm_operand_t *divisor = lir_operand_trans_arm64(c, op, op->second, operations); // 除数
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations); // 结果

    // 使用临时寄存器 x16 存储商
    arm64_asm_operand_t *temp_reg = ARM64_REG(x16);
    if (dividend->size <= 4) {
        temp_reg = ARM64_REG(w16);
    }
    temp_reg->size = dividend->size;

    // 1. 先做除法得到商: temp_reg = dividend / divisor
    slice_push(operations, ARM64_INST(R_SDIV, temp_reg, dividend, divisor));

    // 2. 用 MSUB 计算余数: result = dividend - (temp_reg * divisor)
    slice_push(operations, ARM64_INST(R_MSUB, result, temp_reg, divisor, dividend));

    return operations;
}

static slice_t *arm64_native_neg(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *source = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 判断是否为整数取负
    if (arm64_is_integer_operand(op->first)) {
        arm64_asm_operand_t *zero_reg = ARM64_REG(xzr);
        if (source->size <= 4) {
            zero_reg = ARM64_REG(wzr);
        }

        // 使用 SUB 指令实现整数取负：result = 0 - source
        slice_push(operations, ARM64_INST(R_SUB, result, zero_reg, source));
    } else {
        // 使用 FNEG 指令进行浮点数取负
        slice_push(operations, ARM64_INST(R_FNEG, result, source));
    }

    return operations;
}


static slice_t *arm64_native_xor(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    assert(op->first->assert_type == LIR_OPERAND_REG);
    assert(op->second->assert_type == LIR_OPERAND_REG);
    assert(op->output->assert_type == LIR_OPERAND_REG);

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 EOR 指令实现异或操作
    slice_push(operations, ARM64_INST(R_EOR, result, first, second));

    return operations;
}

static slice_t *arm64_native_or(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    assert(first->type == ARM64_ASM_OPERAND_REG);
    assert(second->type == ARM64_ASM_OPERAND_REG);
    assert(result->type == ARM64_ASM_OPERAND_REG);

    // 使用 ORR 指令实现或操作
    slice_push(operations, ARM64_INST(R_ORR, result, first, second));

    return operations;
}

static slice_t *arm64_native_and(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 参数转换
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 AND 指令实现与操作
    slice_push(operations, ARM64_INST(R_AND, result, first, second));

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
            slice_push(operations, ARM64_INST(R_LSL, result, source, shift_amount));
            break;
        case LIR_OPCODE_SHR:
            slice_push(operations, ARM64_INST(R_LSR, result, source, shift_amount));
            break;
        case LIR_OPCODE_SAR:
            slice_push(operations, ARM64_INST(R_ASR, result, source, shift_amount));
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
    slice_push(operations, ARM64_INST(R_MVN, result, source));

    return operations;
}

static slice_t *arm64_native_add(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *left = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *right = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 ADD 指令进行加法
    if (arm64_is_integer_operand(op->output)) {
        slice_push(operations, ARM64_INST(R_ADD, result, left, right));
    } else {
        slice_push(operations, ARM64_INST(R_FADD, result, left, right));
    }

    return operations;
}

static slice_t *arm64_native_sub(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    arm64_asm_operand_t *left = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *right = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 使用 SUB 指令进行减法
    if (arm64_is_integer_operand(op->output)) {
        slice_push(operations, ARM64_INST(R_SUB, result, left, right));
    } else {
        slice_push(operations, ARM64_INST(R_FSUB, result, left, right));
    }

    return operations;
}


static slice_t *arm64_native_call(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 获取函数地址操作数
    arm64_asm_operand_t *func_address = lir_operand_trans_arm64(c, op, op->first, operations);
    assert(func_address->type == ARM64_ASM_OPERAND_REG || func_address->type == ARM64_ASM_OPERAND_SYMBOL);

    // 使用 BL 指令进行函数调用, 需要根据 fn 的类型选择合适的调用指令
    if (func_address->type == ARM64_ASM_OPERAND_REG) {
        slice_push(operations, ARM64_INST(R_BLR, func_address));
    } else {
        slice_push(operations, ARM64_INST(R_BL, func_address));
    }


    return operations;
}


static slice_t *arm64_native_scc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG);

    // 假设 op->first 和 op->second 是需要比较的操作数
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    // 进行比较
    arm64_gen_cmp(op, operations, second, first, arm64_is_integer_operand(op->first));


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
        case LIR_OPCODE_USLT:
            cond = ARM64_COND_LO;
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
    slice_push(operations, ARM64_INST(R_CSET, result, ARM64_COND(cond)));

    return operations;
}


static slice_t *arm64_native_label(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    lir_symbol_label_t *label_operand = op->output->value;

    // 虚拟指令，接下来的汇编器实现会解析该指令
    slice_push(operations, ARM64_INST(R_LABEL, ARM64_SYM(label_operand->ident, label_operand->is_local, 0, 0)));

    return operations;
}

static slice_t *arm64_native_fn_begin(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    int64_t offset = c->stack_offset;
    offset += c->call_stack_max_offset;

    // 进行最终的对齐, linux arm64 中栈一般都是是按 16byte 对齐的
    offset = align_up(offset, ARM64_STACK_ALIGN_SIZE);

    // 保存当前的帧指针和链接寄存器, ARM64_INDIRECT(sp, -16, 1) 最后一个参数是 1 表示 pre index, 会先进行 sub sp, sp, #16 减少 sp 的值
    // 等价于 push sp 两次
    slice_push(operations, ARM64_INST(R_STP, ARM64_REG(fp), ARM64_REG(lr), ARM64_INDIRECT(sp, -16, 1, OWORD)));
    // 更新帧指针
    slice_push(operations, ARM64_INST(R_MOV, ARM64_REG(fp), ARM64_REG(sp)));

    // 如果需要，调整栈指针以分配栈空间
    if (offset != 0) {
        slice_push(operations, ARM64_INST(R_SUB, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(offset)));
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

    // 恢复栈帧
    if (c->stack_offset != 0) {
        slice_push(operations, ARM64_INST(R_ADD, ARM64_REG(sp), ARM64_REG(sp), ARM64_IMM(c->stack_offset)));
    }

    // 恢复 fp(x29) 和 lr(x30), ARM64_INDIRECT(sp, 16, 2) 2 表示 post-index 模式
    slice_push(operations, ARM64_INST(R_LDP, ARM64_REG(fp), ARM64_REG(lr), ARM64_INDIRECT(sp, 16, 2, OWORD)));
    // [sp], #16
    // 返回
    slice_push(operations, ARM64_INST(R_RET));
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
        slice_push(operations, ARM64_INST(R_ADRP, result, first));

        arm64_asm_operand_t *lo12_symbol_operand = ARM64_SYM(first->symbol.name, first->symbol.is_local,
                                                             first->symbol.offset, ASM_ARM64_RELOC_LO12);
        slice_push(operations, ARM64_INST(R_ADD, result, result, lo12_symbol_operand));
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        reg_t *base = NULL;
        int64_t offset = 0;
        if (op->first->assert_type == LIR_OPERAND_STACK) {
            lir_stack_t *stack = op->first->value;
            base = x29;
            offset = stack->slot;
        } else {
            lir_indirect_addr_t *mem = op->first->value;
            assert(mem->base->assert_type == LIR_OPERAND_REG);
            base = mem->base->value;
            offset = mem->offset;
        }

        slice_push(operations, ARM64_INST(R_ADD, result, ARM64_REG(base), ARM64_IMM(offset)));
    }
    return operations;
}


static slice_t *arm64_native_beq(closure_t *c, lir_op_t *op) {
    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);
    // arm64 lower 阶段必须确保所有的指令的第一个操作数是寄存器
    assert(op->first->assert_type == LIR_OPERAND_REG);

    slice_t *operations = slice_new();

    // 比较 first 是否等于 second，如果相等就跳转到 result label
    arm64_asm_operand_t *first = lir_operand_trans_arm64(c, op, op->first, operations);
    arm64_asm_operand_t *second = lir_operand_trans_arm64(c, op, op->second, operations);
    assert(second->type == ARM64_ASM_OPERAND_REG || second->type == ARM64_ASM_OPERAND_IMMEDIATE);
    arm64_asm_operand_t *result = lir_operand_trans_arm64(c, op, op->output, operations);

    arm64_gen_cmp(op, operations, second, first, arm64_is_integer_operand(op->first));

    // 如果相等则跳转(beq)到标签
    slice_push(operations, ARM64_INST(R_BEQ, result));

    return operations;
}

static slice_t *arm64_native_safepoint(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output && op->output->assert_type == LIR_OPERAND_REG);
    reg_t *x0_reg = op->output->value;
    assert(x0_reg->index == x0->index);

    arm64_asm_operand_t *x0_operand = ARM64_REG(x0_reg);
    x0_operand->size = x0_reg->size;


    // 加载 tls_yield_safepoint 的指针
    if (BUILD_OS == OS_DARWIN) {
        // 处理 TLS 变量
        // 1. 使用 ADRP 加载 TLS 变量的页地址
        arm64_asm_operand_t *tlv_page = ARM64_SYM(TLS_YIELD_SAFEPOINT_IDENT, false, 0,
                                                  ASM_ARM64_RELOC_TLVP_LOAD_PAGE21);
        slice_push(operations, ARM64_INST(R_ADRP, x0_operand, tlv_page));

        // 2. 加载 TLV getter 函数的地址
        assert(op->output->assert_type == LIR_OPERAND_REG);
        reg_t *result_reg = op->output->value;
        assert(result_reg->index == x0->index);
        slice_push(operations, ARM64_INST(R_LDR, x0_operand, ARM64_INDIRECT_SYM(result_reg, TLS_YIELD_SAFEPOINT_IDENT,
                                                                                ASM_ARM64_RELOC_TLVP_LOAD_PAGEOFF12)));

        // 3.?
        slice_push(operations, ARM64_INST(R_LDR, ARM64_REG(x16), ARM64_INDIRECT(result_reg, 0, 0, QWORD)));

        // 3. 调用 TLV get 函数获取实际的 TLS 变量地址
        slice_push(operations, ARM64_INST(R_BLR, ARM64_REG(x16)));
    } else {
        // 1. 使用 MRS 指令读取 TPIDR_EL0 寄存器（TLS 基址）到结果寄存器
        slice_push(operations, ARM64_INST(R_MRS, x0_operand, ARM64_IMM(TPIDR_EL0)));

        // 2.1 添加高12位偏移量
        arm64_asm_operand_t *tls_hi12_operand = ARM64_SYM(TLS_YIELD_SAFEPOINT_IDENT, false, 0,
                                                          ASM_ARM64_RELOC_TLSLE_ADD_TPREL_HI12);
        slice_push(operations, ARM64_INST(R_ADD, x0_operand, x0_operand, tls_hi12_operand));

        // 2.2 添加低12位偏移量
        arm64_asm_operand_t *tls_lo12_operand = ARM64_SYM(TLS_YIELD_SAFEPOINT_IDENT, false, 0,
                                                          ASM_ARM64_RELOC_TLSLE_ADD_TPREL_LO12_NC);
        slice_push(operations, ARM64_INST(R_ADD, x0_operand, x0_operand, tls_lo12_operand));
    }

    // ldr x0, [x0]
    //    arm64_asm_operand_t *w0_operand = ARM64_REG(w0);
    //    w0_operand->size = w0_reg->size;
    slice_push(operations, ARM64_INST(R_LDR, x0_operand, ARM64_INDIRECT(x0_reg, 0, 0, POINTER_SIZE)));

    // cmp x0,#0x0
    slice_push(operations, ARM64_INST(R_CMP, x0_operand, ARM64_IMM(0)));

    // b.eq 跳过 bl 指令
    slice_push(operations, ARM64_INST(R_BEQ, ARM64_IMM(8)));

    slice_push(operations, ARM64_INST(R_BL, ARM64_SYM(ASSIST_PREEMPT_YIELD_IDENT, false, 0, 0)));

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
        [LIR_OPCODE_SAR] = arm64_native_shift,
        [LIR_OPCODE_SHL] = arm64_native_shift,

        // 算数运算
        [LIR_OPCODE_ADD] = arm64_native_add,
        [LIR_OPCODE_SUB] = arm64_native_sub,
        [LIR_OPCODE_DIV] = arm64_native_div,
        [LIR_OPCODE_MUL] = arm64_native_mul,
        [LIR_OPCODE_REM] = arm64_native_rem,

        // 逻辑相关运算符
        [LIR_OPCODE_SGT] = arm64_native_scc,
        [LIR_OPCODE_SGE] = arm64_native_scc,
        [LIR_OPCODE_SLT] = arm64_native_scc,
        [LIR_OPCODE_USLT] = arm64_native_scc,
        [LIR_OPCODE_SLE] = arm64_native_scc,
        [LIR_OPCODE_SEE] = arm64_native_scc,
        [LIR_OPCODE_SNE] = arm64_native_scc,

        [LIR_OPCODE_MOVE] = arm64_native_mov,
        [LIR_OPCODE_LEA] = arm64_native_lea,
        [LIR_OPCODE_ZEXT] = arm64_native_zext,
        [LIR_OPCODE_SEXT] = arm64_native_sext,
        [LIR_OPCODE_TRUNC] = arm64_native_trunc,
        [LIR_OPCODE_FN_BEGIN] = arm64_native_fn_begin,
        [LIR_OPCODE_FN_END] = arm64_native_fn_end,

        [LIR_OPCODE_SAFEPOINT] = arm64_native_safepoint,
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
