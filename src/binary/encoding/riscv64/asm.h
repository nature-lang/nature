#ifndef NATURE_RISCV64_ASM_H
#define NATURE_RISCV64_ASM_H

#include "src/register/arch/riscv64.h"
#include "src/types.h"

#define RO_INDIRECT(_reg, _offset, _size) ({                               \
    riscv64_asm_operand_t *_indirect_operand = NEW(riscv64_asm_operand_t); \
    _indirect_operand->type = RISCV64_ASM_OPERAND_INDIRECT;                \
    _indirect_operand->size = _size;                                       \
    _indirect_operand->indirect.reg = _reg;                                \
    _indirect_operand->indirect.offset = _offset;                          \
    _indirect_operand;                                                     \
})

#define RO_IMM(_imm) ({                                               \
    riscv64_asm_operand_t *_imm_operand = NEW(riscv64_asm_operand_t); \
    _imm_operand->type = RISCV64_ASM_OPERAND_IMMEDIATE;               \
    _imm_operand->immediate = _imm;                                   \
    _imm_operand;                                                     \
})

#define RO_SYM(_name, _is_local, _offset, _reloc_type) ({         \
    riscv64_asm_operand_t *_operand = NEW(riscv64_asm_operand_t); \
    _operand->type = RISCV64_ASM_OPERAND_SYMBOL;                  \
    _operand->symbol.name = _name;                                \
    _operand->symbol.is_local = _is_local;                        \
    _operand->symbol.offset = _offset;                            \
    _operand->symbol.reloc_type = _reloc_type;                    \
    _operand;                                                     \
})

#define RO_REG(_reg) ({                                               \
    riscv64_asm_operand_t *_reg_operand = NEW(riscv64_asm_operand_t); \
    if (FLAG(LIR_FLAG_ALLOC_FLOAT) & _reg->flag) {                    \
        _reg_operand->type = RISCV64_ASM_OPERAND_FREG;                \
    } else {                                                          \
        _reg_operand->type = RISCV64_ASM_OPERAND_REG;                 \
    }                                                                 \
    _reg_operand->reg = *_reg;                                        \
    _reg_operand;                                                     \
})

#define RISCV64_INST(_raw_opcode, ...) ({                     \
    riscv64_asm_inst_t *_inst = NEW(riscv64_asm_inst_t);      \
    _inst->op_id = op->id;                                    \
    _inst->line = op->line;                                   \
    _inst->column = op->column;                               \
    _inst->raw_opcode = _raw_opcode;                          \
                                                              \
    riscv64_asm_operand_t *_temp_operands[4] = {__VA_ARGS__}; \
                                                              \
    for (int _i = 0; _i < 4; ++_i) {                          \
        if (_temp_operands[_i] != NULL) {                     \
            _inst->operands[_i] = _temp_operands[_i];         \
            _inst->count++;                                   \
        }                                                     \
    }                                                         \
    _inst;                                                    \
})

