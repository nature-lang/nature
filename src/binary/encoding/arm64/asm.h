#ifndef NATURE_ARM64_ASM_H
#define NATURE_ARM64_ASM_H

#include "src/types.h"

typedef enum {
    R_NOOP,
    R_MOV, R_MOVK,
    R_ADD, R_SUB,
    R_MUL, R_SDIV, R_UDIV,
    R_MADD, R_MSUB,
    R_AND, R_ORR, R_EOR, R_EON,
    R_CMP, R_CMN,
    R_LSL, R_LSR, R_ASR,
    R_SXTB, R_SXTH, R_SXTW,
    R_UXTB, R_UXTH, R_UXTW,
    R_LDRB, R_LDRH, R_LDR, R_LDRSB, R_LDRSH, R_LDRSW,
    R_STRB, R_STRH, R_STR,
    R_LDP, R_STP,
    R_ADRP,
    R_CSET,
    R_B, R_BR,
    R_BEQ, R_BNE, R_BHS, R_BLO, R_BMI, R_BPL, R_BVS, R_BVC,
    R_BHI, R_BLS, R_BGE, R_BLT, R_BGT, R_BLE, R_BAL, R_BNV,
    R_BL, R_BLR,
    R_RET,
    R_SVC,

    R_FMOV,
    R_FADD, R_FSUB, R_FMUL, R_FDIV,
    R_FCMP, R_FNEG,
    R_FSQRT,
    R_SCVTF, R_UCVTF,
    R_FCVT, R_FCVTZS, R_FCVTZU,
} arm64_asm_raw_opcode_t;

typedef enum {
    NOOP = 0,
    MOV, MOVK,
    ADD_R, ADD_I, SUB_R, SUB_I,
    MUL, SDIV, UDIV,
    MADD, MSUB,
    AND, ORR, EOR, EON,
    CMP_R, CMP_I, CMN_R, CMN_I,
    LSL_R, LSL_I,
    LSR_R, LSR_I,
    ASR_R, ASR_I,
    SXTB, SXTH, SXTW,
    UXTB, UXTH, UXTW,
    LDRB, LDRH, LDR, LDRSB, LDRSH, LDRSW,
    STRB, STRH, STR,
    LDP, STP,
    ADRP,
    CSET,
    B, BR,
    BEQ, BNE, BHS, BLO, BMI, BPL, BVS, BVC,
    BHI, BLS, BGE, BLT, BGT, BLE, BAL, BNV,
    BL, BLR,
    RET,
    SVC,

    F_LDR, F_STR,
    F_LDP, F_STP,
    FMOV,
    FADD, FSUB, FMUL, FDIV,
    FCMP, FNEG,
    FSQRT,
    SCVTF, UCVTF,
    FCVT, FCVTZS, FCVTZU,
} arm64_asm_opcode_t;

typedef enum {
    ARM64_ASM_OPERAND_REG = 1,
    ARM64_ASM_OPERAND_FREG,
    ARM64_ASM_OPERAND_IMMEDIATE, // #233

    ARM64_ASM_OPERAND_SYMBOL, // label 或者 symbol 符号，汇编器基于此进行 offset 计算。编译二进制时通常使用占位符号占用

    // indirect:   [reg,#nn]
    // pre-index:  [reg,#nn]!
    // post-index: [reg],#nn
    ARM64_ASM_OPERAND_INDIRECT,

    // [reg,reg,#nn]
    ARM64_ASM_OPERAND_REG_OFFSET,

    ARM64_ASM_OPERAND_COND,
    ARM64_ASM_OPERAND_SHIFT,
    ARM64_ASM_OPERAND_EXTEND,
} arm64_asm_operand_type;

