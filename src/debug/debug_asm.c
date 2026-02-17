#include "debug_asm.h"
#include "utils/helper.h"
#include <assert.h>

static string amd64_asm_operand_to_string(amd64_asm_operand_t *operand) {
    void *value = operand->value;
    switch (operand->type) {
        case AMD64_ASM_OPERAND_TYPE_UINT8: {
            return utoa(((asm_uint8_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_UINT16: {
            return utoa(((asm_uint16_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_UINT:
        case AMD64_ASM_OPERAND_TYPE_UINT32: {
            return utoa(((asm_uint32_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_UINT64: {
            return utoa(((asm_uint64_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_INT8: {
            return itoa(((asm_int8_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_INT32: {
            return itoa(((asm_int32_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_FREG:
        case AMD64_ASM_OPERAND_TYPE_REG: {
            return ((reg_t *) value)->name;
        }
        case AMD64_ASM_OPERAND_TYPE_DISP_REG: {
            asm_disp_reg_t *disp = value;
            return dsprintf("[%s%+d|%d]", disp->reg->name, disp->disp, operand->size);
        }
        case AMD64_ASM_OPERAND_TYPE_SEG_OFFSET: {
            asm_seg_offset_t *seg = value;
            return dsprintf("[%s%+d]", seg->name, seg->offset);
        }
        case AMD64_ASM_OPERAND_TYPE_RIP_RELATIVE: {
            return dsprintf("[rip%+d|%d]", ((asm_rip_relative_t *) value)->disp, operand->size);
        }
        case AMD64_ASM_OPERAND_TYPE_INDIRECT_REG: {
            return dsprintf("[%s|%d]", ((asm_indirect_reg_t *) value)->reg->name, operand->size);
        }
        case AMD64_ASM_OPERAND_TYPE_SIB_REG: {
            // [base+index*scale+disp]
            asm_sib_reg_t *sib = (asm_sib_reg_t *) value;
            if (sib->base && strstr(sib->base->name, rsp->name)) {
                return dsprintf("[%s%+d|%d]", sib->base->name, sib->disp, operand->size);
            }

            return dsprintf("[%s+%s*%d+%d]", sib->base ? sib->base->name : "_", sib->index ? sib->index->name : "_",
                            sib->scale,
                            sib->disp);
        }
        case AMD64_ASM_OPERAND_TYPE_SYMBOL: {
            return dsprintf("%s", ((asm_symbol_t *) value)->name);
        }

        case AMD64_ASM_OPERAND_TYPE_FLOAT32: {
            return dsprintf("%f", ((asm_float32_t *) value)->value);
        }
        case AMD64_ASM_OPERAND_TYPE_FLOAT64: {
            return dsprintf("%f", ((asm_float64_t *) value)->value);
        }
        default: {
            assert(false && "unknown operand type");
        }
    }
}

static string arm64_asm_operand_to_string(arm64_asm_operand_t *operand) {
    switch (operand->type) {
        case ARM64_ASM_OPERAND_FREG:
        case ARM64_ASM_OPERAND_REG: {
            return operand->reg.name;
        }
        case ARM64_ASM_OPERAND_IMMEDIATE: {
            return dsprintf("#%ld", operand->immediate);
        }
        case ARM64_ASM_OPERAND_SYMBOL: {
            if (operand->symbol.offset != 0) {
                return dsprintf("%s%s%ld",
                                operand->symbol.name,
                                operand->symbol.offset >= 0 ? "+" : "",
                                operand->symbol.offset);
            }
            return operand->symbol.name;
        }
        case ARM64_ASM_OPERAND_INDIRECT: {
            // [reg] 或 [reg,#offset] 或 [reg,#offset]! 或 [reg],#offset
            if (operand->indirect.offset == 0) {
                return dsprintf("[%s]", operand->indirect.reg->name);
            }

            switch (operand->indirect.prepost) {
                case 0: // 普通间接寻址
                    return dsprintf("[%s,#%ld]",
                                    operand->indirect.reg ? operand->indirect.reg->name : "_",
                                    operand->indirect.offset);
                case 1: // pre-index
                    return dsprintf("[%s,#%ld]!",
                                    operand->indirect.reg->name,
                                    operand->indirect.offset);
                case 2: // post-index
                    return dsprintf("[%s],#%ld",
                                    operand->indirect.reg->name,
                                    operand->indirect.offset);
                default:
                    assert(false && "invalid prepost value");
            }
        }
        case ARM64_ASM_OPERAND_REG_OFFSET: {
            // [base,index,extend #scale]
            string extend_str;
            switch (operand->reg_offset.extend) {
                case 0:
                    extend_str = "";
                    break;
                case 1:
                    extend_str = ",sxtw";
                    break;
                case 2:
                    extend_str = ",uxtw";
                    break;
                case 3:
                    extend_str = ",lsl";
                    break;
                case 4:
                    extend_str = ",sxtx";
                    break;
                default:
                    assert(false && "invalid extend type");
            }

            if (operand->reg_offset.scale == 0) {
                return dsprintf("[%s,%s%s]",
                                operand->reg_offset.base->name,
                                operand->reg_offset.index->name,
                                extend_str);
            } else {
                return dsprintf("[%s,%s%s #%ld]",
                                operand->reg_offset.base->name,
                                operand->reg_offset.index->name,
                                extend_str,
                                operand->reg_offset.scale);
            }
        }
        case ARM64_ASM_OPERAND_COND: {
            static char *cond_names[] = {
                    "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
                    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};

            if (operand->cond >= 0 && operand->cond < 16) {
                return cond_names[operand->cond];
            }
            return "invalid";
        }
        case ARM64_ASM_OPERAND_SHIFT: {
            return dsprintf("lsl #%ld", operand->immediate);
        }
        case ARM64_ASM_OPERAND_EXTEND: {
            static char *extend_types[] = {
                    "uxtb", "uxth", "uxtw", "uxtx",
                    "sxtb", "sxth", "sxtw", "sxtx"};
            if (operand->extend.option < 8) {
                if (operand->extend.imm == 0) {
                    return extend_types[operand->extend.option];
                } else {
                    return dsprintf("%s #%ld",
                                    extend_types[operand->extend.option],
                                    operand->extend.imm);
                }
            }
            return "invalid";
        }
        default: {
            assert(false && "unknown operand type");
            return "unknown";
        }
    }
}

static string riscv64_asm_operand_to_string(riscv64_asm_operand_t *operand) {
    switch (operand->type) {
        case RISCV64_ASM_OPERAND_FREG:
        case RISCV64_ASM_OPERAND_REG: {
            return operand->reg.name;
        }
        case RISCV64_ASM_OPERAND_IMMEDIATE: {
            return dsprintf("%ld", operand->immediate);
        }
        case RISCV64_ASM_OPERAND_SYMBOL: {
            if (operand->symbol.offset != 0) {
                return dsprintf("%s%s%ld",
                                operand->symbol.name,
                                operand->symbol.offset >= 0 ? "+" : "",
                                operand->symbol.offset);
            }
            return operand->symbol.name;
        }
        case RISCV64_ASM_OPERAND_INDIRECT: {
            // RISC-V 间接寻址格式: offset(reg)
            return dsprintf("%ld(%s)", operand->indirect.offset, operand->indirect.reg->name);
        }
        case RISCV64_ASM_OPERAND_ROUNDMODE: {
            static char *round_modes[] = {
                    "rne", "rtz", "rdn", "rup", "rmm"};
            if (operand->round_mode >= RISCV64_RNE && operand->round_mode <= RISCV64_RMM) {
                return round_modes[operand->round_mode];
            }
            return "";
        }
        default: {
            assert(false && "unknown operand type");
            return "unknown";
        }
    }
}

void amd64_asm_op_to_string(int i, amd64_asm_inst_t *op) {
    printf("%lu\t", op->op_id);
    if (str_equal(op->name, "label")) {
        printf("%s  ", op->name);
    } else {
        printf("\t\t%s  ", op->name);
    }

    if (op->count == 1) {
        printf("%s", amd64_asm_operand_to_string(op->operands[0]));
    } else if (op->count == 2) {
        printf("%s -> %s",
               amd64_asm_operand_to_string(op->operands[1]),
               amd64_asm_operand_to_string(op->operands[0]));
    } else if (op->count == 3) {
        printf("%s, %s, %s", amd64_asm_operand_to_string(op->operands[0]),
               amd64_asm_operand_to_string(op->operands[1]), amd64_asm_operand_to_string(op->operands[2]));
    } else if (op->count == 0) {
    } else {
        printf("not_support");
    }

    printf("\n");
    fflush(stdout);
}

void arm64_asm_op_to_string(int i, arm64_asm_inst_t *op) {
    printf("%lu\t", op->op_id);

    // 处理标签的特殊情况
    if (str_equal(arm64_raw_op_names[op->raw_opcode], "label")) {
        printf("%s  ", arm64_raw_op_names[op->raw_opcode]);
    } else {
        printf("\t\t%s  ", arm64_raw_op_names[op->raw_opcode]);
    }

    // ARM64 指令格式通常是: opcode dest, source1, source2
    // 例如: add x0, x1, x2
    switch (op->count) {
        case 0:
            break;
        case 1:
            printf("%s", arm64_asm_operand_to_string(op->operands[0]));
            break;
        case 2:
            // 大多数双操作数指令格式为: dest, source
            printf("%s, %s",
                   arm64_asm_operand_to_string(op->operands[0]),
                   arm64_asm_operand_to_string(op->operands[1]));
            break;
        case 3:
            // 三操作数指令格式为: dest, source1, source2
            printf("%s, %s, %s",
                   arm64_asm_operand_to_string(op->operands[0]),
                   arm64_asm_operand_to_string(op->operands[1]),
                   arm64_asm_operand_to_string(op->operands[2]));
            break;
        case 4:
            // 特殊情况，如 LDP/STP 指令
            printf("%s, %s, %s, %s",
                   arm64_asm_operand_to_string(op->operands[0]),
                   arm64_asm_operand_to_string(op->operands[1]),
                   arm64_asm_operand_to_string(op->operands[2]),
                   arm64_asm_operand_to_string(op->operands[3]));
            break;
        default:
            printf("not_support");
    }

    printf("\n");
    fflush(stdout);
}

void riscv64_asm_op_to_string(int i, riscv64_asm_inst_t *op) {
    printf("%lu\t", op->op_id);

    // 处理标签的特殊情况
    printf("\t\t%s  ", riscv64_raw_op_names[op->raw_opcode]);

    // RISC-V 指令格式通常是: opcode dest, source1, source2
    // 例如: add rd, rs1, rs2
    switch (op->count) {
        case 0:
            break;
        case 1:
            printf("%s", riscv64_asm_operand_to_string(op->operands[0]));
            break;
        case 2:
            // 大多数双操作数指令格式为: dest, source
            printf("%s, %s",
                   riscv64_asm_operand_to_string(op->operands[0]),
                   riscv64_asm_operand_to_string(op->operands[1]));
            break;
        case 3:
            // 三操作数指令格式为: dest, source1, source2
            printf("%s, %s, %s",
                   riscv64_asm_operand_to_string(op->operands[0]),
                   riscv64_asm_operand_to_string(op->operands[1]),
                   riscv64_asm_operand_to_string(op->operands[2]));
            break;
        case 4:
            // 特殊情况，如需要四个操作数的指令
            printf("%s, %s, %s, %s",
                   riscv64_asm_operand_to_string(op->operands[0]),
                   riscv64_asm_operand_to_string(op->operands[1]),
                   riscv64_asm_operand_to_string(op->operands[2]),
                   riscv64_asm_operand_to_string(op->operands[3]));
            break;
        default:
            printf("not_support");
    }

    printf("\n");
    fflush(stdout);
}

void asm_op_to_string(int i, void *op) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_asm_op_to_string(i, op);
    } else if (BUILD_ARCH == ARCH_ARM64) {
        return arm64_asm_op_to_string(i, op);
    } else if (BUILD_ARCH == ARCH_RISCV64) {
        return riscv64_asm_op_to_string(i, op);
    }

    assert(false);
}
