#include "riscv64.h"
#include "src/debug/debug.h"
#include "src/register/arch/riscv64.h"
#include <assert.h>

static slice_t *riscv64_native_block(closure_t *c, basic_block_t *block);

typedef slice_t *(*riscv64_native_fn)(closure_t *c, lir_op_t *op);

static riscv64_asm_operand_t *riscv64_convert_operand_to_free_reg(lir_op_t *op, slice_t *operations, riscv64_asm_operand_t *source,
                                                                  bool is_int, uint8_t size);

/**
 * 分发入口, 基于 op->code 做选择(包含 label code)
 * @param c
 * @param op
 * @return
 */
static slice_t *riscv64_native_op(closure_t *c, lir_op_t *op);

/**
 * 处理大数值的移动操作
 * @param operations 操作序列
 * @param result 目标寄存器操作数
 * @param value 需要移动的值
 */
static void riscv64_mov_imm(lir_op_t *op, slice_t *operations, riscv64_asm_operand_t *result, int64_t value) {
    // li 伪指令可以处理超过 12 位(-2048 ~ 2047)的 imm 操作数到 reg 中
    assert(result->type == RISCV64_ASM_OPERAND_REG);
    slice_push(operations, RISCV64_INST(RV_LI, result, RO_IMM(value)));
}

/**
 * 判断操作数是否为整数类型
 */
static bool riscv64_is_integer_operand(lir_operand_t *operand) {
    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        return reg->flag & FLAG(LIR_FLAG_ALLOC_INT);
    }

    // bool 也被当成 int 类型
    return !is_float(operand_type_kind(operand));
}

/**
 * lir_operand 转换为 RISCV64汇编操作数
 */
static riscv64_asm_operand_t *
lir_operand_trans_riscv64(closure_t *c, lir_op_t *op, lir_operand_t *operand, slice_t *operations) {
    riscv64_asm_operand_t *result = NULL;

    if (operand->assert_type == LIR_OPERAND_REG) {
        reg_t *reg = operand->value;
        result = RO_REG(reg);
        result->size = reg->size;
        return result;
    }

    uint32_t mem_size = 0;

    if (operand->assert_type == LIR_OPERAND_STACK) {
        lir_stack_t *stack = operand->value;
        mem_size = stack->size;
        // RISC-V 使用 [fp+offset] 形式访问栈
        result = RO_INDIRECT(R_FP, stack->slot, mem_size);
    } else if (operand->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        lir_indirect_addr_t *indirect = operand->value;
        lir_operand_t *base = indirect->base;
        mem_size = type_kind_sizeof(indirect->type.kind);

        // 处理栈基址
        if (base->assert_type == LIR_OPERAND_STACK) {
            assert(op->code == LIR_OPCODE_LEA);
            lir_stack_t *stack = base->value;
            stack->slot += indirect->offset;
            result = RO_INDIRECT(R_FP, stack->slot, mem_size);
        } else {
            assert(base->assert_type == LIR_OPERAND_REG);
            reg_t *reg = base->value;
            result = RO_INDIRECT(reg, indirect->offset, mem_size);
        }
    }

    // RISC-V只支持12位有符号立即数作为偏移量
    // 如果偏移量超出范围，需要使用临时寄存器
    if (result && result->type == RISCV64_ASM_OPERAND_INDIRECT &&
        (result->indirect.offset < -2048 || result->indirect.offset > 2047)) {
        // TODO issue handle
        assert("data movement range exceeds +-2048");
        //         使用t6(x31)作为临时寄存器
        //        riscv64_asm_operand_t *temp_reg = RO_REG(T6);

        // 计算基址和偏移量的和
        //        slice_push(operations, RISCV64_INST(RV_MV, temp_reg, RO_REG(result->indirect.reg)));
        //        riscv64_mov_imm(op, operations, RO_REG(T6), result->indirect.offset);
        //        slice_push(operations, RISCV64_INST(RV_ADD, temp_reg, temp_reg, RO_REG(T6)));

        // 使用临时寄存器和零偏移量创建新的间接寻址操作数
        //        result = RO_INDIRECT(T6, 0, mem_size);
    }

    // 如果是非 MOVE 指令的第二个操作数且是内存操作数，需要先加载到寄存器
    if (operand->pos == LIR_FLAG_SECOND && lir_is_mem(operand) && op->code != LIR_OPCODE_MOVE) {
        return riscv64_convert_operand_to_free_reg(op, operations, result, riscv64_is_integer_operand(operand), mem_size);
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
            result = RO_IMM(v->uint_value);
        } else if (v->kind == TYPE_INT8 || v->kind == TYPE_UINT8 ||
                   v->kind == TYPE_INT16 || v->kind == TYPE_UINT16 ||
                   v->kind == TYPE_INT32 || v->kind == TYPE_UINT32) {
            result = RO_IMM(v->uint_value);
        } else if (v->kind == TYPE_FLOAT32 || v->kind == TYPE_FLOAT ||
                   v->kind == TYPE_FLOAT64) {
            // 对于浮点数，先加载到临时寄存器
            result = RO_IMM(*(uint64_t *) &v->f64_value);
        } else if (v->kind == TYPE_BOOL) {
            result = RO_IMM(v->bool_value);
        }

        assertf(result, "unsupported immediate type");

        // 如果是三元运算符的第二个操作数且是立即数 (addi t0, 3000, t1)，RISC-V 的立即数范围有限制
        // 如果超出范围，则需要先加载到临时寄存器
        if (operand->pos == LIR_FLAG_SECOND && lir_op_ternary(op) &&
            (result->immediate < -2048 || result->immediate > 2047)) {
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, result->immediate);
            result = temp_reg;
        }

        return result;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        lir_symbol_var_t *v = operand->value;
        result = RO_SYM(v->ident, false, 0, 0);
        return result;
    }

    if (operand->assert_type == LIR_OPERAND_SYMBOL_LABEL) {
        lir_symbol_label_t *v = operand->value;
        return RO_SYM(v->ident, v->is_local, 0, 0);
    }

    assert(false && "unsupported operand type");
    return NULL;
}

