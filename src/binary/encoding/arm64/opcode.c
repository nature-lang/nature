#include "opcode.h"

#include "asm.h"
#include "src/register/arch/arm64.h"


static const uint32_t pre_post_map[] = {2, 3, 1};

static bool is_sp(reg_t *reg) {
    return str_equal(reg->name, "sp") && reg->index == sp->index;
}

static uint32_t asm_noop(arm64_asm_inst_t *inst) {
    return 0;
}


static uint32_t asm_mov(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0]; // dst 总是 REG
    arm64_asm_operand_t *opr2 = inst->operands[1];

    switch (opr2->type) {
        case ARM64_ASM_OPERAND_REG: {
            assert(opr1->reg.size == opr2->reg.size);
            uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

            if (is_sp(&opr1->reg) || is_sp(&opr2->reg)) {
                return P_MOV_SP(sz, opr1->reg.index, opr2->reg.index);
            } else {
                return P_MOV(sz, opr1->reg.index, opr2->reg.index);
            }
        }
        case ARM64_ASM_OPERAND_IMMEDIATE: {
            uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
            int64_t imm = opr2->immediate;
            if (imm >= 0) {
                return W_MOVZ(sz, opr1->reg.index, imm, 0);
            } else {
                return W_MOVN(sz, opr1->reg.index, ~imm, 0);
            }
        }
        default:
            assert(false);
            break;
    }

    return 0;
}

static uint32_t asm_movk(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    arm64_asm_operand_t *opr3 = inst->operands[2];

    assert(opr3->type == ARM64_ASM_OPERAND_SHIFT && (opr3->immediate & 15) == 0);
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    return W_MOVK(sz, opr1->reg.index, opr2->immediate, opr3->immediate >> 4);
}

static uint32_t asm_3r(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    arm64_asm_operand_t *opr3 = inst->operands[2];
    assert(opr1->reg.size == opr2->reg.size);
    assert(opr1->reg.size == opr3->reg.size);
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    switch (inst->opcode) {
        case ADD_R:
            if (inst->count == 3) {
                if (!is_sp(&opr1->reg) && !is_sp(&opr2->reg)) {
                    return W_ADD_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0, 0);
                } else {
                    return W_ADD_E(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 3, 0); // LSL 0
                }
            } else {
                arm64_asm_operand_t *ext = inst->operands[3];
                assert(ext->type == ARM64_ASM_OPERAND_EXTEND);
                int64_t option = ext->extend.option;
                if (option < 8) {
                    // extend
                    return W_ADD_E(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, option, ext->extend.imm);
                } else {
                    return W_ADD_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, option & 3, ext->extend.imm);
                }
            }
            break;
        case SUB_R:
            if (inst->count == 3) {
                if (!is_sp(&opr1->reg) && !is_sp(&opr2->reg)) {
                    return W_SUB_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0, 0);
                } else {
                    return W_SUB_E(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 3, 0); // LSL 0
                }
            } else {
                arm64_asm_operand_t *ext = inst->operands[3];
                assert(ext->type == ARM64_ASM_OPERAND_EXTEND);
                int64_t option = ext->extend.option;
                if (option < 8) {
                    // extend
                    return W_SUB_E(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, option, ext->extend.imm);
                } else {
                    return W_SUB_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, option & 3, ext->extend.imm);
                }
            }
            break;
        case MUL:
            return P_MUL(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        case SDIV:
            return W_SDIV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        case UDIV:
            return W_UDIV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        case AND:
            return W_AND_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0);
            break;
        case ORR:
            return W_ORR_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0);
            break;
        case EOR:
            return W_EOR_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0);
            break;
        case EON:
            return W_EON_S(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, 0);
            break;
        case LSL_R:
            return W_LSLV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        case LSR_R:
            return W_LSRV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        case ASR_R:
            return W_ASRV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
            break;
        default:
            assert(false);
    }
}

static uint32_t asm_2ri(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    arm64_asm_operand_t *opr3 = inst->operands[2];
    assert(opr1->reg.size == opr2->reg.size);
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    int64_t imm = 0;
    if (opr3->type == ARM64_ASM_OPERAND_IMMEDIATE) {
        if (opr3->immediate < -(1 << 12) || opr3->immediate >= (1 << 12)) {
            return 0;
        }
        imm = opr3->immediate;
    }

    switch (inst->opcode) {
        case ADD_I:
            if (imm >= 0) return W_ADD_I(sz, opr1->reg.index, opr2->reg.index, imm);
            else
                return W_SUB_I(sz, opr1->reg.index, opr2->reg.index, -imm);
        case SUB_I:
            if (imm >= 0) return W_SUB_I(sz, opr1->reg.index, opr2->reg.index, imm);
            else
                return W_ADD_I(sz, opr1->reg.index, opr2->reg.index, -imm);
        case LSL_I:
            return P_LSL_I(sz, opr1->reg.index, opr2->reg.index, imm);
        case LSR_I:
            return P_LSR_I(sz, opr1->reg.index, opr2->reg.index, imm);
        case ASR_I:
            return P_ASR_I(sz, opr1->reg.index, opr2->reg.index, imm);
        default:
            assert(false);
    }
}