typedef enum {
    RV_NOOP, // = 0
    RV_LABEL,
    RV_MV,
    RV_LI,
    RV_LA,
    RV_LUIS,
    RV_ADDIS,
    RV_ADD,
    RV_ADDW,
    RV_ADDI,
    RV_ADDIW,
    RV_SUB,
    RV_SUBW,
    RV_MUL,
    RV_MULW,
    RV_DIV,
    RV_DIVU,
    RV_DIVW,
    RV_DIVUW,
    RV_REM,
    RV_REMU,
    RV_REMW,
    RV_REMUW,
    RV_AND,
    RV_ANDI,
    RV_OR,
    RV_ORI,
    RV_XOR,
    RV_XORI,
    RV_NEG,
    RV_NOT,
    RV_SEXT_B,
    RV_SEXT_H,
    RV_SEXT_W,
    RV_ZEXT_B,
    RV_ZEXT_H,
    RV_ZEXT_W,
    RV_SLL,
    RV_SLLW,
    RV_SLLI,
    RV_SLLIW,
    RV_SRL,
    RV_SRLW,
    RV_SRLI,
    RV_SRLIW,
    RV_SRA,
    RV_SRAW,
    RV_SRAI,
    RV_SRAIW,
    RV_LB,
    RV_LH,
    RV_LW,
    RV_LD,
    RV_LBU,
    RV_LHU,
    RV_LWU,
    RV_SB,
    RV_SH,
    RV_SW,
    RV_SD,
    RV_SLT,
    RV_SLTU,
    RV_SLTI,
    RV_SLTIU,
    RV_SEQZ,
    RV_SNEZ,
    RV_SLTZ,
    RV_SGTZ,
    RV_J,
    RV_JR,
    RV_JALR,
    RV_BEQ,
    RV_BNE,
    RV_BLT,
    RV_BGE,
    RV_BLTU,
    RV_BGEU,
    RV_CALL,
    RV_RET,
    RV_ECALL,

    RV_FADD_D,
    RV_FSUB_D,
    RV_FMUL_D,
    RV_FDIV_D,
    RV_FADD_S,
    RV_FSUB_S,
    RV_FMUL_S,
    RV_FDIV_S,
    RV_FSQRT_D,
    RV_FSQRT_S,
    RV_FSGNJ_D,
    RV_FSGNJN_D,
    RV_FSGNJX_D,
    RV_FSGNJ_S,
    RV_FSGNJN_S,
    RV_FSGNJX_S,
    RV_FMV_D,
    RV_FNEG_D,
    RV_FMV_S,
    RV_FNEG_S,
    RV_FMV_X_D,
    RV_FMV_X_W,
    RV_FEQ_D,
    RV_FLT_D,
    RV_FLE_D,
    RV_FEQ_S,
    RV_FLT_S,
    RV_FLE_S,
    RV_FLD,
    RV_FLW,
    RV_FSD,
    RV_FSW,

    RV_FCVT_D_W,
    RV_FCVT_D_WU,
    RV_FCVT_D_L,
    RV_FCVT_D_LU,
    RV_FCVT_W_D,
    RV_FCVT_WU_D,
    RV_FCVT_L_D,
    RV_FCVT_LU_D,
    RV_FCVT_S_W,
    RV_FCVT_S_WU,
    RV_FCVT_S_L,
    RV_FCVT_S_LU,
    RV_FCVT_W_S,
    RV_FCVT_WU_S,
    RV_FCVT_L_S,
    RV_FCVT_LU_S,
    RV_FCVT_D_S,
    RV_FCVT_S_D,
} riscv64_asm_raw_opcode_t;

typedef enum {
    RISCV64_NOROUND = -1,
    RISCV64_RNE, // Round to Nearest, ties to Even
    RISCV64_RTZ, // Round towards Zero
    RISCV64_RDN, // Round Down (towards -Inf)
    RISCV64_RUP, // Round Up (towards +Inf)
    RISCV64_RMM, // Round to Nearest, ties to Max Magnitude
} riscv64_round_mode_t;

#define RISCV64_R64 (1 << 0)
#define RISCV64_F64 (1 << 1)
#define RISCV64_IMM (1 << 2)
#define RISCV64_IND (1 << 3)
#define RISCV64_SYM (1 << 4)
#define RISCV64_RND (1 << 10)

typedef enum {
    RISCV64_ASM_OPERAND_NOOPERAND,
    RISCV64_ASM_OPERAND_REG, // reg
    RISCV64_ASM_OPERAND_IMMEDIATE, // 1234
    RISCV64_ASM_OPERAND_DIRECT, // foobar + 345
    RISCV64_ASM_OPERAND_INDIRECT, // ofs(reg)
    RISCV64_ASM_OPERAND_FREG, // freg
    RISCV64_ASM_OPERAND_ROUNDMODE, // rm

    RISCV64_ASM_OPERAND_SYMBOL, // label 或者 symbol 符号，汇编器基于此进行 offset 计算。编译二进制时通常使用占位符号占用
} riscv64_asm_operand_type_t;