/**
 * 将内存操作数转换为寄存器操作数
 */
static riscv64_asm_operand_t *riscv64_convert_operand_to_free_reg(lir_op_t *op, slice_t *operations, riscv64_asm_operand_t *source,
                                                                  bool is_int, uint8_t size) {
    riscv64_asm_operand_t *free_reg;

    if (is_int) {
        free_reg = RO_REG(T6);

        // 根据大小选择适当的加载指令
        if (size == BYTE) {
            slice_push(operations, RISCV64_INST(RV_LB, free_reg, source));
        } else if (size == WORD) {
            slice_push(operations, RISCV64_INST(RV_LH, free_reg, source));
        } else if (size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_LW, free_reg, source));
        } else {
            slice_push(operations, RISCV64_INST(RV_LD, free_reg, source));
        }
    } else {
        // 使用ft11(f31)作为临时浮点寄存器
        free_reg = RO_REG(FT11);

        // 根据大小选择适当的浮点加载指令
        if (size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FLW, free_reg, source));
        } else {
            slice_push(operations, RISCV64_INST(RV_FLD, free_reg, source));
        }
    }

    return free_reg;
}

/**
 * 实现基本的MOVE指令
 */
static slice_t *riscv64_native_mov(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_REG || op->first->assert_type == LIR_OPERAND_REG);

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 检查源操作数类型
    if (op->first->assert_type == LIR_OPERAND_REG) {
        if (riscv64_is_integer_operand(op->first)) {
            if (op->output->assert_type == LIR_OPERAND_REG) {
                // REG -> REG
                slice_push(operations, RISCV64_INST(RV_MV, result, source));
            } else {
                // REG -> MEM
                if (result->size == BYTE) {
                    slice_push(operations, RISCV64_INST(RV_SB, source, result));
                } else if (result->size == WORD) {
                    slice_push(operations, RISCV64_INST(RV_SH, source, result));
                } else if (result->size == DWORD) {
                    slice_push(operations, RISCV64_INST(RV_SW, source, result));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SD, source, result));
                }
            }
        } else {
            // 浮点寄存器操作
            if (op->output->assert_type == LIR_OPERAND_REG) {
                // FREG -> FREG
                if (source->size == DWORD) {
                    slice_push(operations, RISCV64_INST(RV_FMV_S, result, source));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FMV_D, result, source));
                }
            } else {
                // FREG -> MEM
                if (source->size == DWORD) {
                    slice_push(operations, RISCV64_INST(RV_FSW, source, result));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FSD, source, result));
                }
            }
        }
    } else if (op->first->assert_type == LIR_OPERAND_IMM) {
        // move imm -> reg
        lir_imm_t *imm = op->first->value;
        assert(is_number(imm->kind) || imm->kind == TYPE_BOOL);
        int64_t value = imm->int_value;

        // 使用RISC-V的LI指令加载立即数
        riscv64_mov_imm(op, operations, result, value);
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR ||
               op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // 内存到寄存器的移动
        // MEM -> REG
        if (riscv64_is_integer_operand(op->output)) {
            if (source->size == BYTE) {
                // 检查是否为有符号类型
                if (is_signed(operand_type_kind(op->first))) {
                    slice_push(operations, RISCV64_INST(RV_LB, result, source)); // RISC-V的LB已经是符号扩展
                } else {
                    slice_push(operations, RISCV64_INST(RV_LBU, result, source)); // 无符号加载
                }
            } else if (source->size == WORD) {
                if (is_signed(operand_type_kind(op->first))) {
                    slice_push(operations, RISCV64_INST(RV_LH, result, source)); // 有符号加载
                } else {
                    slice_push(operations, RISCV64_INST(RV_LHU, result, source)); // 无符号加载
                }
            } else if (source->size == DWORD) {
                if (is_signed(operand_type_kind(op->first))) {
                    slice_push(operations, RISCV64_INST(RV_LW, result, source)); // 有符号加载
                } else {
                    slice_push(operations, RISCV64_INST(RV_LWU, result, source)); // 无符号加载
                }
            } else {
                slice_push(operations, RISCV64_INST(RV_LD, result, source));
            }
        } else {
            // 浮点寄存器加载
            if (source->size == DWORD) {
                slice_push(operations, RISCV64_INST(RV_FLW, result, source));
            } else {
                slice_push(operations, RISCV64_INST(RV_FLD, result, source));
            }
        }
    } else {
        assert(false && "Unsupported operand type for MOV");
    }

    return operations;
}