typedef enum {
    ARM64_COND_NOCOND = -1,
    ARM64_COND_EQ, ARM64_COND_NE, ARM64_COND_HS, ARM64_COND_LO,
    ARM64_COND_MI, ARM64_COND_PL, ARM64_COND_VS, ARM64_COND_VC,
    ARM64_COND_HI, ARM64_COND_LS, ARM64_COND_GE, ARM64_COND_LT,
    ARM64_COND_GT, ARM64_COND_LE, ARM64_COND_AL, ARM64_COND_NV
} arm64_asm_cond_type;


typedef struct {
    arm64_asm_operand_type type;
    union {
        reg_t reg; // 包括 freg 和 reg
        int64_t immediate;
        struct {
            char *name;
            bool is_local;
            int64_t offset; // 汇编器识别便宜 offset
        } symbol;
        struct {
            int64_t offset;
            reg_t *reg;
            int8_t prepost; // 0=none, 1=pre, 2=post
        } indirect; // 间接寻址， [x0] [x0, #14]

        struct {
            reg_t *base;
            reg_t *index;
            int64_t scale;
            int64_t extend; // 0=no, 1=sxtw, 2=uxtw, 3=lsl, 4=sxtx
        } reg_offset;

        arm64_asm_cond_type cond;
        struct {
            int64_t option;
            int64_t imm;
        } extend;
    };
} arm64_asm_operand_t;

typedef struct {
    uint64_t op_id;
    arm64_asm_raw_opcode_t raw_opcode;
    arm64_asm_opcode_t opcode;
    uint8_t count;
    arm64_asm_operand_t *operands[4];
} arm64_asm_inst_t;

#define R32  (1 << 0)
#define R64  (1 << 1)
#define F32  (1 << 2)
#define F64  (1 << 3)
#define RSP  (1 << 4)
#define RZR  (1 << 5)
#define IMM  (1 << 6)
#define IND  (1 << 7) // indirect offset
#define ROI  (1 << 8)  // Register Offset Indirect
#define SYM  (1 << 9) // symbol 类型一般直接使用 imm 占位
#define CND  (1 << 10)
#define SFT  (1 << 11)  // lsl #nn
#define EXT  (1 << 12)  // UXTB, UXTH, UXTW, UXTX, SXTB, SXTH, SXTW, SXTX, LSL, LSR, ASR

#define ZERO  31
#define SP    31
#define LR    30

#define FLAG_MAX_COUNT 5

typedef struct {
    arm64_asm_opcode_t op;
    int flags[FLAG_MAX_COUNT];
} arm64_opr_flags; //arm64_asm_opcode_flags;

typedef struct {
    int count;
    arm64_opr_flags **list;
} arm64_opr_flags_list; // arm64_asm_opcode_flags_list;


#define ARM_REG(_reg) ({ \
    arm64_asm_operand_t *reg_operand = NEW(arm64_asm_operand_t); \
    if (FLAG(LIR_FLAG_ALLOC_FLOAT) & _reg->flag) {\
        reg_operand->type = ARM64_ASM_OPERAND_FREG; \
    } else { \
        reg_operand->type = ARM64_ASM_OPERAND_REG; \
    }                 \
    reg_operand->reg = *_reg;    \
    reg_operand;\
})

#define ARM_SYM(_name, _is_local, _offset) ({ \
    arm64_asm_operand_t *operand = NEW(arm64_asm_operand_t); \
    operand->type = ARM64_ASM_OPERAND_SYMBOL; \
    operand->symbol.name = _name; \
    operand->symbol.is_local = _is_local; \
    operand->symbol.offset = _offset;\
    operand; \
})

#define ARM_IMM(_imm) ({ \
    arm64_asm_operand_t *imm_operand = NEW(arm64_asm_operand_t); \
    imm_operand->type = ARM64_ASM_OPERAND_IMMEDIATE; \
    imm_operand->immediate = _imm; \
    imm_operand; \
})