static uint32_t asm_2r(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    switch (inst->opcode) {
        case CMP_R:
            return P_CMP(sz, opr1->reg.index, opr2->reg.index);
        case CMN_R:
            return P_CMN(sz, opr1->reg.index, opr2->reg.index);
        case SXTB:
        case SXTH:
        case SXTW: {
            uint32_t imms = (8U << (inst->opcode - SXTB)) - 1U;
            assert(opr2->reg.size == DWORD);
            return W_SBFM(sz, opr1->reg.index, sz, opr2->reg.index, 0, imms);
        }
        case UXTB:
        case UXTH: {
            uint32_t imms = (8U << (inst->opcode - UXTB)) - 1U;
            assert(opr2->reg.size == DWORD);
            return W_UBFM(0, opr1->reg.index, 0, opr2->reg.index, 0, imms);
        }
        case UXTW:
            return P_MOV(0, opr1->reg.index, opr2->reg.index);
        default:
            assert(false);
    }
}

static uint32_t asm_ri(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    uint32_t imm = 0;
    if (opr2->type == ARM64_ASM_OPERAND_IMMEDIATE) {
        if (opr2->immediate < 0 || opr2->immediate >= (1 << 12))
            return 0;
        imm = opr2->immediate;
    }

    switch (inst->opcode) {
        case CMP_I:
            return P_CMP_I(sz, opr1->reg.index, imm);
        case CMN_I:
            return P_CMN_I(sz, opr1->reg.index, imm);
        default:
            assert(false);
    }
}

static uint32_t asm_4r(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    arm64_asm_operand_t *opr3 = inst->operands[2];
    arm64_asm_operand_t *opr4 = inst->operands[3];
    assert(opr1->reg.size == opr2->reg.size);
    assert(opr1->reg.size == opr3->reg.size);
    assert(opr1->reg.size == opr4->reg.size);
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    switch (inst->opcode) {
        case MADD:
            return W_MADD(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, opr4->reg.index);
        case MSUB:
            return W_MSUB(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index, opr4->reg.index);
        default:
            assert(false);
    }
}

static uint32_t asm_ldrstr(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    arm64_asm_operand_t *opr2 = inst->operands[1];
    if (opr2->type == ARM64_ASM_OPERAND_INDIRECT) {
        assert(opr2->indirect.reg->size == QWORD);

        // 检查是否是带符号的间接寻址
        if (opr2->indirect.indirect_sym) {
            // 对于 TLS 访问，我们只需要返回基本的 LDR 指令
            // 实际的符号重定位会在汇编器阶段处理
            uint32_t base = opr2->indirect.reg->index;
            uint32_t b = inst->opcode - LDRB;
            uint32_t s = 0;
            if (b >= 3) {
                b -= 3;
                s = 1;
            }
            b |= sz;

            return W_LDR_UIMM(b, s, opr1->reg.index, 0, base);
            // 其他指令类型如果需要支持符号重定位，可以在这里添加
            assert(false && "Unsupported instruction type for symbol relocation");
            return 0;
        }

        int64_t offset = opr2->indirect.offset;
        assert(offset <= 4095 && offset >= -4096);
        uint32_t base = opr2->indirect.reg->index;
        uint32_t prepost = pre_post_map[opr2->indirect.prepost];
        switch (inst->opcode) {
            case LDRB:
            case LDRH:
            case LDR:
            case LDRSB:
            case LDRSH: {
                uint32_t b = inst->opcode - LDRB, s = 0;
                if (b >= 3) {
                    b -= 3;
                    s = 1;
                }
                b |= sz;
                if (opr2->indirect.prepost == 0) {
                    if (offset >= 0) {
                        if (inst->opcode == LDR) {
                            offset = offset >> (2 + sz);
                        } else if (inst->opcode == LDRH || inst->opcode == LDRSH) {
                            offset = offset >> 1;
                        }

                        return W_LDR_UIMM(b, s, opr1->reg.index, offset, base);
                    } else {
                        return W_LDUR(b, s, opr1->reg.index, offset, base);
                    }
                } else {
                    return W_LDR(b, s, opr1->reg.index, offset, base, prepost);
                }
            } break;
            case STRB:
            case STRH:
            case STR:
                if (opr2->indirect.prepost == 0) {
                    if (offset >= 0) {
                        if (inst->opcode == STR) {
                            offset = offset >> (2 + sz);
                        } else if (inst->opcode == STRH) {
                            offset = offset >> 1;
                        }

                        return W_STR_UIMM((inst->opcode - STRB) | sz, opr1->reg.index, offset, base);
                    } else {
                        return W_STUR((inst->opcode - STRB) | sz, opr1->reg.index, offset, base);
                    }
                } else {
                    return W_STR((inst->opcode - STRB) | sz, opr1->reg.index, offset, base, prepost);
                }
                break;
            default:
                assert(false);
                break;
        }
    } else {
        assert(opr2->type == ARM64_ASM_OPERAND_REG_OFFSET);
        assert(opr2->reg_offset.scale == 0);

        static const uint32_t opts[] = {3, 6, 2, 3, 3};
        uint32_t opt = opts[opr2->reg_offset.extend];

        switch (inst->opcode) {
            case LDRB:
            case LDRH:
            case LDR:
            case LDRSB:
            case LDRSH: {
                uint32_t b = inst->opcode - LDRB, s = 0;
                if (b >= 3) {
                    b -= 3;
                    s = 1;
                }
                b |= sz;

                uint32_t s2 = 0;
                int64_t scale = opr2->reg_offset.scale;
                if (scale != 0) {
                    if (!(scale == 0 || scale == b)) {
                        // TODO: Error message.
                        return 0;
                    }
                    s2 = scale == b;
                }
                return W_LDR_R(b, opr1->reg.index, opr2->reg_offset.base->index, opr2->reg_offset.index->index,
                               s, s2, opt);
            } break;
            case STRB:
            case STRH:
            case STR: {
                uint32_t b = (inst->opcode - STRB) | sz;

                uint32_t s2 = 0;
                int64_t scale = opr2->reg_offset.scale;
                if (scale != 0) {
                    int64_t n = scale;
                    if (!(n == 0 || n == b)) {
                        // TODO: Error message.
                        return 0;
                    }
                    s2 = n == b;
                }
                return W_STR_R(b, opr1->reg.index, opr2->reg_offset.base->index, opr2->reg_offset.index->index,
                               s2, opt);
            }
            default:
                assert(false);
        }
    }
}