/**
 * 实现ADD指令
 */
static slice_t *riscv64_native_add(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *left = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *right = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 使用ADD指令进行加法
    if (riscv64_is_integer_operand(op->output)) {
        if (right->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
            slice_push(operations, RISCV64_INST(RV_ADDI, result, left, right));
        } else {
            // 寄存器间加法
            slice_push(operations, RISCV64_INST(RV_ADD, result, left, right));
        }
    } else {
        // 浮点加法
        if (result->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FADD_S, result, left, right));
        } else {
            slice_push(operations, RISCV64_INST(RV_FADD_D, result, left, right));
        }
    }

    return operations;
}

/**
 * 实现SUB指令
 */
static slice_t *riscv64_native_sub(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *left = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *right = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 使用SUB指令进行减法
    if (riscv64_is_integer_operand(op->output)) {
        if (right->type == RISCV64_ASM_OPERAND_IMMEDIATE &&
            right->immediate >= -2048 && right->immediate <= 2047) {
            // 立即数在范围内，可以使用ADDI和负数
            slice_push(operations, RISCV64_INST(RV_ADDI, result, left, RO_IMM(-right->immediate)));
        } else if (right->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
            // 立即数超出范围，需要先加载到寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, right->immediate);
            slice_push(operations, RISCV64_INST(RV_SUB, result, left, temp_reg));
        } else {
            // 寄存器间减法
            slice_push(operations, RISCV64_INST(RV_SUB, result, left, right));
        }
    } else {
        // 浮点减法
        if (result->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FSUB_S, result, left, right));
        } else {
            slice_push(operations, RISCV64_INST(RV_FSUB_D, result, left, right));
        }
    }

    return operations;
}

/**
 * 实现乘法指令
 */
static slice_t *riscv64_native_mul(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *left = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *right = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数乘法
    if (right->type == RISCV64_ASM_OPERAND_IMMEDIATE && riscv64_is_integer_operand(op->output)) {
        // RISC-V没有直接的立即数乘法指令，需要先加载到寄存器
        riscv64_asm_operand_t *temp_reg = RO_REG(T6);
        riscv64_mov_imm(op, operations, temp_reg, right->immediate);
        right = temp_reg;
    }

    // 执行乘法操作
    if (riscv64_is_integer_operand(op->output)) {
        slice_push(operations, RISCV64_INST(RV_MUL, result, left, right));
    } else {
        // 浮点乘法
        if (result->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FMUL_S, result, left, right));
        } else {
            slice_push(operations, RISCV64_INST(RV_FMUL_D, result, left, right));
        }
    }

    return operations;
}

/**
 * 实现除法指令
 */
static slice_t *riscv64_native_div(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *dividend = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *divisor = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数除法
    if (divisor->type == RISCV64_ASM_OPERAND_IMMEDIATE && riscv64_is_integer_operand(op->output)) {
        // RISC-V没有直接的立即数除法指令，需要先加载到寄存器
        riscv64_asm_operand_t *temp_reg = RO_REG(T6);
        riscv64_mov_imm(op, operations, temp_reg, divisor->immediate);
        divisor = temp_reg;
    }

    // 执行除法操作
    if (riscv64_is_integer_operand(op->output)) {
        if (op->code == LIR_OPCODE_SDIV) {
            // 有符号除法
            slice_push(operations, RISCV64_INST(RV_DIV, result, dividend, divisor));
        } else {
            // 无符号除法
            slice_push(operations, RISCV64_INST(RV_DIVU, result, dividend, divisor));
        }
    } else {
        // 浮点除法
        if (result->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FDIV_S, result, dividend, divisor));
        } else {
            slice_push(operations, RISCV64_INST(RV_FDIV_D, result, dividend, divisor));
        }
    }

    return operations;
}

/**
 * 实现余数指令
 */
static slice_t *riscv64_native_rem(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *dividend = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *divisor = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数
    if (divisor->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        riscv64_asm_operand_t *temp_reg = RO_REG(T6);
        riscv64_mov_imm(op, operations, temp_reg, divisor->immediate);
        divisor = temp_reg;
    }

    // 执行余数操作
    if (op->code == LIR_OPCODE_SREM) {
        // 有符号余数
        slice_push(operations, RISCV64_INST(RV_REM, result, dividend, divisor));
    } else {
        // 无符号余数
        slice_push(operations, RISCV64_INST(RV_REMU, result, dividend, divisor));
    }

    return operations;
}

/**
 * 实现位运算指令：AND
 */