#define ARM_INDIRECT(_reg, _offset, _prepost) ({ \
    arm64_asm_operand_t *indirect_operand = NEW(arm64_asm_operand_t); \
    indirect_operand->type = ARM64_ASM_OPERAND_INDIRECT; \
    indirect_operand->indirect.reg = _reg; \
    indirect_operand->indirect.offset = _offset; \
    indirect_operand->indirect.prepost = _prepost; \
    indirect_operand; \
})

#define ARM_REG_OFFSET(_base, _index, _scale, _extend) ({ \
    arm64_asm_operand_t *reg_offset_operand = NEW(arm64_asm_operand_t); \
    reg_offset_operand->type = ARM64_ASM_OPERAND_REGISTER_OFFSET; \
    reg_offset_operand->base = _base; \
    reg_offset_operand->index = _index; \
    reg_offset_operand->scale = _scale; \
    reg_offset_operand->extend = _extend; \
    reg_offset_operand; \
})

#define ARM_COND(_cond) ({ \
    arm64_asm_operand_t *cond_operand = NEW(arm64_asm_operand_t); \
    cond_operand->type = ARM64_ASM_OPERAND_COND; \
    cond_operand->cond = _cond; \
    cond_operand; \
})

#define ARM_SHIFT(_option, _amount) ({ \
    arm64_asm_operand_t *shift_operand = NEW(arm64_asm_operand_t); \
    shift_operand->type = ARM64_ASM_OPERAND_SHIFT; \
    shift_operand->extend.option = _option; \
    shift_operand->extend.imm = _amount; \
    shift_operand; \
})

#define ARM_EXTEND(_option, _amount) ({ \
    arm64_asm_operand_t *extend_operand = NEW(arm64_asm_operand_t); \
    extend_operand->type = ARM64_ASM_OPERAND_EXTEND; \
    extend_operand->extend.option = _option; \
    extend_operand->extend.imm = _amount; \
    extend_operand; \
})

// 如何在 native 阶段生成 asm_arm64_inst_t?
#define ARM64_ASM(_raw_opcode, ...) ({ \
    arm64_asm_inst_t* _inst = NEW(arm64_asm_inst_t); \
    _inst->raw_opcode = _raw_opcode;       \
                                    \
    arm64_asm_operand_t *_temp_operands[4] = { __VA_ARGS__ }; \
                                        \
      for (int _i = 0; _i < 4; ++_i) {\
        if (_temp_operands[_i] != NULL) {\
          _inst->operands[_i] = _temp_operands[_i];\
          _inst->count++;\
        }\
      }\
      _inst;\
})

#define ARM64_IMM(imm, t, b)  (((imm) >> (b)) & ((1 << (t - b + 1)) - 1))

#define W_MOVK(sz, rd, imm, sft)                   (0x72800000U | ((sz) << 31) | ((sft) << 21) | (((imm) & ((1U << 16) - 1)) << 5) | (rd))
#define W_MOVZ(sz, rd, imm, sft)                   (0x52800000U | ((sz) << 31) | ((sft) << 21) | (((imm) & ((1U << 16) - 1)) << 5) | (rd))
#define W_MOVN(sz, rd, imm, sft)                   (0x12800000U | ((sz) << 31) | ((sft) << 21) | (((imm) & ((1U << 16) - 1)) << 5) | (rd))