static uint32_t asm_ldpstp(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    assert(opr1->reg.size == opr2->reg.size);
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    arm64_asm_operand_t *opr3 = inst->operands[2];
    assert(opr3->indirect.reg->size == QWORD);
    int64_t offset = opr3->indirect.offset;
    assert(offset < (1 << (6 + 3)) && offset >= -(1 << (6 + 3)));
    uint32_t base = opr3->indirect.reg->index;
    uint32_t prepost = pre_post_map[opr3->indirect.prepost];
    switch (inst->opcode) {
        case LDP:
            return W_LDP(sz, opr1->reg.index, opr2->reg.index, offset, base, prepost);
        case STP:
            return W_STP(sz, opr1->reg.index, opr2->reg.index, offset, base, prepost);
        default:
            assert(false);
    }
}

static uint32_t asm_adrp(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    if (opr1->reg.size == QWORD) {
        return W_ADRP(opr1->reg.index, 0);
    }
    return 0;
}

static uint32_t asm_cset(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    return P_CSET(sz, opr1->reg.index, opr2->cond);
}

static uint32_t asm_b(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    int64_t offset;

    if (opr1->type == ARM64_ASM_OPERAND_SYMBOL) {
        offset = opr1->symbol.offset;
    } else if (opr1->type == ARM64_ASM_OPERAND_IMMEDIATE) {
        offset = opr1->immediate;
    } else {
        assert(false); // 不支持的操作数类型
        return 0;
    }

    offset = offset / 4;

    return W_B(offset);
}


static uint32_t asm_bl(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    int64_t offset;

    if (opr1->type == ARM64_ASM_OPERAND_SYMBOL) {
        offset = opr1->symbol.offset;
    } else if (opr1->type == ARM64_ASM_OPERAND_IMMEDIATE) {
        offset = opr1->immediate;
    } else {
        assert(false); // 不支持的操作数类型
        return 0;
    }

    offset = offset / 4;

    return W_BL(offset);
}

static uint32_t asm_br(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    if (opr1->reg.size == QWORD) {
        return W_BR(opr1->reg.index);
    }
    return 0;
}

static uint32_t asm_bcc(arm64_asm_inst_t *inst) {
    uint32_t cond = inst->opcode == B ? BAL - BEQ : inst->opcode - BEQ;
    int64_t offset = 0;

    arm64_asm_operand_t *opr1 = inst->operands[0];
    if (opr1->type == ARM64_ASM_OPERAND_IMMEDIATE) {
        offset = opr1->immediate;
    } else if (opr1->type == ARM64_ASM_OPERAND_SYMBOL) {
        offset = opr1->symbol.offset;
    } else {
        assert(false); // 不支持的操作数类型
        return 0;
    }

    offset = offset / 4; // 将偏移量调整为指令地址的单位
    return W_BCC(cond, offset);
}