static slice_t *riscv64_native_and(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *first = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *second = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数
    if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        if (second->immediate >= -2048 && second->immediate <= 2047) {
            // 使用ANDI指令
            slice_push(operations, RISCV64_INST(RV_ANDI, result, first, second));
        } else {
            // 立即数超出范围，需要先加载到寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, second->immediate);
            slice_push(operations, RISCV64_INST(RV_AND, result, first, temp_reg));
        }
    } else {
        // 寄存器间位与操作
        slice_push(operations, RISCV64_INST(RV_AND, result, first, second));
    }

    return operations;
}

/**
 * 实现位运算指令：OR
 */
static slice_t *riscv64_native_or(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *first = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *second = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数
    if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        if (second->immediate >= -2048 && second->immediate <= 2047) {
            // 使用ORI指令
            slice_push(operations, RISCV64_INST(RV_ORI, result, first, second));
        } else {
            // 立即数超出范围，需要先加载到寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, second->immediate);
            slice_push(operations, RISCV64_INST(RV_OR, result, first, temp_reg));
        }
    } else {
        // 寄存器间位或操作
        slice_push(operations, RISCV64_INST(RV_OR, result, first, second));
    }

    return operations;
}

/**
 * 实现位运算指令：XOR
 */
static slice_t *riscv64_native_xor(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *first = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *second = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 处理立即数
    if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        if (second->immediate >= -2048 && second->immediate <= 2047) {
            // 使用XORI指令
            slice_push(operations, RISCV64_INST(RV_XORI, result, first, second));
        } else {
            // 立即数超出范围，需要先加载到寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, second->immediate);
            slice_push(operations, RISCV64_INST(RV_XOR, result, first, temp_reg));
        }
    } else {
        // 寄存器间异或操作
        slice_push(operations, RISCV64_INST(RV_XOR, result, first, second));
    }

    return operations;
}

/**
 * 实现取反指令(NOT)
 */
static slice_t *riscv64_native_not(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // RISC-V使用XORI指令和-1实现NOT操作
    slice_push(operations, RISCV64_INST(RV_XORI, result, source, RO_IMM(-1)));

    return operations;
}

/**
 * 实现取负指令(NEG)
 */
static slice_t *riscv64_native_neg(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    if (riscv64_is_integer_operand(op->first)) {
        // RISC-V使用从0减去源值来实现NEG
        slice_push(operations, RISCV64_INST(RV_SUB, result, RO_REG(ZEROREG), source));
    } else {
        // 浮点取负
        if (source->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FNEG_S, result, source));
        } else {
            slice_push(operations, RISCV64_INST(RV_FNEG_D, result, source));
        }
    }

    return operations;
}

/**
 * 实现移位指令
 */
static slice_t *riscv64_native_shift(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *shift_amount = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 选择适当的移位指令
    riscv64_asm_raw_opcode_t opcode;
    riscv64_asm_raw_opcode_t imm_opcode;

    switch (op->code) {
        case LIR_OPCODE_USHL:
            opcode = RV_SLL;
            imm_opcode = RV_SLLI;
            break;
        case LIR_OPCODE_USHR:
            opcode = RV_SRL;
            imm_opcode = RV_SRLI;
            break;
        case LIR_OPCODE_SSHR:
            opcode = RV_SRA;
            imm_opcode = RV_SRAI;
            break;
        default:
            assert(false && "Unsupported shift operation");
    }

    // 处理立即数移位
    if (shift_amount->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        // 立即数移位
        slice_push(operations, RISCV64_INST(imm_opcode, result, source, shift_amount));
    } else {
        // 寄存器移位
        slice_push(operations, RISCV64_INST(opcode, result, source, shift_amount));
    }

    return operations;
}

/**
 * 实现比较指令(SCC)
 */