#define W_ADD_I(sz, rd, rn, imm)                   (0x11000000U | ((sz) << 31) | (((imm) & ((1U << 12) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_ADD_S(sz, rd, rn, rm, sft, imm)          (0x0b000000U | ((sz) << 31) | ((sft) << 22) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_ADD_E(sz, rd, rn, rm, option, ov)        (0x0b200000U | ((sz) << 31) | ((rm) << 16) | ((option) << 13) | ((ov) << 10) | ((rn) << 5) | (rd))
#define W_ADDS_I(sz, rd, rn, imm)                  (0x31000000U | ((sz) << 31) | (((imm) & ((1U << 12) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_ADDS_S(sz, rd, rn, rm, imm)              (0x2b000000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_SUB_I(sz, rd, rn, imm)                   (0x51000000U | ((sz) << 31) | (((imm) & ((1U << 12) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_SUB_S(sz, rd, rn, rm, sft, imm)          (0x4b000000U | ((sz) << 31) | ((sft) << 22) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_SUBS_I(sz, rd, rn, imm)                  (0x71000000U | ((sz) << 31) | (((imm) & ((1U << 12) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_SUBS_S(sz, rd, rn, rm, imm)              (0x6b000000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_SUB_E(sz, rd, rn, rm, option, ov)        (0x4b200000U | ((sz) << 31) | ((rm) << 16) | ((option) << 13) | ((ov) << 10) | ((rn) << 5) | (rd))
#define W_MADD(sz, rd, rn, rm, ra)                 (0x1b000000U | ((sz) << 31) | ((rm) << 16) | ((ra) << 10) | ((rn) << 5) | (rd))
#define W_MSUB(sz, rd, rn, rm, ra)                 (0x1b008000U | ((sz) << 31) | ((rm) << 16) | ((ra) << 10) | ((rn) << 5) | (rd))
#define W_SDIV(sz, rd, rn, rm)                     (0x1ac00c00U | ((sz) << 31) | ((rm) << 16) | ((rn) << 5) | (rd))
#define W_UDIV(sz, rd, rn, rm)                     (0x1ac00800U | ((sz) << 31) | ((rm) << 16) | ((rn) << 5) | (rd))
#define W_AND_S(sz, rd, rn, rm, imm)               (0x0a000000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_ORR_S(sz, rd, rn, rm, imm)               (0x2a000000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_EOR_S(sz, rd, rn, rm, imm)               (0x4a000000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))
#define W_EON_S(sz, rd, rn, rm, imm)               (0x4a200000U | ((sz) << 31) | ((rm) << 16) | (((imm) & ((1U << 6) - 1)) << 10) | ((rn) << 5) | (rd))

#define W_LSLV(sz, rd, rn, rm)                     (0x1ac02000U | ((sz) << 31) | ((rm) << 16) | ((rn) << 5) | (rd))
#define W_LSRV(sz, rd, rn, rm)                     (0x1ac02400U | ((sz) << 31) | ((rm) << 16) | ((rn) << 5) | (rd))
#define W_ASRV(sz, rd, rn, rm)                     (0x1ac02800U | ((sz) << 31) | ((rm) << 16) | ((rn) << 5) | (rd))

#define W_SBFM(sz, rd, sn, rn, immr, imms)         (0x13000000U | ((sz) << 31) | ((sn) << 22) | ((immr) << 16) | ((imms) << 10) | ((rn) << 5) | (rd))
#define W_UBFM(sz, rd, sn, rn, immr, imms)         (0x53000000U | ((sz) << 31) | ((sn) << 22) | ((immr) << 16) | ((imms) << 10) | ((rn) << 5) | (rd))

#define W_LDUR(b, s, rt, ofs, base)                (0x38400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((base) << 5) | (rt))
#define W_LDR_UIMM(b, s, rt, ofs, base)            (0x39400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 12) - 1))) << 10) | ((base) << 5) | (rt))
#define W_LDR(b, s, rt, ofs, base, prepost)        (0x38400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((prepost) << 10) | ((base) << 5) | (rt))
#define W_STUR(b, rt, ofs, base)                   (0x38000000U | ((b) << 30) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((base) << 5) | (rt))
#define W_STR_UIMM(b, rt, ofs, base)               (0x39000000U | ((b) << 30) | ((((ofs) & ((1U << 12) - 1))) << 10) | ((base) << 5) | (rt))
#define W_STR(b, rt, ofs, base, prepost)           (0x38000000U | ((b) << 30) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((prepost) << 10) | ((base) << 5) | (rt))
#define W_LDP(sz, rs1, rs2, ofs, base, prepost)    (0x28400000U | ((sz) << 31) | ((prepost) << 23) | (((((ofs) >> 3) & ((1U << 7) - 1))) << 15) | ((rs2) << 10) | ((base) << 5) | (rs1))
#define W_STP(sz, rs1, rs2, ofs, base, prepost)    (0x28000000U | ((sz) << 31) | ((prepost) << 23) | (((((ofs) >> 3) & ((1U << 7) - 1))) << 15) | ((rs2) << 10) | ((base) << 5) | (rs1))