static uint32_t asm_blr(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    if (opr1->reg.size == QWORD) {
        return W_BLR(opr1->reg.index);
    }
    return 0;
}

static uint32_t asm_ret(arm64_asm_inst_t *inst) {
    return W_RET(LR);
}

static uint32_t asm_svc(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    return W_SVC(opr1->immediate);
}

static uint32_t asm_mrs(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0]; // 目标寄存器
    arm64_asm_operand_t *opr2 = inst->operands[1]; // 系统寄存器编号

    // 确保目标寄存器是64位的
    if (opr1->reg.size == QWORD) {
        // 从立即数操作数中获取系统寄存器编号
        uint32_t sysreg = (uint32_t) opr2->immediate;
        return W_MRS(opr1->reg.index, sysreg);
    }

    return 0;
}

static uint32_t asm_f_ldrstr(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    if (opr2->type == ARM64_ASM_OPERAND_INDIRECT) {
        assert(opr2->indirect.reg->size == QWORD);
        int64_t offset = opr2->indirect.offset;
        assert(offset < (1 << (6 + 3)) && offset >= -(1 << (6 + 3)));
        uint32_t base = opr2->indirect.reg->index;
        uint32_t prepost = pre_post_map[opr2->indirect.prepost];

        switch (inst->opcode) {
            case F_LDR: {
                uint32_t s = 0;
                if (opr2->indirect.prepost == 0) {
                    if (offset >= 0)
                        return F_LDR_UIMM(sz, s, opr1->reg.index, offset >> (2 + sz), base);
                    else
                        return F_LDUR(sz, s, opr1->reg.index, offset, base);
                } else {
                    return F_LDR(sz, s, opr1->reg.index, offset, base, prepost);
                }
            } break;
            case F_STR:
                if (opr2->indirect.prepost == 0) {
                    if (offset >= 0)
                        return F_STR_UIMM(sz, opr1->reg.index, offset >> (2 + sz), base);
                    else
                        return F_STUR(sz, opr1->reg.index, offset, base);
                } else {
                    return F_STR(sz, opr1->reg.index, offset, base, prepost);
                }
                break;
            default:
                assert(false);
                break;
        }
    } else {
        assert(opr2->type == ARM64_ASM_OPERAND_REG_OFFSET);
        int64_t scale = opr2->reg_offset.scale;

        static const uint32_t opts[] = {3, 6, 2, 3, 3};
        uint32_t opt = opts[opr2->reg_offset.extend];
        uint32_t s = opr2->reg_offset.extend > 0 ? 1 : 0;

        switch (inst->opcode) {
            case F_LDR:
                return F_LDR_R(sz, opr1->reg.index, opr2->reg_offset.base->index, opr2->reg_offset.index->index, 0, s,
                               opt);
            case F_STR:
                return F_STR_R(sz, opr1->reg.index, opr2->reg_offset.base->index, opr2->reg_offset.index->index, s,
                               opt);
            default:
                assert(false);
        }
    }
}

static uint32_t asm_f_ldpstp(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;
    arm64_asm_operand_t *opr3 = inst->operands[2];
    assert(opr3->indirect.reg->size == QWORD);
    int64_t offset = opr3->indirect.offset;
    assert(offset < (1 << (6 + 3)) && offset >= -(1 << (6 + 3)));
    uint32_t base = opr3->indirect.reg->index;
    uint32_t prepost = pre_post_map[opr3->indirect.prepost];
    switch (inst->opcode) {
        case F_LDP:
            return F_LDP(sz, opr1->reg.index, opr2->reg.index, base, offset, prepost);
        case F_STP:
            return F_STP(sz, opr1->reg.index, opr2->reg.index, base, offset, prepost);
        default:
            assert(false);
            return 0;
    }
}