static slice_t *riscv64_native_scc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *first = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *second = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 判断是否为整数操作数
    bool is_integer = riscv64_is_integer_operand(op->first);

    if (is_integer) {
        // 整数比较逻辑（处理立即数）
        if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
            if (second->immediate < -2048 || second->immediate > 2047) {
                riscv64_asm_operand_t *temp_reg = RO_REG(T6);
                riscv64_mov_imm(op, operations, temp_reg, second->immediate);
                second = temp_reg;
            }
        }

        // 整数比较指令
        switch (op->code) {
            case LIR_OPCODE_SGT:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_SLTI, result, first, RO_IMM(second->immediate + 1)));
                    slice_push(operations, RISCV64_INST(RV_XORI, result, result, RO_IMM(1)));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SLT, result, second, first));
                }
                break;

            case LIR_OPCODE_SGE:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_SLTI, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SLT, result, first, second));
                }
                slice_push(operations, RISCV64_INST(RV_XORI, result, result, RO_IMM(1)));
                break;

            case LIR_OPCODE_SLT:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_SLTI, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SLT, result, first, second));
                }
                break;

            case LIR_OPCODE_USLT:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_SLTIU, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SLTU, result, first, second));
                }
                break;

            case LIR_OPCODE_SLE:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_SLTI, result, first, RO_IMM(second->immediate + 1)));
                } else {
                    slice_push(operations, RISCV64_INST(RV_SLT, result, second, first));
                    slice_push(operations, RISCV64_INST(RV_XORI, result, result, RO_IMM(1)));
                }
                break;

            case LIR_OPCODE_SEE:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_XORI, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_XOR, result, first, second));
                }
                slice_push(operations, RISCV64_INST(RV_SLTIU, result, result, RO_IMM(1)));
                break;

            case LIR_OPCODE_SNE:
                if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
                    slice_push(operations, RISCV64_INST(RV_XORI, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_XOR, result, first, second));
                }
                slice_push(operations, RISCV64_INST(RV_SLTU, result, RO_REG(ZEROREG), result));
                break;

            default:
                assert(false && "Unsupported integer SCC operation");
        }
    } else {
        // 浮点数比较逻辑
        // 确定使用单精度还是双精度指令
        bool is_double = (first->size == 8); // 8字节为双精度，4字节为单精度

        switch (op->code) {
            case LIR_OPCODE_SGT: // first > second 等价于 second < first
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FLT_D, result, second, first));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FLT_S, result, second, first));
                }
                break;

            case LIR_OPCODE_SGE: // first >= second 等价于 !(first < second)
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FLT_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FLT_S, result, first, second));
                }
                slice_push(operations, RISCV64_INST(RV_XORI, result, result, RO_IMM(1)));
                break;

            case LIR_OPCODE_SLT: // first < second
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FLT_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FLT_S, result, first, second));
                }
                break;

            case LIR_OPCODE_SLE: // first <= second
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FLE_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FLE_S, result, first, second));
                }
                break;

            case LIR_OPCODE_SEE: // first == second
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FEQ_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FEQ_S, result, first, second));
                }
                break;

            case LIR_OPCODE_SNE: // first != second 等价于 !(first == second)
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FEQ_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FEQ_S, result, first, second));
                }
                slice_push(operations, RISCV64_INST(RV_XORI, result, result, RO_IMM(1)));
                break;

            case LIR_OPCODE_USLT: // 浮点数没有无符号比较，使用有符号比较
                if (is_double) {
                    slice_push(operations, RISCV64_INST(RV_FLT_D, result, first, second));
                } else {
                    slice_push(operations, RISCV64_INST(RV_FLT_S, result, first, second));
                }
                break;

            default:
                assert(false && "Unsupported float SCC operation");
        }
    }

    return operations;
}

/**
 * 实现条件跳转指令(BEQ)
 */
static slice_t *riscv64_native_beq(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    assert(op->output->assert_type == LIR_OPERAND_SYMBOL_LABEL);
    assert(op->first->assert_type == LIR_OPERAND_REG);

    riscv64_asm_operand_t *first = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *second = lir_operand_trans_riscv64(c, op, op->second, operations);
    riscv64_asm_operand_t *target = lir_operand_trans_riscv64(c, op, op->output, operations);

    assert(target->type == RISCV64_ASM_OPERAND_SYMBOL);
    target->symbol.reloc_type = ASM_RISCV64_RELOC_BRANCH;

    // RISC-V 的 BEQ 指令需要两个寄存器进行比较
    if (second->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        if (second->immediate == 0) {
            // 如果比较的是0，可以直接使用BEQ
            slice_push(operations, RISCV64_INST(RV_BEQ, first, RO_REG(ZEROREG), target));
        } else {
            // 否则需要先加载立即数到临时寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, second->immediate);
            slice_push(operations, RISCV64_INST(RV_BEQ, first, temp_reg, target));
        }
    } else {
        // 寄存器间比较
        slice_push(operations, RISCV64_INST(RV_BEQ, first, second, target));
    }

    return operations;
}

/**
 * 实现无条件跳转指令(BAL)
 */
static slice_t *riscv64_native_bal(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *target = lir_operand_trans_riscv64(c, op, op->output, operations);

    assert(target->type == RISCV64_ASM_OPERAND_SYMBOL);
    target->symbol.reloc_type = ASM_RISCV64_RELOC_JAL;

    // 用 RISC-V 的 J 指令进行无条件跳转, J 是 jal 的伪指令，所以直接使用 ASM_RISCV64_RELOC_JAL 重定位即可
    slice_push(operations, RISCV64_INST(RV_J, target));

    return operations;
}

/**
 * 实现函数调用指令(CALL)
 */
static slice_t *riscv64_native_call(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *fn_addr = lir_operand_trans_riscv64(c, op, op->first, operations);

    // 函数调用
    if (fn_addr->type == RISCV64_ASM_OPERAND_SYMBOL) {
        fn_addr->symbol.reloc_type = ASM_RISCV64_RELOC_CALL;
        // 对于全局函数符号，使用 CALL 伪指令
        slice_push(operations, RISCV64_INST(RV_CALL, fn_addr));
    } else {
        assert(fn_addr->type == RISCV64_ASM_OPERAND_REG);
        // 对于寄存器中的函数地址，使用 JALR 指令
        slice_push(operations, RISCV64_INST(RV_JALR, fn_addr));
    }

    return operations;
}

/**
 * 实现标签指令(LABEL)
 */