#define W_LDR_R(sz, rt, base, rm, s, s2, option)   (0x38600800U | ((sz) << 30) | ((s) << 23) | ((rm) << 16) | ((option) << 13) | ((s2) << 12) | ((base) << 5) | (rt))
#define W_STR_R(sz, rt, base, rm, s2, option)      (0x38200800U | ((sz) << 30) | ((rm) << 16) | ((option) << 13) | ((s2) << 12) | ((base) << 5) | (rt))

#define W_ADRP(rd, imm)                            (0x90000000U | (ARM64_IMM(imm, 31, 30) << 29) | (ARM64_IMM(imm, 29, 12) << 5) | (rd))

#define W_CSINC(sz, rd, rn, rm, cond)              (0x1a800400U | ((sz) << 31) | ((rm) << 16) | ((cond) << 12) | ((rn) << 5) | (rd))

#define W_B(offset)                                (0x14000000U | ((offset) & ((1U << 26) - 1)))
#define W_BR(rn)                                   (0xd61f0000U | ((rn) << 5))
#define W_BCC(cond)                                (0x54000000U | (cond))

#define W_BL(offset)                               (0x94000000U | ((offset) & ((1U << 26) - 1)))
#define W_BLR(rn)                                  (0xd63f0000U | ((rn) << 5))
#define W_RET(rn)                                  (0xd65f0000U | ((rn) << 5))
#define W_SVC(imm)                                 (0xd4000001U | ((imm) << 5))

#define P_MOV(sz, rd, rs)                          W_ORR_S(sz, rd, ZERO, rs, 0)
#define P_MOV_SP(sz, rd, rs)                       W_ADD_I(sz, rd, rs, 0)
#define P_MUL(sz, rd, rn, rm)                      W_MADD(sz, rd, rn, rm, ZERO)
#define P_CMP(sz, rm, rn)                          W_SUBS_S(sz, ZERO, rm, rn, 0)
#define P_CMP_I(sz, rd, imm)                       W_SUBS_I(sz, ZERO, rd, imm)
#define P_CMN(sz, rn, rm)                          W_ADDS_S(sz, ZERO, rn, rm, 0)
#define P_CMN_I(sz, rd, imm)                       W_ADDS_I(sz, ZERO, rd, imm)
#define P_LSL_I(sz, rd, rn, imm)                   W_UBFM(sz, rd, sz, rn, -(imm) & (63>>(1-(sz))), (63>>(1-(sz))) - (imm))
#define P_LSR_I(sz, rd, rn, imm)                   W_UBFM(sz, rd, sz, rn, imm, 63>>(1-(sz)))
#define P_ASR_I(sz, rd, rn, imm)                   W_SBFM(sz, rd, sz, rn, imm, 63>>(1-(sz)))
#define P_CSET(sz, rd, cond)                       W_CSINC(sz, rd, ZERO, ZERO, (cond) ^ 1)

// FP instructions.