static uint32_t asm_f_3r(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    arm64_asm_operand_t *opr3 = inst->operands[2];
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    switch (inst->opcode) {
        case FADD:
            return FADD(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
        case FSUB:
            return FSUB(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
        case FMUL:
            return FMUL(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
        case FDIV:
            return FDIV(sz, opr1->reg.index, opr2->reg.index, opr3->reg.index);
        default:
            assert(false);
            return 0;
    }
}

static uint32_t asm_f_2r(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0];
    arm64_asm_operand_t *opr2 = inst->operands[1];
    uint32_t dsz = opr1->reg.size == QWORD ? 1 : 0;
    uint32_t ssz = opr2->reg.size == QWORD ? 1 : 0;

    switch (inst->opcode) {
        case FMOV:
            if (opr2->type == ARM64_ASM_OPERAND_REG) {
                return FMOV_I(dsz, opr1->reg.index, opr2->reg.index);
            }
            return FMOV(dsz, opr1->reg.index, opr2->reg.index);
        case FCMP:
            return FCMP(dsz, opr1->reg.index, opr2->reg.index);
        case FNEG:
            return FNEG(dsz, opr1->reg.index, opr2->reg.index);
        case FSQRT:
            return FSQRT(dsz, opr1->reg.index, opr2->reg.index);
        case SCVTF:
            return SCVTF(ssz, opr1->reg.index, dsz, opr2->reg.index);
        case UCVTF:
            return UCVTF(ssz, opr1->reg.index, dsz, opr2->reg.index);
        case FCVT:
            return FCVT(dsz, opr1->reg.index, opr2->reg.index);
        case FCVTZS:
            return FCVTZS(dsz, opr1->reg.index, ssz, opr2->reg.index);
        case FCVTZU:
            return FCVTZU(dsz, opr1->reg.index, ssz, opr2->reg.index);
        default:
            assert(false);
            return 0;
    }
}

static uint32_t asm_mvn(arm64_asm_inst_t *inst) {
    arm64_asm_operand_t *opr1 = inst->operands[0]; // 目标寄存器
    arm64_asm_operand_t *opr2 = inst->operands[1]; // 源寄存器
    uint32_t sz = opr1->reg.size == QWORD ? 1 : 0;

    // MVN 实际上是 ORN Rd, ZR, Rm 的别名指令
    return W_ORN_S(sz, opr1->reg.index, wzr->index, opr2->reg.index, 0);
}

// raw_opcode to opcode
arm64_opr_flags_list arm64_opcode_map[] = {
        [R_MOV] = {
                3, (arm64_opr_flags *[]) {
                           &(arm64_opr_flags) {MOV, {R32, R32}},
                           &(arm64_opr_flags) {MOV, {R64 | RSP, R64 | RSP}},
                           &(arm64_opr_flags) {MOV, {R32 | R64, IMM}},
                   }},
        [R_MOVK] = {1, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {MOVK, {R32 | R64, IMM, SFT}},
                       }},
        [R_ADD] = {9, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {ADD_R, {R32, R32, R32}}, // 三个操作数
                              &(arm64_opr_flags) {ADD_R, {R32, R32, R32, EXT}},
                              &(arm64_opr_flags) {ADD_I, {R32, R32, IMM}},
                              &(arm64_opr_flags) {ADD_I, {R32, R32, SYM}},
                              &(arm64_opr_flags) {ADD_R, {R64, R64, R64}},
                              &(arm64_opr_flags) {ADD_R, {R64, R64, R64, EXT}},
                              &(arm64_opr_flags) {ADD_R, {R64 | RSP, R64 | RSP, R64}},
                              &(arm64_opr_flags) {ADD_I, {R64 | RSP, R64 | RSP, IMM}},
                              &(arm64_opr_flags) {ADD_I, {R64 | RSP, R64 | RSP, SYM}},
                      }},
        [R_SUB] = {9, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {SUB_R, {R32, R32, R32}},
                              &(arm64_opr_flags) {SUB_R, {R32, R32, R32, EXT}},
                              &(arm64_opr_flags) {SUB_I, {R32, R32, IMM}},
                              &(arm64_opr_flags) {SUB_I, {R32, R32, SYM}},
                              &(arm64_opr_flags) {SUB_R, {R64, R64, R64}},
                              &(arm64_opr_flags) {SUB_R, {R64, R64, R64, EXT}},
                              &(arm64_opr_flags) {SUB_R, {R64 | RSP, R64 | RSP, R64}},
                              &(arm64_opr_flags) {SUB_I, {R64 | RSP, R64 | RSP, IMM}},
                              &(arm64_opr_flags) {SUB_I, {R64 | RSP, R64 | RSP, SYM}},
                      }},
        [R_MUL] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {MUL, {R32, R32, R32}}, &(arm64_opr_flags) {MUL, {R64, R64, R64}}}},
        [R_SDIV] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SDIV, {R32, R32, R32}}, &(arm64_opr_flags) {SDIV, {R64, R64, R64}}}},
        [R_UDIV] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {UDIV, {R32, R32, R32}}, &(arm64_opr_flags) {UDIV, {R64, R64, R64}}}},
        [R_MADD] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {MADD, {R32, R32, R32, R32}}, &(arm64_opr_flags) {MADD, {R64, R64, R64, R64}}}},
        [R_MSUB] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {MSUB, {R32, R32, R32, R32}}, &(arm64_opr_flags) {MSUB, {R64, R64, R64, R64}}}},
        [R_AND] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {AND, {R32, R32, R32}}, &(arm64_opr_flags) {AND, {R64, R64, R64}}}},
        [R_ORR] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {ORR, {R32, R32, R32}}, &(arm64_opr_flags) {ORR, {R64, R64, R64}}}},
        [R_EOR] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {EOR, {R32, R32, R32}}, &(arm64_opr_flags) {EOR, {R64, R64, R64}}}},
        [R_EON] = {2, (arm64_opr_flags *[]) {&(arm64_opr_flags) {EON, {R32, R32, R32}}, &(arm64_opr_flags) {EON, {R64, R64, R64}}}},
        [R_CMP] = {3, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {CMP_R, {R32, R32}},
                              &(arm64_opr_flags) {CMP_R, {R64, R64}},
                              &(arm64_opr_flags) {CMP_I, {R32 | R64, IMM}},
                      }},
        [R_CMN] = {3, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {CMN_R, {R32, R32}},
                              &(arm64_opr_flags) {CMN_R, {R64, R64}},
                              &(arm64_opr_flags) {CMN_I, {R32 | R64, IMM}},
                      }},
        [R_LSL] = {4, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {LSL_R, {R32, R32, R32}},
                              &(arm64_opr_flags) {LSL_I, {R32, R32, IMM}},
                              &(arm64_opr_flags) {LSL_R, {R64, R64, R64}},
                              &(arm64_opr_flags) {LSL_I, {R64, R64, IMM}},
                      }},
        [R_LSR] = {4, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {LSR_R, {R32, R32, R32}},
                              &(arm64_opr_flags) {LSR_I, {R32, R32, IMM}},
                              &(arm64_opr_flags) {LSR_R, {R64, R64, R64}},
                              &(arm64_opr_flags) {LSR_I, {R64, R64, IMM}},
                      }},
        [R_ASR] = {4, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {ASR_R, {R32, R32, R32}},
                              &(arm64_opr_flags) {ASR_I, {R32, R32, IMM}},
                              &(arm64_opr_flags) {ASR_R, {R64, R64, R64}},
                              &(arm64_opr_flags) {ASR_I, {R64, R64, IMM}},
                      }},
        [R_SXTB] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SXTB, {R32 | R64, R32}}}},
        [R_SXTH] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SXTH, {R32 | R64, R32}}}},
        [R_SXTW] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SXTW, {R64, R32}}}},
        [R_UXTB] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {UXTB, {R32 | R64, R32}}}},
        [R_UXTH] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {UXTH, {R32 | R64, R32}}}},
        [R_UXTW] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {UXTW, {R64, R32}}}},
        [R_LDRB] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {LDRB, {R32 | R64, IND | ROI}}}},
        [R_LDRH] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {LDRH, {R32 | R64, IND | ROI}}}},
        [R_LDR] = {2, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {LDR, {R32 | R64, IND | ROI}},
                              &(arm64_opr_flags) {F_LDR, {F32 | F64, IND | ROI}},
                      }},
        [R_LDRSB] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {LDRSB, {R32 | R64, IND | ROI}}}},
        [R_LDRSH] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {LDRSH, {R32 | R64, IND | ROI}}}},
        [R_STRB] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {STRB, {R32, IND | ROI}}}},
        [R_STRH] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {STRH, {R32, IND | ROI}}}},
        [R_STR] = {2, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {STR, {R32 | R64, IND | ROI}},
                              &(arm64_opr_flags) {F_STR, {F32 | F64, IND | ROI}},
                      }},
        [R_LDP] = {4, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {LDP, {R32, R32, IND}},
                              &(arm64_opr_flags) {LDP, {R64, R64, IND}},
                              &(arm64_opr_flags) {F_LDP, {F32, F32, IND}},
                              &(arm64_opr_flags) {F_LDP, {F64, F64, IND}},
                      }},
        [R_STP] = {4, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {STP, {R32, R32, IND}},
                              &(arm64_opr_flags) {STP, {R64, R64, IND}},
                              &(arm64_opr_flags) {F_STP, {F32, F32, IND}},
                              &(arm64_opr_flags) {F_STP, {F64, F64, IND}},
                      }},
        [R_ADRP] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {ADRP, {R64, SYM}}}},
        [R_CSET] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {CSET, {R32 | R64, CND}}}},
        [R_B] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {B, {SYM | IMM}}}},
        [R_BL] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BL, {SYM | IMM}}}},
        [R_BR] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BR, {R64}}}},
        [R_BEQ] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BEQ, {SYM | IMM}}}},
        [R_BNE] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BNE, {SYM | IMM}}}},
        [R_BHS] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BHS, {SYM | IMM}}}},
        [R_BLO] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BLO, {SYM | IMM}}}},
        [R_BMI] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BMI, {SYM | IMM}}}},
        [R_BPL] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BPL, {SYM | IMM}}}},
        [R_BVS] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BVS, {SYM | IMM}}}},
        [R_BVC] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BVC, {SYM | IMM}}}},
        [R_BHI] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BHI, {SYM | IMM}}}},
        [R_BLS] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BLS, {SYM | IMM}}}},
        [R_BGE] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BGE, {SYM | IMM}}}},
        [R_BLT] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BLT, {SYM | IMM}}}},
        [R_BGT] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BGT, {SYM | IMM}}}},
        [R_BLE] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BLE, {SYM | IMM}}}},
        [R_BAL] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BAL, {SYM | IMM}}}},
        [R_BNV] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BNV, {SYM | IMM}}}},
        [R_BLR] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {BLR, {R64}}}},
        [R_RET] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {RET}}},
        [R_SVC] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SVC, {IMM}}}},
        [R_MRS] = {1, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {MRS, {R64, IMM}},
                      }}, // MRS 指令需要一个64位寄存器和一个立即数(系统寄存器编号)
        [R_FMOV] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FMOV, {F32, F32 | R32}},
                               &(arm64_opr_flags) {FMOV, {F64, F64 | R64}},
                       }},
        [R_FADD] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FADD, {F32, F32, F32}},
                               &(arm64_opr_flags) {FADD, {F64, F64, F64}},
                       }},
        [R_FSUB] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FSUB, {F32, F32, F32}},
                               &(arm64_opr_flags) {FSUB, {F64, F64, F64}},
                       }},
        [R_FMUL] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FMUL, {F32, F32, F32}},
                               &(arm64_opr_flags) {FMUL, {F64, F64, F64}},
                       }},
        [R_FDIV] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FDIV, {F32, F32, F32}},
                               &(arm64_opr_flags) {FDIV, {F64, F64, F64}},
                       }},
        [R_FCMP] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FCMP, {F32, F32}},
                               &(arm64_opr_flags) {FCMP, {F64, F64}},
                       }},
        [R_FNEG] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FNEG, {F32, F32}},
                               &(arm64_opr_flags) {FNEG, {F64, F64}},
                       }},
        [R_FSQRT] = {2, (arm64_opr_flags *[]) {
                                &(arm64_opr_flags) {FSQRT, {F32, F32}},
                                &(arm64_opr_flags) {FSQRT, {F64, F64}},
                        }},
        [R_SCVTF] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {SCVTF, {F32 | F64, R32 | R64}}}},
        [R_UCVTF] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {UCVTF, {F32 | F64, R32 | R64}}}},
        [R_FCVT] = {2, (arm64_opr_flags *[]) {
                               &(arm64_opr_flags) {FCVT, {F64, F32}},
                               &(arm64_opr_flags) {FCVT, {F32, F64}},
                       }},
        [R_FCVTZS] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {FCVTZS, {R32 | R64, F32 | F64}}}},
        [R_FCVTZU] = {1, (arm64_opr_flags *[]) {&(arm64_opr_flags) {FCVTZU, {R32 | R64, F32 | F64}}}},
        [R_MVN] = {2, (arm64_opr_flags *[]) {
                              &(arm64_opr_flags) {MVN, {R32, R32}}, // 32位寄存器
                              &(arm64_opr_flags) {MVN, {R64, R64}}, // 64位寄存器
                      }},
};