static char *riscv64_raw_op_names[] = {
        "noop",
        "label",
        "mv",
        "li",
        "la",
        "lui",
        "addi",
        "add",
        "addw",
        "addi",
        "addiw",
        "sub",
        "subw",
        "mul",
        "mulw",
        "div",
        "divu",
        "divw",
        "divuw",
        "rem",
        "remu",
        "remw",
        "remuw",
        "and",
        "andi",
        "or",
        "ori",
        "xor",
        "xori",
        "neg",
        "not",
        "sext.b",
        "sext.h",
        "sext.w",
        "zext.b",
        "zext.h",
        "zext.w",
        "sll",
        "sllw",
        "slli",
        "slliw",
        "srl",
        "srlw",
        "srli",
        "srliw",
        "sra",
        "sraw",
        "srai",
        "sraiw",
        "lb",
        "lh",
        "lw",
        "ld",
        "lbu",
        "lhu",
        "lwu",
        "sb",
        "sh",
        "sw",
        "sd",
        "slt",
        "sltu",
        "slti",
        "sltiu",
        "seqz",
        "snez",
        "sltz",
        "sgtz",
        "j",
        "jr",
        "jalr",
        "beq",
        "bne",
        "blt",
        "bge",
        "bltu",
        "bgeu",
        "call",
        "ret",
        "ecall",

        "fadd.d",
        "fsub.d",
        "fmul.d",
        "fdiv.d",
        "fadd.s",
        "fsub.s",
        "fmul.s",
        "fdiv.s",
        "fsqrt.d",
        "fsqrt.s",
        "fsgnj.d",
        "fsgnjn.d",
        "fsgnjx.d",
        "fsgnj.s",
        "fsgnjn.s",
        "fsgnjx.s",
        "fmv.d",
        "fneg.d",
        "fmv.s",
        "fneg.s",
        "fmv.x.d",
        "fmv.x.w",
        "feq.d",
        "flt.d",
        "fle.d",
        "feq.s",
        "flt.s",
        "fle.s",
        "fld",
        "flw",
        "fsd",
        "fsw",
        "fcvt.d.w",
        "fcvt.d.wu",
        "fcvt.d.l",
        "fcvt.d.lu",
        "fcvt.w.d",
        "fcvt.wu.d",
        "fcvt.l.d",
        "fcvt.lu.d",
        "fcvt.s.w",
        "fcvt.s.wu",
        "fcvt.s.l",
        "fcvt.s.lu",
        "fcvt.w.s",
        "fcvt.wu.s",
        "fcvt.l.s",
        "fcvt.lu.s",
        "fcvt.d.s",
        "fcvt.s.d",
        NULL,
};

typedef enum {
    O_NOOP,
    O_LABEL,
    O_MV,
    O_LI,
    O_LA,
    O_LUIS,
    O_ADDIS,
    O_ADD,
    O_ADDW,
    O_ADDI,
    O_ADDIW,
    O_SUB,
    O_SUBW,
    O_MUL,
    O_MULW,
    O_DIV,
    O_DIVU,
    O_DIVW,
    O_DIVUW,
    O_REM,
    O_REMU,
    O_REMW,
    O_REMUW,
    O_AND,
    O_ANDI,
    O_OR,
    O_ORI,
    O_XOR,
    O_XORI,
    O_NEG,
    O_NOT,
    O_SEXT_B,
    O_SEXT_H,
    O_SEXT_W,
    O_ZEXT_B,
    O_ZEXT_H,
    O_ZEXT_W,
    O_SLL,
    O_SLLW,
    O_SLLI,
    O_SLLIW,
    O_SRL,
    O_SRLW,
    O_SRLI,
    O_SRLIW,
    O_SRA,
    O_SRAW,
    O_SRAI,
    O_SRAIW,
    O_LB,
    O_LH,
    O_LW,
    O_LD,
    O_LBU,
    O_LHU,
    O_LWU,
    O_SB,
    O_SH,
    O_SW,
    O_SD,
    O_SLT,
    O_SLTU,
    O_SLTI,
    O_SLTIU,
    O_SEQZ,
    O_SNEZ,
    O_SLTZ,
    O_SGTZ,
    O_J,
    O_JR,
    O_JALR,
    O_BEQ,
    O_BNE,
    O_BLT,
    O_BGE,
    O_BLTU,
    O_BGEU,
    O_CALL,
    O_RET,
    O_ECALL,

    O_FADD_D,
    O_FSUB_D,
    O_FMUL_D,
    O_FDIV_D,
    O_FADD_S,
    O_FSUB_S,
    O_FMUL_S,
    O_FDIV_S,
    O_FSQRT_D,
    O_FSQRT_S,
    O_FSGNJ_D,
    O_FSGNJN_D,
    O_FSGNJX_D,
    O_FSGNJ_S,
    O_FSGNJN_S,
    O_FSGNJX_S,
    O_FMV_D,
    O_FNEG_D,
    O_FMV_S,
    O_FNEG_S,
    O_FMV_X_D,
    O_FMV_X_W,
    O_FEQ_D,
    O_FLT_D,
    O_FLE_D,
    O_FEQ_S,
    O_FLT_S,
    O_FLE_S,
    O_FLD,
    O_FLW,
    O_FSD,
    O_FSW,

    O_FCVT_D_W,
    O_FCVT_D_WU,
    O_FCVT_D_L,
    O_FCVT_D_LU,
    O_FCVT_W_D,
    O_FCVT_WU_D,
    O_FCVT_L_D,
    O_FCVT_LU_D,
    O_FCVT_S_W,
    O_FCVT_S_WU,
    O_FCVT_S_L,
    O_FCVT_S_LU,
    O_FCVT_W_S,
    O_FCVT_WU_S,
    O_FCVT_L_S,
    O_FCVT_LU_S,
    O_FCVT_D_S,
    O_FCVT_S_D,
} riscv64_asm_opcode_t;


