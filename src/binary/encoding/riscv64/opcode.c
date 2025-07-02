#include "opcode.h"

#include "asm.h"
#include "src/debug/debug_asm.h"
#include "src/register/arch/riscv64.h"


// raw_opcode to opcode
riscv64_opr_flags_list riscv64_opcode_map[] = {
        [RV_MV] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_MV, {RISCV64_R64, RISCV64_R64}}}},
        [RV_LI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LI, {RISCV64_R64, RISCV64_IMM}}}},
        [RV_LA] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LA, {RISCV64_R64, RISCV64_SYM}}}},
        [RV_ADD] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ADD, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_ADDW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ADDW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_ADDI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ADDI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_ADDIW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ADDIW, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SUB] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SUB, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SUBW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SUBW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_MUL] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_MUL, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_MULW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_MULW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_DIV] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_DIV, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_DIVW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_DIVW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_DIVU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_DIVU, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_DIVUW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_DIVUW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_REM] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_REM, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_REMW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_REMW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_REMU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_REMU, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_REMUW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_REMUW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_AND] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_AND, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_ANDI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ANDI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_OR] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_OR, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_ORI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ORI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_XOR] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_XOR, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_XORI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_XORI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_NEG] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_NEG, {RISCV64_R64, RISCV64_R64}}}},
        [RV_NOT] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_NOT, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SEXT_B] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SEXT_B, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SEXT_H] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SEXT_H, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SEXT_W] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SEXT_W, {RISCV64_R64, RISCV64_R64}}}},
        [RV_ZEXT_B] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ZEXT_B, {RISCV64_R64, RISCV64_R64}}}},
        [RV_ZEXT_H] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ZEXT_H, {RISCV64_R64, RISCV64_R64}}}},
        [RV_ZEXT_W] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ZEXT_W, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SLL] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLL, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SLLW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLLW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SLLI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLLI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SLLIW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLLIW, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SRL] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRL, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SRLW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRLW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SRLI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRLI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SRLIW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRLIW, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SRA] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRA, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SRAW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRAW, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SRAI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRAI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SRAIW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SRAIW, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_LB] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LB, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LH] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LH, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LW, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LD] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LD, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LBU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LBU, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LHU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LHU, {RISCV64_R64, RISCV64_IND}}}},
        [RV_LWU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_LWU, {RISCV64_R64, RISCV64_IND}}}},
        [RV_SB] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SB, {RISCV64_R64, RISCV64_IND}}}},
        [RV_SH] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SH, {RISCV64_R64, RISCV64_IND}}}},
        [RV_SW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SW, {RISCV64_R64, RISCV64_IND}}}},
        [RV_SD] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SD, {RISCV64_R64, RISCV64_IND}}}},
        [RV_SLT] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLT, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SLTI] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLTI, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SLTU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLTU, {RISCV64_R64, RISCV64_R64, RISCV64_R64}}}},
        [RV_SLTIU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLTIU, {RISCV64_R64, RISCV64_R64, RISCV64_IMM}}}},
        [RV_SEQZ] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SEQZ, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SNEZ] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SNEZ, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SLTZ] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SLTZ, {RISCV64_R64, RISCV64_R64}}}},
        [RV_SGTZ] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_SGTZ, {RISCV64_R64, RISCV64_R64}}}},
        [RV_J] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_J, {RISCV64_SYM | RISCV64_IMM}}}},
        [RV_JR] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_JR, {RISCV64_R64}}}},
        [RV_JALR] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_JALR, {RISCV64_R64}}}},
        [RV_BEQ] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BEQ, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_BNE] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BNE, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_BLT] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BLT, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_BGE] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BGE, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_BLTU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BLTU, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_BGEU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_BGEU, {RISCV64_R64, RISCV64_R64, RISCV64_SYM | RISCV64_IMM}}}},
        [RV_CALL] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_CALL, {RISCV64_SYM}}}},
        [RV_RET] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_RET}}},
        [RV_ECALL] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_ECALL}}},

        [RV_FADD_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FADD_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FADD_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FADD_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSUB_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSUB_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSUB_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSUB_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FMUL_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMUL_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FMUL_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMUL_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FDIV_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FDIV_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FDIV_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FDIV_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSQRT_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSQRT_D, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FSQRT_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSQRT_S, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJ_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJ_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJ_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJ_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJN_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJN_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJN_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJN_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJX_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJX_D, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FSGNJX_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSGNJX_S, {RISCV64_F64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FMV_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMV_D, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FMV_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMV_S, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FNEG_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FNEG_D, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FNEG_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FNEG_S, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FMV_X_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMV_X_D, {RISCV64_R64, RISCV64_F64}}}},
        [RV_FMV_X_W] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FMV_X_W, {RISCV64_R64, RISCV64_F64}}}},
        [RV_FEQ_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FEQ_D, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FEQ_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FEQ_S, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FLT_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLT_D, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FLT_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLT_S, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FLE_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLE_D, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FLE_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLE_S, {RISCV64_R64, RISCV64_F64, RISCV64_F64}}}},
        [RV_FLD] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLD, {RISCV64_F64, RISCV64_IND}}}},
        [RV_FLW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FLW, {RISCV64_F64, RISCV64_IND}}}},
        [RV_FSD] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSD, {RISCV64_F64, RISCV64_IND}}}},
        [RV_FSW] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FSW, {RISCV64_F64, RISCV64_IND}}}},
        [RV_FCVT_D_W] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_D_W, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_D_WU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_D_WU, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_D_L] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_D_L, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_D_LU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_D_LU, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_W_D] = {2, (riscv64_opr_flags *[]) {
                                    &(riscv64_opr_flags) {O_FCVT_W_D, {RISCV64_R64, RISCV64_F64}},
                                    &(riscv64_opr_flags) {O_FCVT_W_D, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                            }},
        [RV_FCVT_WU_D] = {2, (riscv64_opr_flags *[]) {
                                     &(riscv64_opr_flags) {O_FCVT_WU_D, {RISCV64_R64, RISCV64_F64}},
                                     &(riscv64_opr_flags) {O_FCVT_WU_D, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                             }},
        [RV_FCVT_L_D] = {2, (riscv64_opr_flags *[]) {
                                    &(riscv64_opr_flags) {O_FCVT_L_D, {RISCV64_R64, RISCV64_F64}},
                                    &(riscv64_opr_flags) {O_FCVT_L_D, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                            }},
        [RV_FCVT_LU_D] = {2, (riscv64_opr_flags *[]) {
                                     &(riscv64_opr_flags) {O_FCVT_LU_D, {RISCV64_R64, RISCV64_F64}},
                                     &(riscv64_opr_flags) {O_FCVT_LU_D, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                             }},
        [RV_FCVT_S_W] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_S_W, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_S_WU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_S_WU, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_S_L] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_S_L, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_S_LU] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_S_LU, {RISCV64_F64, RISCV64_R64}}}},
        [RV_FCVT_W_S] = {2, (riscv64_opr_flags *[]) {
                                    &(riscv64_opr_flags) {O_FCVT_W_S, {RISCV64_R64, RISCV64_F64}},
                                    &(riscv64_opr_flags) {O_FCVT_W_S, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                            }},
        [RV_FCVT_WU_S] = {2, (riscv64_opr_flags *[]) {
                                     &(riscv64_opr_flags) {O_FCVT_WU_S, {RISCV64_R64, RISCV64_F64}},
                                     &(riscv64_opr_flags) {O_FCVT_WU_S, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                             }},
        [RV_FCVT_L_S] = {2, (riscv64_opr_flags *[]) {
                                    &(riscv64_opr_flags) {O_FCVT_L_S, {RISCV64_R64, RISCV64_F64}},
                                    &(riscv64_opr_flags) {O_FCVT_L_S, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                            }},
        [RV_FCVT_LU_S] = {2, (riscv64_opr_flags *[]) {
                                     &(riscv64_opr_flags) {O_FCVT_LU_S, {RISCV64_R64, RISCV64_F64}},
                                     &(riscv64_opr_flags) {O_FCVT_LU_S, {RISCV64_R64, RISCV64_F64, RISCV64_RND}},
                             }},
        [RV_FCVT_D_S] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_D_S, {RISCV64_F64, RISCV64_F64}}}},
        [RV_FCVT_S_D] = {1, (riscv64_opr_flags *[]) {&(riscv64_opr_flags) {O_FCVT_S_D, {RISCV64_F64, RISCV64_F64}}}},
};