static bool match_operand_flags(arm64_asm_operand_t *operand, int flag) {
    switch (operand->type) {
        case ARM64_ASM_OPERAND_REG: {
            reg_t reg = operand->reg;
            uint8_t size = reg.size; // 1, 2, 4, or 8

            switch (size) {
                case DWORD: // 32-bit register
                    return flag & R32;
                case QWORD: // 64-bit register
                    return flag & R64;
                default:
                    assertf(false, "unknown reg width %d", size);
                    return false; // 不支持的寄存器大小
            }
        }
        case ARM64_ASM_OPERAND_FREG: {
            reg_t reg = operand->reg;
            uint8_t size = reg.size;

            switch (size) {
                case DWORD: // 32-bit floating point register
                    return flag & F32;
                case QWORD: // 64-bit floating point register
                    return flag & F64;
                default:
                    assertf(false, "unknown reg width %d", size);
                    return false; // 不支持的浮点寄存器大小
            }
        }
        case ARM64_ASM_OPERAND_SYMBOL: {
            return flag & SYM;
        }
        case ARM64_ASM_OPERAND_IMMEDIATE: {
            return flag & IMM;
        }
        case ARM64_ASM_OPERAND_INDIRECT: {
            return flag & IND;
        }
        case ARM64_ASM_OPERAND_REG_OFFSET: {
            return flag & ROI;
        }
        case ARM64_ASM_OPERAND_COND: {
            return flag & CND;
        }
        case ARM64_ASM_OPERAND_SHIFT: {
            return flag & SFT;
        }
        case ARM64_ASM_OPERAND_EXTEND: {
            return flag & EXT;
        }
        default:
            return false;
    }
}