static slice_t *riscv64_native_label(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    lir_symbol_label_t *label_operand = op->output->value;

    // 生成标签伪指令
    slice_push(operations, RISCV64_INST(RV_LABEL, RO_SYM(label_operand->ident, label_operand->is_local, 0, 0)));

    return operations;
}

/**
 * 实现清零指令(CLR)
 */
static slice_t *riscv64_native_clr(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    if (result->type == RISCV64_ASM_OPERAND_REG) {
        // 对于整数寄存器，使用MV指令和零寄存器
        slice_push(operations, RISCV64_INST(RV_MV, result, RO_REG(ZEROREG)));
    } else {
        // 对于浮点寄存器，需要先清零整数寄存器，再转换
        riscv64_asm_operand_t *temp_reg = RO_REG(T6);
        slice_push(operations, RISCV64_INST(RV_MV, temp_reg, RO_REG(ZEROREG)));

        if (result->size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_FCVT_S_W, result, temp_reg));
        } else {
            slice_push(operations, RISCV64_INST(RV_FCVT_D_W, result, temp_reg));
        }
    }

    return operations;
}

/**
 * 实现内存清零指令(CLV)
 */
static slice_t *riscv64_native_clv(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    lir_operand_t *output = op->output;
    assert(output->assert_type == LIR_OPERAND_REG ||
           output->assert_type == LIR_OPERAND_STACK ||
           output->assert_type == LIR_OPERAND_INDIRECT_ADDR);

    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, output, operations);

    if (output->assert_type == LIR_OPERAND_REG) {
        // 清零寄存器
        return riscv64_native_clr(c, op);
    }

    if (result->type == RISCV64_ASM_OPERAND_INDIRECT) {
        uint8_t size = result->size;

        // 使用零寄存器存储0到内存
        if (size == BYTE) {
            slice_push(operations, RISCV64_INST(RV_SB, RO_REG(ZEROREG), result));
        } else if (size == WORD) {
            slice_push(operations, RISCV64_INST(RV_SH, RO_REG(ZEROREG), result));
        } else if (size == DWORD) {
            slice_push(operations, RISCV64_INST(RV_SW, RO_REG(ZEROREG), result));
        } else {
            slice_push(operations, RISCV64_INST(RV_SD, RO_REG(ZEROREG), result));
        }

        return operations;
    }

    assert(false);
    return operations;
}

/**
 * 实现函数开始指令(FN_BEGIN)
 */
static slice_t *riscv64_native_fn_begin(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    int64_t offset = c->stack_offset;
    offset += c->call_stack_max_offset;

    // 按16字节对齐栈
    offset = align_up(offset, 16);

    // 保存返回地址和帧指针
    // 对于RISC-V，需要保存ra(x1)和fp(s0/x8)
    slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_SP), RO_REG(R_SP), RO_IMM(-16)));
    slice_push(operations, RISCV64_INST(RV_SD, RO_REG(RA), RO_INDIRECT(R_SP, 8, QWORD)));
    slice_push(operations, RISCV64_INST(RV_SD, RO_REG(R_FP), RO_INDIRECT(R_SP, 0, QWORD)));

    // 更新帧指针
    slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_FP), RO_REG(R_SP), RO_IMM(0)));

    // 分配栈空间
    if (offset > 0) {
        slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_SP), RO_REG(R_SP), RO_IMM(-offset)));
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

/**
 * 实现函数结束指令(FN_END)
 */
static slice_t *riscv64_native_fn_end(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    // 恢复栈指针
    if (c->stack_offset != 0) {
        slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_SP), RO_REG(R_SP), RO_IMM(c->stack_offset)));
    }

    // 恢复帧指针和返回地址
    slice_push(operations, RISCV64_INST(RV_LD, RO_REG(R_FP), RO_INDIRECT(R_SP, 0, QWORD)));
    slice_push(operations, RISCV64_INST(RV_LD, RO_REG(RA), RO_INDIRECT(R_SP, 8, QWORD)));
    slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_SP), RO_REG(R_SP), RO_IMM(16)));

    // 返回
    slice_push(operations, RISCV64_INST(RV_RET));

    return operations;
}

/**
 * 实现加载有效地址指令(LEA)
 */
static slice_t *riscv64_native_lea(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *addr = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    if (op->first->assert_type == LIR_OPERAND_SYMBOL_LABEL ||
        op->first->assert_type == LIR_OPERAND_SYMBOL_VAR) {
        // 加载符号地址，使用 LA 伪指令, LA 指令和 CALL 指令使用同样的重定位方式
        addr->symbol.reloc_type = ASM_RISCV64_RELOC_CALL;
        slice_push(operations, RISCV64_INST(RV_LA, result, addr));
    } else if (op->first->assert_type == LIR_OPERAND_STACK ||
               op->first->assert_type == LIR_OPERAND_INDIRECT_ADDR) {
        // 计算栈变量或间接地址的有效地址
        reg_t *base = NULL;
        int64_t offset = 0;

        if (op->first->assert_type == LIR_OPERAND_STACK) {
            lir_stack_t *stack = op->first->value;
            base = R_FP;
            offset = stack->slot;
        } else {
            lir_indirect_addr_t *mem = op->first->value;
            assert(mem->base->assert_type == LIR_OPERAND_REG);
            base = mem->base->value;
            offset = mem->offset;
        }

        if (offset >= -2048 && offset <= 2047) {
            // 偏移量在立即数范围内，直接使用ADDI
            slice_push(operations, RISCV64_INST(RV_ADDI, result, RO_REG(base), RO_IMM(offset)));
        } else {
            // 偏移量超出范围，需要先加载到寄存器
            riscv64_asm_operand_t *temp_reg = RO_REG(T6);
            riscv64_mov_imm(op, operations, temp_reg, offset);
            slice_push(operations, RISCV64_INST(RV_ADD, result, RO_REG(base), temp_reg));
        }
    }

    return operations;
}