#define FLAG_MAX_COUNT 5

typedef struct {
    riscv64_asm_opcode_t op;
    int flags[FLAG_MAX_COUNT];
} riscv64_opr_flags; //riscv64_asm_opcode_flags;

typedef struct {
    int count;
    riscv64_opr_flags **list;
} riscv64_opr_flags_list; // arm64_asm_opcode_flags_list;


static const struct {
    const char *name;
    riscv64_round_mode_t mode;
} riscv64_round_modes[] = {
        {"rne", RISCV64_RNE},
        {"rtz", RISCV64_RTZ},
        {"rdn", RISCV64_RDN},
        {"rup", RISCV64_RUP},
        {"rmm", RISCV64_RMM},
};

typedef enum {
    ASM_RISCV64_RELOC_NONE = 0,
    ASM_RISCV64_RELOC_BRANCH, // beq x1, x2, symbol
    ASM_RISCV64_RELOC_JAL, // jal symbol(+-1M)
    ASM_RISCV64_RELOC_CALL, // call symbol  R_RISCV_PCREL_HI20 + R_RISCV_PCREL_LO12_I, la 指令使用同样的方式

    ASM_RISCV64_RELOC_TPREL_HI20,
    ASM_RISCV64_RELOC_TPREL_LO12_I,
    ASM_RISCV64_RELOC_TPREL_LO12_S,
} asm_riscv64_reloc_type;

typedef struct {
    char *name;
    bool is_local;
    int64_t offset; // 汇编器识别 offset
    asm_riscv64_reloc_type reloc_type;
} riscv64_asm_operand_symbol_t;

typedef struct {
    riscv64_asm_operand_type_t type;
    uint8_t size;

    union {
        reg_t reg;
        int64_t immediate;
        struct {
            int64_t offset;
            reg_t *reg;
        } indirect;
        riscv64_round_mode_t round_mode;

        riscv64_asm_operand_symbol_t symbol;
    };
} riscv64_asm_operand_t;

// riscv64 在指令生成时长度不固定，从 16 ～ 64 不等, 所以在 inst 中冗余空间存储指令
typedef struct {
    uint64_t op_id;
    int line;
    int column;
    riscv64_asm_raw_opcode_t raw_opcode;
    riscv64_asm_opcode_t opcode;
    uint8_t count;
    riscv64_asm_operand_t *operands[4];

    uint8_t opcode_count;
    uint8_t opcode_data[26];
} riscv64_asm_inst_t;

void riscv64_match_opcode(riscv64_asm_inst_t *inst);

#endif //NATURE_ASM_H