void arm64_match_opcode(arm64_asm_inst_t *inst) {
    assert(inst->raw_opcode);
    arm64_opr_flags_list *flags_list = &arm64_opcode_map[inst->raw_opcode];

    for (int i = 0; i < flags_list->count; i++) {
        arm64_opr_flags *flags = flags_list->list[i];
        bool match = true;

        // 检查操作数数量是否匹配
        for (int j = 0; j < FLAG_MAX_COUNT; j++) {
            if (flags->flags[j] == 0) {
                // == 0 表示 j 此时等于 flags 的长度
                if (j != inst->count) {
                    match = false;
                }

                break;
            }
        }

        if (!match) {
            continue;
        }

        // 检查每个操作数是否匹配, 不匹配则跳出当前循环，匹配则添加 op
        for (int j = 0; j < inst->count; j++) {
            if (!match_operand_flags(inst->operands[j], flags->flags[j])) {
                match = false;
                break;
            }
        }

        if (match) {
            inst->opcode = flags->op;
            return;
        }
    }
}

typedef uint32_t (*arm64_opcode_handle_fn)(arm64_asm_inst_t *inst);

static arm64_opcode_handle_fn arm64_opcode_handle_table[] = {
        [NOOP] = asm_noop,
        [MOV] = asm_mov,
        [MOVK] = asm_movk,
        [ADD_R] = asm_3r,
        [ADD_I] = asm_2ri,
        [SUB_R] = asm_3r,
        [SUB_I] = asm_2ri,
        [MUL] = asm_3r,
        [SDIV] = asm_3r,
        [UDIV] = asm_3r,
        [MADD] = asm_4r,
        [MSUB] = asm_4r,
        [AND] = asm_3r,
        [ORR] = asm_3r,
        [EOR] = asm_3r,
        [EON] = asm_3r,
        [CMP_R] = asm_2r,
        [CMP_I] = asm_ri,
        [CMN_R] = asm_2r,
        [CMN_I] = asm_ri,
        [LSL_R] = asm_3r,
        [LSL_I] = asm_2ri,
        [LSR_R] = asm_3r,
        [LSR_I] = asm_2ri,
        [ASR_R] = asm_3r,
        [ASR_I] = asm_2ri,
        [SXTB] = asm_2r,
        [SXTH] = asm_2r,
        [SXTW] = asm_2r,
        [UXTB] = asm_2r,
        [UXTH] = asm_2r,
        [UXTW] = asm_2r,
        [LDRB] = asm_ldrstr,
        [LDRSB] = asm_ldrstr,
        [LDR] = asm_ldrstr,
        [LDRH] = asm_ldrstr,
        [LDRSH] = asm_ldrstr,
        [LDRSW] = asm_ldrstr,
        [STRB] = asm_ldrstr,
        [STRH] = asm_ldrstr,
        [STR] = asm_ldrstr,
        [LDP] = asm_ldpstp,
        [STP] = asm_ldpstp,
        [ADRP] = asm_adrp,
        [CSET] = asm_cset,
        [B] = asm_b,
        [BR] = asm_br,
        [BEQ] = asm_bcc,
        [BNE] = asm_bcc,
        [BHS] = asm_bcc,
        [BLO] = asm_bcc,
        [BMI] = asm_bcc,
        [BPL] = asm_bcc,
        [BVS] = asm_bcc,
        [BVC] = asm_bcc,
        [BHI] = asm_bcc,
        [BLS] = asm_bcc,
        [BGE] = asm_bcc,
        [BLT] = asm_bcc,
        [BGT] = asm_bcc,
        [BLE] = asm_bcc,
        [BAL] = asm_bcc,
        [BNV] = asm_bcc,
        [BL] = asm_bl,
        [BLR] = asm_blr,
        [RET] = asm_ret,
        [MRS] = asm_mrs,
        [SVC] = asm_svc,

        [F_LDR] = asm_f_ldrstr,
        [F_STR] = asm_f_ldrstr,
        [F_LDP] = asm_f_ldpstp,
        [F_STP] = asm_f_ldpstp,
        [FMOV] = asm_f_2r,
        [FADD] = asm_f_3r,
        [FSUB] = asm_f_3r,
        [FMUL] = asm_f_3r,
        [FDIV] = asm_f_3r,
        [FCMP] = asm_f_2r,
        [FNEG] = asm_f_2r,
        [FSQRT] = asm_f_2r,
        [SCVTF] = asm_f_2r,
        [UCVTF] = asm_f_2r,
        [FCVT] = asm_f_2r,
        [FCVTZS] = asm_f_2r,
        [FCVTZU] = asm_f_2r,
        [MVN] = asm_mvn,
};

uint32_t arm64_asm_inst_encoding(arm64_asm_inst_t *inst, uint8_t *data_count) {
    // opcode 确认
    arm64_match_opcode(inst);
    assert(inst->opcode > 0);
    arm64_opcode_handle_fn fn = arm64_opcode_handle_table[inst->opcode];
    assert(fn);

    uint32_t result = fn(inst);
    assert(result > 0);

    *data_count = 4; // arm64 固定占用 4 字节
    return result;
}