/**
 * 实现空操作指令(NOP)
 */
static slice_t *riscv64_native_nop(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    return operations;
}

/**
 * 符号扩展指令(SEXT)
 */
static slice_t *riscv64_native_sext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 根据源操作数大小选择合适的符号扩展指令
    if (source->size == BYTE) {
        slice_push(operations, RISCV64_INST(RV_SEXT_B, result, source));
    } else if (source->size == WORD) {
        slice_push(operations, RISCV64_INST(RV_SEXT_H, result, source));
    } else if (source->size == DWORD && result->size == QWORD) {
        slice_push(operations, RISCV64_INST(RV_SEXT_W, result, source));
    } else {
        // 其他情况，直接移动
        slice_push(operations, RISCV64_INST(RV_MV, result, source));
    }

    return operations;
}

/**
 * 无符号扩展指令(ZEXT)
 */
static slice_t *riscv64_native_zext(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 根据源操作数大小选择合适的无符号扩展指令
    if (source->size == BYTE) {
        slice_push(operations, RISCV64_INST(RV_ZEXT_B, result, source));
    } else if (source->size == WORD) {
        slice_push(operations, RISCV64_INST(RV_ZEXT_H, result, source));
    } else if (source->size == DWORD && result->size == QWORD) {
        slice_push(operations, RISCV64_INST(RV_ZEXT_W, result, source));
    } else {
        // 其他情况，直接移动
        slice_push(operations, RISCV64_INST(RV_MV, result, source));
    }

    return operations;
}

/**
 * 截断指令(TRUNC)
 */
static slice_t *riscv64_native_trunc(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *source = lir_operand_trans_riscv64(c, op, op->first, operations);
    riscv64_asm_operand_t *result = lir_operand_trans_riscv64(c, op, op->output, operations);

    // 先将源操作数移动到目标寄存器
    slice_push(operations, RISCV64_INST(RV_MV, result, source));

    // 根据目标寄存器的大小应用适当的掩码进行截断
    if (result->size == BYTE) {
        // 截断为8位
        slice_push(operations, RISCV64_INST(RV_ANDI, result, result, RO_IMM(0xFF)));
    } else if (result->size == WORD) {
        // 截断为16位
        slice_push(operations, RISCV64_INST(RV_ANDI, result, result, RO_IMM(0xFFFF)));
    } else if (result->size == DWORD) {
        // 截断为32位
        // 在RISC-V中，32位操作自动清除高32位
        slice_push(operations, RISCV64_INST(RV_ADDW, result, result, RO_REG(ZEROREG)));
    }

    return operations;
}

static slice_t *riscv64_native_safepoint(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();
    return operations; // TODO 暂时不添加 safepoint

    assert(op->output && op->output->assert_type == LIR_OPERAND_REG);
    assert(BUILD_OS == OS_LINUX);

    reg_t *a0_reg = op->output->value;
    assert(a0_reg->index == A0->index);

    riscv64_asm_operand_t *a0_operand = RO_REG(a0_reg);
    a0_operand->size = a0_reg->size;

    // 在 Linux 上，RISC-V 使用 tp 寄存器 (x4) 作为 TLS 基址
    // 1. 加载 TLS 变量地址到 a0
    // 使用 LA 指令加载 TLS 变量的地址
    riscv64_asm_operand_t *tls_symbol = RO_SYM(TLS_YIELD_SAFEPOINT_IDENT, false, 0, ASM_RISCV64_RELOC_CALL);
    slice_push(operations, RISCV64_INST(RV_LA, a0_operand, tls_symbol));

    // 2. 从 TLS 变量地址加载实际的 safepoint 值
    // ld a0, 0(a0)
    slice_push(operations, RISCV64_INST(RV_LD, a0_operand, RO_INDIRECT(a0_reg, 0, POINTER_SIZE)));

    // 3. 检查 safepoint 值是否为 0
    // beq a0, zero, skip_call (跳过 8 字节，即跳过下面的 call 指令)
    slice_push(operations, RISCV64_INST(RV_BEQ, a0_operand, RO_REG(ZEROREG), RO_IMM(8)));

    // 4. 如果不为 0，调用 assist_preempt_yield 函数
    riscv64_asm_operand_t *yield_func = RO_SYM(ASSIST_PREEMPT_YIELD_IDENT, false, 0, ASM_RISCV64_RELOC_CALL);
    slice_push(operations, RISCV64_INST(RV_CALL, yield_func));

    return operations;
}