static unsigned char *asm_mv(riscv64_asm_inst_t *inst);

static inline bool is_im6(int64_t x) {
    return x <= ((1L << 5) - 1) && x >= -(1L << 5);
}

static inline bool is_uim6(uint64_t x) {
    return x <= ((1UL << 6) - 1);
}

static inline bool is_im12(int64_t x) {
    return x <= ((1L << 11) - 1) && x >= -(1L << 11);
}

static inline bool is_im8(int64_t x) {
    return x <= ((1L << 7) - 1) && x >= -(1L << 7);
}

static inline bool is_im16(int64_t x) {
    return x <= ((1L << 15) - 1) && x >= -(1L << 15);
}

static inline bool is_im32(int64_t x) {
    return x <= ((1L << 31) - 1) && x >= -(1L << 31);
}

static unsigned char *asm_3r(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[2]->type == RISCV64_ASM_OPERAND_REG);
    int rd = inst->operands[0]->reg.index;
    int rs1 = inst->operands[1]->reg.index;
    int rs2 = inst->operands[2]->reg.index;

    if (rd == rs1) {
        if (is_rvc_reg(rd) && is_rvc_reg(rs2)) {
            switch (inst->opcode) {
                case O_ADD:
                    C_ADD(rd, rs2);
                    return inst->opcode_data;
                case O_ADDW:
                    C_ADDW(rd, rs2);
                    return inst->opcode_data;
                case O_SUB:
                    C_SUB(rd, rs2);
                    return inst->opcode_data;
                case O_SUBW:
                    C_SUBW(rd, rs2);
                    return inst->opcode_data;
                case O_AND:
                    C_AND(rd, rs2);
                    return inst->opcode_data;
                case O_OR:
                    C_OR(rd, rs2);
                    return inst->opcode_data;
                case O_XOR:
                    C_XOR(rd, rs2);
                    return inst->opcode_data;
                default:
                    break;
            }
        }
    } else if (rd == rs2) {
        if (is_rvc_reg(rd) && is_rvc_reg(rs1)) {
            switch (inst->opcode) {
                case O_ADD:
                    C_ADD(rd, rs1);
                    return inst->opcode_data;
                case O_ADDW:
                    C_ADDW(rd, rs1);
                    return inst->opcode_data;
                case O_AND:
                    C_AND(rd, rs1);
                    return inst->opcode_data;
                case O_OR:
                    C_OR(rd, rs1);
                    return inst->opcode_data;
                case O_XOR:
                    C_XOR(rd, rs1);
                    return inst->opcode_data;
                default:
                    break;
            }
        }
    }

    switch (inst->opcode) {
        case O_ADD:
            if (rd == rs1)
                C_ADD(rd, rs2);
            else if (rd == rs2)
                C_ADD(rd, rs1);
            else
                W_ADD(rd, rs1, rs2);
            break;
        case O_ADDW:
            W_ADDW(rd, rs1, rs2);
            break;
        case O_SUB:
            W_SUB(rd, rs1, rs2);
            break;
        case O_SUBW:
            W_SUBW(rd, rs1, rs2);
            break;
        case O_MUL:
            W_MUL(rd, rs1, rs2);
            break;
        case O_MULW:
            W_MULW(rd, rs1, rs2);
            break;
        case O_DIV:
            W_DIV(rd, rs1, rs2);
            break;
        case O_DIVW:
            W_DIVW(rd, rs1, rs2);
            break;
        case O_DIVU:
            W_DIVU(rd, rs1, rs2);
            break;
        case O_DIVUW:
            W_DIVUW(rd, rs1, rs2);
            break;
        case O_REM:
            W_REM(rd, rs1, rs2);
            break;
        case O_REMW:
            W_REMW(rd, rs1, rs2);
            break;
        case O_REMU:
            W_REMU(rd, rs1, rs2);
            break;
        case O_REMUW:
            W_REMUW(rd, rs1, rs2);
            break;
        case O_AND:
            W_AND(rd, rs1, rs2);
            break;
        case O_OR:
            W_OR(rd, rs1, rs2);
            break;
        case O_XOR:
            W_XOR(rd, rs1, rs2);
            break;
        case O_SLL:
            W_SLL(rd, rs1, rs2);
            break;
        case O_SLLW:
            W_SLLW(rd, rs1, rs2);
            break;
        case O_SRL:
            W_SRL(rd, rs1, rs2);
            break;
        case O_SRLW:
            W_SRLW(rd, rs1, rs2);
            break;
        case O_SRA:
            W_SRA(rd, rs1, rs2);
            break;
        case O_SRAW:
            W_SRAW(rd, rs1, rs2);
            break;
        case O_SLT:
            W_SLT(rd, rs1, rs2);
            break;
        case O_SLTU:
            W_SLTU(rd, rs1, rs2);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_2ri(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[2]->type == RISCV64_ASM_OPERAND_IMMEDIATE);
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;
    int64_t imm = inst->operands[2]->immediate;

    if (inst->opcode == O_ADDI && imm == 0) {
        if (rd != rs)
            return asm_mv(inst);
        return inst->opcode_data;
    }

    if (rd == rs) {
        if (is_im6(imm)) {
            switch (inst->opcode) {
                case O_ADDI:
                    if (imm != 0) {
                        C_ADDI(rd, imm);
                        return inst->opcode_data;
                    }
                    break;
                case O_ADDIW:
                    C_ADDIW(rd, imm);
                    return inst->opcode_data;
                case O_ANDI:
                    if (is_rvc_reg(rd)) {
                        C_ANDI(rd, imm);
                        return inst->opcode_data;
                    }
                    break;
                default:
                    break;
            }
        }
        if (is_uim6(imm)) {
            switch (inst->opcode) {
                case O_SLLI:
                    if (rd != 0 && imm != 0) {
                        C_SLLI(rd, imm);
                        return inst->opcode_data;
                    }
                    break;
                case O_SRLI:
                    if (is_rvc_reg(rd)) {
                        C_SRLI(rd, imm);
                        return inst->opcode_data;
                    }
                    break;
                case O_SRAI:
                    if (is_rvc_reg(rd)) {
                        C_SRAI(rd, imm);
                        return inst->opcode_data;
                    }
                    break;
                default:
                    break;
            }
        }

        if (inst->opcode == O_ADDI && rd == R_SP->index && is_im6(imm >> 4) && (imm & 0xf) == 0 && imm != 0) {
            C_ADDI16SP(imm);
            return inst->opcode_data;
        }
    }
    switch (inst->opcode) {
        case O_ADDI:
        case O_ADDIW:
        case O_ANDI:
        case O_ORI:
        case O_XORI:
        case O_SLTI:
        case O_SLTIU:
            if (imm >= 2048 || imm < -2048)
                return NULL;
            switch (inst->opcode) {
                case O_ADDI:
                    W_ADDI(rd, rs, imm);
                    break;
                case O_ADDIW:
                    W_ADDIW(rd, rs, imm);
                    break;
                case O_ANDI:
                    W_ANDI(rd, rs, imm);
                    break;
                case O_ORI:
                    W_ORI(rd, rs, imm);
                    break;
                case O_XORI:
                    W_XORI(rd, rs, imm);
                    break;
                case O_SLTI:
                    W_SLTI(rd, rs, imm);
                    break;
                case O_SLTIU:
                    W_SLTIU(rd, rs, imm);
                    break;
                default:
                    assert(false);
                    break;
            }
            break;

        case O_SLLI:
        case O_SLLIW:
        case O_SRLI:
        case O_SRLIW:
        case O_SRAI:
        case O_SRAIW:
            if (imm >= 64 || (inst->opcode == O_SLLIW && imm >= 32) || imm < 0)
                return NULL;
            switch (inst->opcode) {
                case O_SLLI:
                    W_SLLI(rd, rs, imm);
                    break;
                case O_SLLIW:
                    W_SLLIW(rd, rs, imm);
                    break;
                case O_SRLI:
                    W_SRLI(rd, rs, imm);
                    break;
                case O_SRLIW:
                    W_SRLIW(rd, rs, imm);
                    break;
                case O_SRAI:
                    W_SRAI(rd, rs, imm);
                    break;
                case O_SRAIW:
                    W_SRAIW(rd, rs, imm);
                    break;
                default:
                    assert(false);
                    break;
            }
            break;

        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_2r(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_REG);
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;
    switch (inst->opcode) {
        case O_NEG:
            RP_NEG(rd, rs);
            break;
        case O_NOT:
            P_NOT(rd, rs);
            break;
        case O_SEXT_B:
            P_SEXT_B(rd, rs);
            break;
        case O_SEXT_H:
            P_SEXT_H(rd, rs);
            break;
        case O_SEXT_W:
            P_SEXT_W(rd, rs);
            break;
        case O_ZEXT_B:
            P_ZEXT_B(rd, rs);
            break;
        case O_ZEXT_H:
            P_ZEXT_H(rd, rs);
            break;
        case O_ZEXT_W:
            P_ZEXT_W(rd, rs);
            break;
        case O_SEQZ:
            P_SEQZ(rd, rs);
            break;
        case O_SNEZ:
            P_SNEZ(rd, rs);
            break;
        case O_SLTZ:
            P_SLTZ(rd, rs);
            break;
        case O_SGTZ:
            P_SGTZ(rd, rs);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_noop(riscv64_asm_inst_t *inst) {
    unsigned char *p = inst->opcode_data;
    return p;
}

static unsigned char *asm_mv(riscv64_asm_inst_t *inst) {
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;
    if (rs != OPCODE_ZERO)
        C_MV(rd, rs);
    else
        C_LI(rd, 0);
    return inst->opcode_data;
}

static void li_sub(riscv64_asm_inst_t *inst, int64_t imm) {
    int rd = inst->operands[0]->reg.index;
    if (is_im6(imm)) {
        C_LI(rd, imm);
    } else if (is_im12(imm)) {
        P_LI(rd, imm);
    } else if (is_im32(imm)) {
        int l = imm & 0xfff;
        if (l >= 0x800) {
            l = l - 0x1000;
            imm += 1 << 12;
        }
        if (is_im6(imm >> 12))
            C_LUI(rd, imm);
        else
            W_LUI(rd, imm);
        if (is_im6(l))
            C_ADDIW(rd, l);
        else
            W_ADDIW(rd, rd, l);
    } else {
        int32_t l = (int32_t) imm & ((1 << 12) - 1);
        imm >>= 12;
        if (l >= (1 << 11)) {
            l = l - (1 << 12);
            ++imm;
        }
        li_sub(inst, imm);
        C_SLLI(rd, 12);
        if (is_im6(l))
            C_ADDI(rd, l);
        else
            W_ADDI(rd, rd, l);
    }
}

static unsigned char *asm_li(riscv64_asm_inst_t *inst) {
    li_sub(inst, inst->operands[1]->immediate);
    return inst->opcode_data;
}

static unsigned char *asm_la(riscv64_asm_inst_t *inst) {
    int rd = inst->operands[0]->reg.index;
    W_AUIPC(rd, 0);
    W_ADDI(rd, rd, 0);
    return inst->opcode_data;
}

static unsigned char *asm_ld(riscv64_asm_inst_t *inst) {
    int rd = inst->operands[0]->reg.index;
    int64_t ofs = inst->operands[1]->indirect.offset;
    int rs = inst->operands[1]->indirect.reg->index;
    switch (inst->opcode) {
        case O_LB:
            // TODO: Check offset range.
            W_LB(rd, ofs, rs);
            return inst->opcode_data;
        case O_LH:
            // TODO: Check offset range.
            W_LH(rd, ofs, rs);
            return inst->opcode_data;
        case O_LW:
            if (ofs >= 0 && ofs < (1 << 7) && (ofs & 3) == 0 && is_rvc_reg(rd) && is_rvc_reg(rs)) {
                C_LW(rd, ofs, rs);
                return inst->opcode_data;
            }
            // TODO: Check offset range.
            W_LW(rd, ofs, rs);
            return inst->opcode_data;
        case O_LD:
            if (ofs >= 0 && ofs < (1 << 9) && (ofs & 7) == 0 && rs == R_SP->index) {
                C_LDSP(rd, ofs);
                return inst->opcode_data;
            }
            if (ofs >= 0 && ofs < (1 << 8) && (ofs & 7) == 0 && is_rvc_reg(rd) && is_rvc_reg(rs)) {
                C_LD(rd, ofs, rs);
                return inst->opcode_data;
            }
            // TODO: Check offset range.
            W_LD(rd, ofs, rs);
            return inst->opcode_data;
        case O_LBU:
            // TODO: Check offset range.
            W_LBU(rd, ofs, rs);
            return inst->opcode_data;
        case O_LHU:
            // TODO: Check offset range.
            W_LHU(rd, ofs, rs);
            return inst->opcode_data;
        case O_LWU:
            // TODO: Check offset range.
            W_LWU(rd, ofs, rs);
            return inst->opcode_data;
        default:
            break;
    }
}

static unsigned char *asm_sd(riscv64_asm_inst_t *inst) {
    int ofs = inst->operands[1]->indirect.offset;
    int rs2 = inst->operands[0]->reg.index;
    int rs1 = inst->operands[1]->indirect.reg->index;
    switch (inst->opcode) {
        case O_SB:
            // TODO: Check offset range.
            W_SB(rs2, ofs, rs1);
            return inst->opcode_data;
        case O_SH:
            // TODO: Check offset range.
            W_SH(rs2, ofs, rs1);
            return inst->opcode_data;
        case O_SW:
            if (ofs >= 0 && ofs < (1 << 7) && (ofs & 3) == 0 && is_rvc_reg(rs1) && is_rvc_reg(rs2)) {
                C_SW(rs2, ofs, rs1);
                return inst->opcode_data;
            }
            // TODO: Check offset range.
            W_SW(rs2, ofs, rs1);
            return inst->opcode_data;
        case O_SD:
            if (ofs >= 0 && ofs < (1 << 9) && (ofs & 7) == 0 && rs1 == R_SP->index) {
                C_SDSP(rs2, ofs);
                return inst->opcode_data;
            }
            if (ofs >= 0 && ofs < (1 << 8) && (ofs & 7) == 0 && is_rvc_reg(rs1) && is_rvc_reg(rs2)) {
                C_SD(rs2, ofs, rs1);
                return inst->opcode_data;
            }
            // TODO: Check offset range.
            W_SD(rs2, ofs, rs1);
            return inst->opcode_data;
        default:
            break;
    }
}

static unsigned char *asm_j(riscv64_asm_inst_t *inst) {
    // imm[11|4|9:8|10|6|7|3:1|5]
    int64_t imm = 0;
    if (inst->operands[0]->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        imm = inst->operands[0]->immediate;
    }

    C_J(imm);
    return inst->opcode_data;
}

static unsigned char *asm_jr(riscv64_asm_inst_t *inst) {
    int rs = inst->operands[0]->reg.index;
    C_JR(rs);
    return inst->opcode_data;
}

static unsigned char *asm_jalr(riscv64_asm_inst_t *inst) {
    int rs = inst->operands[0]->reg.index;
    C_JALR(rs);
    return inst->opcode_data;
}

#define _BEQ 0x0
#define _BNE 0x1
#define _BLT 0x4
#define _BGE 0x5
#define _BLTU 0x6
#define _BGEU 0x7

unsigned char *asm_bxx(riscv64_asm_inst_t *inst) {
    int rs1 = inst->operands[0]->reg.index;
    int rs2 = inst->operands[1]->reg.index;
    if (rs2 == OPCODE_ZERO && is_rvc_reg(rs1)) {
        switch (inst->opcode) {
            case O_BEQ:
                C_BEQZ(rs1);
                return inst->opcode_data;
            case O_BNE:
                C_BNEZ(rs1);
                return inst->opcode_data;
            default:
                break;
        }
    }

    riscv64_asm_operand_t *opr3 = inst->operands[2];
    int64_t offset = 0;
    if (opr3->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        offset = opr3->immediate;
    } else if (opr3->type == RISCV64_ASM_OPERAND_SYMBOL) {
        offset = opr3->symbol.offset;
    } else {
        assert(false); // 不支持的操作数类型
        return 0;
    }

    static const int k_funct3_table[] = {_BEQ, _BNE, _BLT, _BGE, _BLTU, _BGEU};
    int funct3 = k_funct3_table[inst->opcode - O_BEQ];
    W_BXX(funct3, rs1, rs2, offset);
    return inst->opcode_data;
}

static unsigned char *asm_call_d(riscv64_asm_inst_t *inst) {
    riscv64_asm_operand_t *opr1 = inst->operands[0];
    int64_t offset;

    if (opr1->type == RISCV64_ASM_OPERAND_SYMBOL) {
        offset = opr1->symbol.offset;
    } else if (opr1->type == RISCV64_ASM_OPERAND_IMMEDIATE) {
        offset = opr1->immediate;
    } else {
        assert(false);
        return 0;
    }

    W_AUIPC(OPCODE_RA, offset);
    W_JALR(OPCODE_RA, OPCODE_RA, offset);
    return inst->opcode_data;
}

static unsigned char *asm_ret(riscv64_asm_inst_t *inst) {
    P_RET();
    return inst->opcode_data;
}

static unsigned char *asm_ecall(riscv64_asm_inst_t *inst) {
    W_ECALL();
    return inst->opcode_data;
}

static unsigned char *asm_3fr(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_FREG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_FREG);
    assert(inst->operands[2]->type == RISCV64_ASM_OPERAND_FREG);
    int rd = inst->operands[0]->reg.index;
    int rs1 = inst->operands[1]->reg.index;
    int rs2 = inst->operands[2]->reg.index;

    switch (inst->opcode) {
        case O_FADD_D:
            W_FADD_D(rd, rs1, rs2);
            break;
        case O_FSUB_D:
            W_FSUB_D(rd, rs1, rs2);
            break;
        case O_FMUL_D:
            W_FMUL_D(rd, rs1, rs2);
            break;
        case O_FDIV_D:
            W_FDIV_D(rd, rs1, rs2);
            break;
        case O_FADD_S:
            W_FADD_S(rd, rs1, rs2);
            break;
        case O_FSUB_S:
            W_FSUB_S(rd, rs1, rs2);
            break;
        case O_FMUL_S:
            W_FMUL_S(rd, rs1, rs2);
            break;
        case O_FDIV_S:
            W_FDIV_S(rd, rs1, rs2);
            break;
        case O_FSGNJ_D:
            W_FSGNJ_D(rd, rs1, rs2);
            break;
        case O_FSGNJN_D:
            W_FSGNJN_D(rd, rs1, rs2);
            break;
        case O_FSGNJX_D:
            W_FSGNJX_D(rd, rs1, rs2);
            break;
        case O_FSGNJ_S:
            W_FSGNJ_S(rd, rs1, rs2);
            break;
        case O_FSGNJN_S:
            W_FSGNJN_S(rd, rs1, rs2);
            break;
        case O_FSGNJX_S:
            W_FSGNJX_S(rd, rs1, rs2);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_2fr(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_FREG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_FREG);
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;

    switch (inst->opcode) {
        case O_FSQRT_D:
            W_FSQRT_D(rd, rs);
            break;
        case O_FSQRT_S:
            W_FSQRT_S(rd, rs);
            break;
        case O_FMV_D:
            P_FMV_D(rd, rs);
            break;
        case O_FNEG_D:
            P_FNEG_D(rd, rs);
            break;
        case O_FMV_S:
            P_FMV_S(rd, rs);
            break;
        case O_FNEG_S:
            P_FNEG_S(rd, rs);
            break;
        case O_FCVT_D_S:
            W_FCVT_D_S(rd, rs);
            break;
        case O_FCVT_S_D:
            W_FCVT_S_D(rd, rs);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_fcmp(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_FREG);
    assert(inst->operands[2]->type == RISCV64_ASM_OPERAND_FREG);
    int rd = inst->operands[0]->reg.index;
    int rs1 = inst->operands[1]->reg.index;
    int rs2 = inst->operands[2]->reg.index;
    switch (inst->opcode) {
        case O_FEQ_D:
            W_FEQ_D(rd, rs1, rs2);
            break;
        case O_FLT_D:
            W_FLT_D(rd, rs1, rs2);
            break;
        case O_FLE_D:
            W_FLE_D(rd, rs1, rs2);
            break;
        case O_FEQ_S:
            W_FEQ_S(rd, rs1, rs2);
            break;
        case O_FLT_S:
            W_FLT_S(rd, rs1, rs2);
            break;
        case O_FLE_S:
            W_FLE_S(rd, rs1, rs2);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_fld(riscv64_asm_inst_t *inst) {
    int rd = inst->operands[0]->reg.index;
    int64_t ofs = inst->operands[1]->indirect.offset;
    int rs = inst->operands[1]->indirect.reg->index;
    if (inst->opcode == O_FLD) {
        if (ofs >= 0 && ofs < (1 << 9) && (ofs & 7) == 0 && rs == R_SP->index) {
            C_FLDSP(rd, ofs);
            return inst->opcode_data;
        }
        if (ofs >= 0 && ofs < (1 << 8) && (ofs & 7) == 0 && is_rvc_freg(rd) && is_rvc_freg(rs)) {
            C_FLD(rd, ofs, rs);
            return inst->opcode_data;
        }
    }
    // TODO: Check offset range.
    switch (inst->opcode) {
        case O_FLD:
            W_FLD(rd, ofs, rs);
            break;
        case O_FLW:
            W_FLW(rd, ofs, rs);
            break;
        default:
            assert(false);
            break;
    }
    return inst->opcode_data;
}

static unsigned char *asm_fsd(riscv64_asm_inst_t *inst) {
    int64_t ofs = inst->operands[1]->indirect.offset;
    int rs2 = inst->operands[0]->reg.index;
    int rs1 = inst->operands[1]->indirect.reg->index;
    if (inst->opcode == O_FSD) {
        if (ofs >= 0 && ofs < (1 << 9) && (ofs & 7) == 0 && rs1 == R_SP->index) {
            C_FSDSP(rs2, ofs);
            return inst->opcode_data;
        }
        if (ofs >= 0 && ofs < (1 << 8) && (ofs & 7) == 0 && is_rvc_freg(rs1) && is_rvc_freg(rs2)) {
            C_FSD(rs2, ofs, rs1);
            return inst->opcode_data;
        }
    }
    // TODO: Check offset range.
    switch (inst->opcode) {
        case O_FSD:
            W_FSD(rs2, ofs, rs1);
            break;
        case O_FSW:
            W_FSW(rs2, ofs, rs1);
            break;
        default:
            assert(false);
            break;
    }
    return inst->opcode_data;
}

static unsigned char *asm_fi(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_FREG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_REG);
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;
    switch (inst->opcode) {
        case O_FCVT_D_W:
            W_FCVT_D_W(rd, rs);
            break;
        case O_FCVT_D_WU:
            W_FCVT_D_WU(rd, rs);
            break;
        case O_FCVT_D_L:
            W_FCVT_D_L(rd, rs);
            break;
        case O_FCVT_D_LU:
            W_FCVT_D_LU(rd, rs);
            break;
        case O_FCVT_S_W:
            W_FCVT_S_W(rd, rs);
            break;
        case O_FCVT_S_WU:
            W_FCVT_S_WU(rd, rs);
            break;
        case O_FCVT_S_L:
            W_FCVT_S_L(rd, rs);
            break;
        case O_FCVT_S_LU:
            W_FCVT_S_LU(rd, rs);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

static unsigned char *asm_if(riscv64_asm_inst_t *inst) {
    assert(inst->operands[0]->type == RISCV64_ASM_OPERAND_REG);
    assert(inst->operands[1]->type == RISCV64_ASM_OPERAND_FREG);
    int rd = inst->operands[0]->reg.index;
    int rs = inst->operands[1]->reg.index;

    // 可选的转换 round mode
    int rm = 0b111; // 默认激活动态舍入模式
    if (inst->count > 2) {
        rm = inst->operands[2]->type == RISCV64_ASM_OPERAND_ROUNDMODE ? inst->operands[2]->round_mode : 0;
    }

    switch (inst->opcode) {
        case O_FMV_X_D:
            W_FMV_X_D(rd, rs);
            break;
        case O_FMV_X_W:
            W_FMV_X_W(rd, rs);
            break;
        case O_FCVT_W_D:
            W_FCVT_W_D(rd, rs, rm);
            break;
        case O_FCVT_WU_D:
            W_FCVT_WU_D(rd, rs, rm);
            break;
        case O_FCVT_L_D:
            W_FCVT_L_D(rd, rs, rm);
            break;
        case O_FCVT_LU_D:
            W_FCVT_LU_D(rd, rs, rm);
            break;
        case O_FCVT_W_S:
            W_FCVT_W_S(rd, rs, rm);
            break;
        case O_FCVT_WU_S:
            W_FCVT_WU_S(rd, rs, rm);
            break;
        case O_FCVT_L_S:
            W_FCVT_L_S(rd, rs, rm);
            break;
        case O_FCVT_LU_S:
            W_FCVT_LU_S(rd, rs, rm);
            break;
        default:
            assert(false);
            return NULL;
    }
    return inst->opcode_data;
}

/**
 * (riscv64_opr_flags *[]){&(riscv64_opr_flags){O_MV, {RISCV64_R64, RISCV64_R64}}
 */
static bool riscv64_match_operand_flags(riscv64_asm_operand_t *operand, int flag) {
    switch (operand->type) {
        case RISCV64_ASM_OPERAND_REG: {
            return flag & RISCV64_R64;
        }
        case RISCV64_ASM_OPERAND_FREG: {
            return flag & RISCV64_F64;
        }
        case RISCV64_ASM_OPERAND_ROUNDMODE: {
            return flag & RISCV64_RND;
        }
        case RISCV64_ASM_OPERAND_SYMBOL: {
            return flag & RISCV64_SYM;
        }
        case RISCV64_ASM_OPERAND_IMMEDIATE: {
            return flag & RISCV64_IMM;
        }
        case RISCV64_ASM_OPERAND_INDIRECT: {
            return flag & RISCV64_IND;
        }
        default:
            return false;
    }
}

void riscv64_match_opcode(riscv64_asm_inst_t *inst) {
    assert(inst->raw_opcode);
    riscv64_opr_flags_list *flags_list = &riscv64_opcode_map[inst->raw_opcode];

    for (int i = 0; i < flags_list->count; i++) {
        riscv64_opr_flags *flags = flags_list->list[i];
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
            if (!riscv64_match_operand_flags(inst->operands[j], flags->flags[j])) {
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


typedef uint8_t *(*riscv64_opcode_handle_fn)(riscv64_asm_inst_t *inst);

static riscv64_opcode_handle_fn riscv64_opcode_handle_table[] = {
        [O_NOOP] = asm_noop,
        [O_MV] = asm_mv,
        [O_LI] = asm_li,
        [O_LA] = asm_la,
        [O_ADD] = asm_3r,
        [O_ADDW] = asm_3r,
        [O_ADDI] = asm_2ri,
        [O_ADDIW] = asm_2ri,
        [O_SUB] = asm_3r,
        [O_SUBW] = asm_3r,
        [O_MUL] = asm_3r,
        [O_MULW] = asm_3r,
        [O_DIV] = asm_3r,
        [O_DIVU] = asm_3r,
        [O_DIVW] = asm_3r,
        [O_DIVUW] = asm_3r,
        [O_REM] = asm_3r,
        [O_REMU] = asm_3r,
        [O_REMW] = asm_3r,
        [O_REMUW] = asm_3r,
        [O_AND] = asm_3r,
        [O_ANDI] = asm_2ri,
        [O_OR] = asm_3r,
        [O_ORI] = asm_2ri,
        [O_XOR] = asm_3r,
        [O_XORI] = asm_2ri,
        [O_NEG] = asm_2r,
        [O_NOT] = asm_2r,
        [O_SEXT_B] = asm_2r,
        [O_SEXT_H] = asm_2r,
        [O_SEXT_W] = asm_2r,
        [O_ZEXT_B] = asm_2r,
        [O_ZEXT_H] = asm_2r,
        [O_ZEXT_W] = asm_2r,
        [O_SLL] = asm_3r,
        [O_SLLW] = asm_3r,
        [O_SLLI] = asm_2ri,
        [O_SLLIW] = asm_2ri,
        [O_SRL] = asm_3r,
        [O_SRLW] = asm_3r,
        [O_SRLI] = asm_2ri,
        [O_SRLIW] = asm_2ri,
        [O_SRA] = asm_3r,
        [O_SRAW] = asm_3r,
        [O_SRAI] = asm_2ri,
        [O_SRAIW] = asm_2ri,
        [O_LB] = asm_ld,
        [O_LH] = asm_ld,
        [O_LW] = asm_ld,
        [O_LD] = asm_ld,
        [O_LBU] = asm_ld,
        [O_LHU] = asm_ld,
        [O_LWU] = asm_ld,
        [O_SB] = asm_sd,
        [O_SH] = asm_sd,
        [O_SW] = asm_sd,
        [O_SD] = asm_sd,
        [O_SLT] = asm_3r,
        [O_SLTI] = asm_2ri,
        [O_SLTU] = asm_3r,
        [O_SLTIU] = asm_2ri,
        [O_SEQZ] = asm_2r,
        [O_SNEZ] = asm_2r,
        [O_SLTZ] = asm_2r,
        [O_SGTZ] = asm_2r,
        [O_J] = asm_j,
        [O_JR] = asm_jr,
        [O_JALR] = asm_jalr,
        [O_BEQ] = asm_bxx,
        [O_BNE] = asm_bxx,
        [O_BLT] = asm_bxx,
        [O_BGE] = asm_bxx,
        [O_BLTU] = asm_bxx,
        [O_BGEU] = asm_bxx,
        [O_CALL] = asm_call_d,
        [O_RET] = asm_ret,
        [O_ECALL] = asm_ecall,

        [O_FADD_D] = asm_3fr,
        [O_FSUB_D] = asm_3fr,
        [O_FMUL_D] = asm_3fr,
        [O_FDIV_D] = asm_3fr,
        [O_FADD_S] = asm_3fr,
        [O_FSUB_S] = asm_3fr,
        [O_FMUL_S] = asm_3fr,
        [O_FDIV_S] = asm_3fr,
        [O_FSQRT_D] = asm_2fr,
        [O_FSQRT_S] = asm_2fr,
        [O_FSGNJ_D] = asm_3fr,
        [O_FSGNJN_D] = asm_3fr,
        [O_FSGNJX_D] = asm_3fr,
        [O_FSGNJ_S] = asm_3fr,
        [O_FSGNJN_S] = asm_3fr,
        [O_FSGNJX_S] = asm_3fr,
        [O_FMV_D] = asm_2fr,
        [O_FNEG_D] = asm_2fr,
        [O_FMV_S] = asm_2fr,
        [O_FNEG_S] = asm_2fr,
        [O_FMV_X_D] = asm_if,
        [O_FMV_X_W] = asm_if,
        [O_FEQ_D] = asm_fcmp,
        [O_FLT_D] = asm_fcmp,
        [O_FLE_D] = asm_fcmp,
        [O_FEQ_S] = asm_fcmp,
        [O_FLT_S] = asm_fcmp,
        [O_FLE_S] = asm_fcmp,
        [O_FLD] = asm_fld,
        [O_FLW] = asm_fld,
        [O_FSD] = asm_fsd,
        [O_FSW] = asm_fsd,

        [O_FCVT_D_W] = asm_fi,
        [O_FCVT_D_WU] = asm_fi,
        [O_FCVT_D_L] = asm_fi,
        [O_FCVT_D_LU] = asm_fi,
        [O_FCVT_W_D] = asm_if,
        [O_FCVT_WU_D] = asm_if,
        [O_FCVT_L_D] = asm_if,
        [O_FCVT_LU_D] = asm_if,
        [O_FCVT_S_W] = asm_fi,
        [O_FCVT_S_WU] = asm_fi,
        [O_FCVT_S_L] = asm_fi,
        [O_FCVT_S_LU] = asm_fi,
        [O_FCVT_W_S] = asm_if,
        [O_FCVT_WU_S] = asm_if,
        [O_FCVT_L_S] = asm_if,
        [O_FCVT_LU_S] = asm_if,
        [O_FCVT_D_S] = asm_2fr,
        [O_FCVT_S_D] = asm_2fr,
};

uint8_t *riscv64_asm_inst_encoding(riscv64_asm_inst_t *inst, closure_t *c) {
    inst->opcode_count = 0;

    // opcode 确认
    riscv64_match_opcode(inst);
    assert(inst->opcode > 0);

    riscv64_opcode_handle_fn fn = riscv64_opcode_handle_table[inst->opcode];
    assert(fn);

    uint8_t *result = fn(inst);
    assert(result > 0);

#ifdef DEBUG_ASM
    if (c && str_equal(c->linkident, DEBUG_ASM)) {
        asm_op_to_string(0, inst);
        code_to_string(inst->opcode_data, inst->opcode_count);
    }
#endif

    return result;
}