#define F_LDUR(b, s, rt, ofs, base)                (0xbc400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((base) << 5) | (rt))
#define F_LDR_UIMM(b, s, rt, ofs, base)            (0xbd400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 12) - 1))) << 10) | ((base) << 5) | (rt))
#define F_LDR(b, s, rt, ofs, base, prepost)        (0xbc400000U | ((b) << 30) | ((s) << 23) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((prepost) << 10) | ((base) << 5) | (rt))
#define F_STUR(b, rt, ofs, base)                   (0xbc000000U | ((b) << 30) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((base) << 5) | (rt))
#define F_STR_UIMM(b, rt, ofs, base)               (0xbd000000U | ((b) << 30) | ((((ofs) & ((1U << 12) - 1))) << 10) | ((base) << 5) | (rt))
#define F_STR(b, rt, ofs, base, prepost)           (0xbc000000U | ((b) << 30) | ((((ofs) & ((1U << 9) - 1))) << 12) | ((prepost) << 10) | ((base) << 5) | (rt))
#define F_LDP(b, rt, ru, base, ofs, prepost)       (0x2c400000U | ((b) << 30) | ((prepost) << 23) | ((((ofs) & ((1U << 10) - (1 << 3)))) << (15 - 3)) | ((ru) << 10) | ((base) << 5) | (rt))
#define F_STP(b, rt, ru, base, ofs, prepost)       (0x2c000000U | ((b) << 30) | ((prepost) << 23) | ((((ofs) & ((1U << 10) - (1 << 3)))) << (15 - 3)) | ((ru) << 10) | ((base) << 5) | (rt))

#define F_LDR_R(sz, rt, base, rm, s, s2, option)   (0xbc700800U | ((sz) << 30) | ((s) << 23) | ((rm) << 16) | ((option) << 13) | ((s2) << 12) | ((base) << 5) | (rt))
#define F_STR_R(sz, rt, base, rm, s2, option)      (0xbc300800U | ((sz) << 30) | ((rm) << 16) | ((option) << 13) | ((s2) << 12) | ((base) << 5) | (rt))

#define FMOV(sz, rd, rn)                           (0x1e204000U | ((sz) << 22) | ((rn) << 5) | (rd))
#define FADD(sz, rd, rn, rm)                       (0x1e202800U | ((sz) << 22) | ((rm) << 16) | ((rn) << 5) | (rd))
#define FSUB(sz, rd, rn, rm)                       (0x1e203800U | ((sz) << 22) | ((rm) << 16) | ((rn) << 5) | (rd))
#define FMUL(sz, rd, rn, rm)                       (0x1e200800U | ((sz) << 22) | ((rm) << 16) | ((rn) << 5) | (rd))
#define FDIV(sz, rd, rn, rm)                       (0x1e201800U | ((sz) << 22) | ((rm) << 16) | ((rn) << 5) | (rd))
#define FCMP(sz, rd, rn)                           (0x1e202000U | ((sz) << 22) | ((rn) << 16) | ((rd) << 5))
#define FNEG(sz, rd, rn)                           (0x1e214000U | ((sz) << 22) | ((rn) << 5) | (rd))
#define FSQRT(sz, rd, rn)                          (0x1e21c000U | ((sz) << 22) | ((rn) << 5) | (rd))

#define SCVTF(dsz, rt, ssz, rn)                    (0x1e220000 | ((dsz) << 31) | ((ssz) << 22) | ((rn) << 5) | (rt))
#define UCVTF(dsz, rt, ssz, rn)                    (0x1e230000 | ((dsz) << 31) | ((ssz) << 22) | ((rn) << 5) | (rt))
#define FCVT(dsz, rt, rn)                          (0x1e224000 | ((1 - (dsz)) << 22) | ((dsz) << 15) | ((rn) << 5) | (rt))
#define FCVTZS(dsz, rt, ssz, rn)                   (0x1e380000 | ((dsz) << 31) | ((ssz) << 22) | ((rn) << 5) | (rt))
#define FCVTZU(dsz, rt, ssz, rn)                   (0x1e390000 | ((dsz) << 31) | ((ssz) << 22) | ((rn) << 5) | (rt))
extern arm64_opr_flags_list arm64_opcode_map[];

void arm64_match_opcode(arm64_asm_inst_t *inst);

#endif //NATURE_ASM_H