static slice_t *riscv64_native_push(closure_t *c, lir_op_t *op) {
    slice_t *operations = slice_new();

    riscv64_asm_operand_t *value = lir_operand_trans_riscv64(c, op, op->first, operations);

    // 先减少 SP (栈指针向下增长)
    slice_push(operations, RISCV64_INST(RV_ADDI, RO_REG(R_SP), RO_REG(R_SP), RO_IMM(-value->size)));

    // 将值存储到栈上
    assert(value->size >= 4);
    if (value->size == 4) {
        slice_push(operations, RISCV64_INST(RV_SW, value, RO_INDIRECT(R_SP, 0, value->size)));
    } else {
        slice_push(operations, RISCV64_INST(RV_SD, value, RO_INDIRECT(R_SP, 0, value->size)));
    }

    return operations;
}

// 指令分发表
static riscv64_native_fn riscv64_native_table[] = {
        [LIR_OPCODE_CLR] = riscv64_native_clr,
        [LIR_OPCODE_CLV] = riscv64_native_clv,
        [LIR_OPCODE_NOP] = riscv64_native_nop,
        [LIR_OPCODE_CALL] = riscv64_native_call,
        [LIR_OPCODE_RT_CALL] = riscv64_native_call,
        [LIR_OPCODE_LABEL] = riscv64_native_label,
        [LIR_OPCODE_PUSH] = riscv64_native_push, // 待实现
        [LIR_OPCODE_RETURN] = riscv64_native_nop, // 函数返回值处理已在FN_END中完成
        [LIR_OPCODE_BREAK] = riscv64_native_nop,
        [LIR_OPCODE_BEQ] = riscv64_native_beq,
        [LIR_OPCODE_BAL] = riscv64_native_bal,

        // 一元运算符
        [LIR_OPCODE_NEG] = riscv64_native_neg,

        // 位运算
        [LIR_OPCODE_XOR] = riscv64_native_xor,
        [LIR_OPCODE_NOT] = riscv64_native_not,
        [LIR_OPCODE_OR] = riscv64_native_or,
        [LIR_OPCODE_AND] = riscv64_native_and,
        [LIR_OPCODE_SSHR] = riscv64_native_shift,
        [LIR_OPCODE_USHL] = riscv64_native_shift,
        [LIR_OPCODE_USHR] = riscv64_native_shift,

        // 算数运算
        [LIR_OPCODE_ADD] = riscv64_native_add,
        [LIR_OPCODE_SUB] = riscv64_native_sub,
        [LIR_OPCODE_UDIV] = riscv64_native_div,
        [LIR_OPCODE_SDIV] = riscv64_native_div,
        [LIR_OPCODE_MUL] = riscv64_native_mul,
        [LIR_OPCODE_UREM] = riscv64_native_rem,
        [LIR_OPCODE_SREM] = riscv64_native_rem,

        // 逻辑相关运算符
        [LIR_OPCODE_SGT] = riscv64_native_scc,
        [LIR_OPCODE_SGE] = riscv64_native_scc,
        [LIR_OPCODE_SLT] = riscv64_native_scc,
        [LIR_OPCODE_USLT] = riscv64_native_scc,
        [LIR_OPCODE_SLE] = riscv64_native_scc,
        [LIR_OPCODE_SEE] = riscv64_native_scc,
        [LIR_OPCODE_SNE] = riscv64_native_scc,

        [LIR_OPCODE_MOVE] = riscv64_native_mov,
        [LIR_OPCODE_LEA] = riscv64_native_lea,
        [LIR_OPCODE_UEXT] = riscv64_native_zext,
        [LIR_OPCODE_SEXT] = riscv64_native_sext,
        [LIR_OPCODE_TRUNC] = riscv64_native_trunc,
        [LIR_OPCODE_FN_BEGIN] = riscv64_native_fn_begin,
        [LIR_OPCODE_FN_END] = riscv64_native_fn_end,

        [LIR_OPCODE_SAFEPOINT] = riscv64_native_safepoint,
};

/**
 * 指令分发函数
 */
static slice_t *riscv64_native_op(closure_t *c, lir_op_t *op) {
    riscv64_native_fn fn = riscv64_native_table[op->code];
    assert(fn && "riscv64 native op not support");
    return fn(c, op);
}

/**
 * 处理基本块
 */
static slice_t *riscv64_native_block(closure_t *c, basic_block_t *block) {
    slice_t *operations = slice_new();
    linked_node *current = linked_first(block->operations);

    while (current->value != NULL) {
        lir_op_t *op = current->value;
        slice_concat(operations, riscv64_native_op(c, op));
        current = current->succ;
    }

    return operations;
}

/**
 * 主入口函数
 */
void riscv64_native(closure_t *c) {
    assert(c->module);

    // 遍历所有基本块
    for (int i = 0; i < c->blocks->count; ++i) {
        basic_block_t *block = c->blocks->take[i];
        slice_concat(c->asm_operations, riscv64_native_block(c, block));
    }
}
