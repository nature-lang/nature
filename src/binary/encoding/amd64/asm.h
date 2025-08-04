#ifndef NATURE_SRC_OPCODE_AMD64_ASM_H_
#define NATURE_SRC_OPCODE_AMD64_ASM_H_

#include "src/types.h"
#include "utils/helper.h"
#include "utils/linked.h"
#include <string.h>

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

#define MOVSQ(_prefix) ({                      \
    asm_inst_t *_inst = NEW(amd64_asm_inst_t); \
    _inst->name = "movsq";                     \
    _inst->prefix = _prefix;                   \
    _inst->count = 0;                          \
    _inst;                                     \
})

// ASM_INST("mov",  to, from );
#define AMD64_INST(_name, ...) ({                           \
    amd64_asm_inst_t *_inst = NEW(amd64_asm_inst_t);        \
    _inst->op_id = op->id;                                  \
    _inst->line = op->line;                                 \
    _inst->column = op->column;                             \
    _inst->name = _name;                                    \
    amd64_asm_operand_t *_temp_operands[4] = {__VA_ARGS__}; \
    for (int _i = 0; _i < 4; ++_i) {                        \
        if (_temp_operands[_i] != NULL) {                   \
            _inst->operands[_i] = _temp_operands[_i];       \
            _inst->count++;                                 \
        }                                                   \
    }                                                       \
    _inst;                                                  \
})

#define AMD64_REG(_reg) ({                                       \
    amd64_asm_operand_t *reg_operand = NEW(amd64_asm_operand_t); \
    if (FLAG(LIR_FLAG_ALLOC_FLOAT) & _reg->flag) {               \
        reg_operand->type = AMD64_ASM_OPERAND_TYPE_FREG;         \
    } else {                                                     \
        reg_operand->type = AMD64_ASM_OPERAND_TYPE_REG;          \
    }                                                            \
    reg_operand->size = _reg->size;                              \
    reg_operand->value = _reg;                                   \
    reg_operand;                                                 \
})

#define AMD64_TLS_SYMBOL(_name) ({                     \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_SYMBOL;           \
    asm_symbol_t *_symbol = NEW(asm_symbol_t);                \
    _symbol->name = _name;                                    \
    _symbol->is_tls = true;                            \
    _symbol->is_local = false;                            \
    _operand->size = QWORD;                                       \
    _operand->value = _symbol;                                \
    _operand;                                                 \
})

#define AMD64_SYMBOL(_name, _is_local) ({                     \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_SYMBOL;           \
    asm_symbol_t *_symbol = NEW(asm_symbol_t);                \
    _symbol->name = _name;                                    \
    _symbol->is_local = _is_local;                            \
    _operand->size = 0;                                       \
    _operand->value = _symbol;                                \
    _operand;                                                 \
})

#define LABEL(_name) (AMD64_SYMBOL(_name, false))

#define DISP_REG(_reg, _disp, _size) ({                       \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_DISP_REG;         \
    asm_disp_reg_t *_disp_reg = NEW(asm_disp_reg_t);          \
    _disp_reg->reg = (reg_t *) (_reg);                        \
    _disp_reg->disp = _disp;                                  \
    _operand->size = _size;                                   \
    _operand->value = _disp_reg;                              \
    _operand;                                                 \
})

#define INDIRECT_REG(_reg, _size) ({                          \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_INDIRECT_REG;     \
    asm_indirect_reg_t *_indirect = NEW(asm_indirect_reg_t);  \
    _indirect->reg = (reg_t *) (_reg);                        \
    _operand->size = _size;                                   \
    _operand->value = _indirect;                              \
    _operand;                                                 \
})

#define AMD64_SEG_OFFSET(_name, _offset) ({                         \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_SEG_OFFSET;       \
    asm_seg_offset_t *_seg_offset = NEW(asm_seg_offset_t);    \
    _seg_offset->name = _name;                                \
    _seg_offset->offset = _offset;                            \
    _operand->value = _seg_offset;                            \
    _operand->size = QWORD;                            \
    _operand;                                                 \
})

#define SIB_REG(_base, _index, _scale, _disp, _size) ({       \
    amd64_asm_operand_t *_operand = NEW(amd64_asm_operand_t); \
    _operand->type = AMD64_ASM_OPERAND_TYPE_SIB_REG;          \
    asm_sib_reg_t *_sib = NEW(asm_sib_reg_t);                 \
    _sib->base = _base;                                       \
    _sib->index = _index;                                     \
    _sib->scale = _scale;                                     \
    _sib->disp = _disp;                                       \
    _operand->size = _size;                                   \
    _operand->value = _sib;                                   \
    _operand;                                                 \
})

#define RIP_RELATIVE(_disp) ({                               \
    amd64_asm_operand_t *operand = NEW(amd64_asm_operand_t); \
    operand->type = AMD64_ASM_OPERAND_TYPE_RIP_RELATIVE;     \
    asm_rip_relative_t *rip = NEW(asm_rip_relative_t);       \
    rip->disp = _disp;                                       \
    operand->size = QWORD;                                   \
    operand->value = rip;                                    \
    operand;                                                 \
})

#define AMD64_UINT8(_value) VALUE_OPERAND(asm_uint8_t, AMD64_ASM_OPERAND_TYPE_UINT8, (_value), BYTE)
#define AMD64_UINT16(_value) VALUE_OPERAND(asm_uint16_t, AMD64_ASM_OPERAND_TYPE_UINT16, (_value), WORD)
#define AMD64_UINT32(_value) VALUE_OPERAND(asm_uint32_t, AMD64_ASM_OPERAND_TYPE_UINT32, (_value), DWORD)
#define AMD64_UINT64(_value) VALUE_OPERAND(asm_uint64_t, AMD64_ASM_OPERAND_TYPE_UINT64, (_value), QWORD)
#define AMD64_UINT(_value) VALUE_OPERAND(asm_uint32_t, AMD64_ASM_OPERAND_TYPE_UINT, (_value), QWORD)
#define AMD64_INT8(_value) VALUE_OPERAND(asm_int8_t, AMD64_ASM_OPERAND_TYPE_INT8, (_value), BYTE)
#define AMD64_INT32(_value) VALUE_OPERAND(asm_int32_t, AMD64_ASM_OPERAND_TYPE_INT32, (_value), DWORD)
#define AMD64_FLOAT32(_value) VALUE_OPERAND(asm_float32_t, AMD64_ASM_OPERAND_TYPE_FLOAT32, (_value), OWORD)
#define AMD64_FLOAT64(_value) VALUE_OPERAND(asm_float64_t, AMD64_ASM_OPERAND_TYPE_FLOAT64, (_value), OWORD)

#define VALUE_OPERAND(_type, _operand_type, _value, _size) ({                   \
    amd64_asm_operand_t *_number_operand = malloc(sizeof(amd64_asm_operand_t)); \
    _number_operand->type = (_operand_type);                                    \
    _type *_number = malloc(sizeof(_type));                                     \
    _number->value = (_value);                                                  \
    _number_operand->size = (_size);                                            \
    _number_operand->value = _number;                                           \
    _number_operand;                                                            \
})

typedef enum {
    AMD64_ASM_OPERAND_TYPE_REG = 1,
    AMD64_ASM_OPERAND_TYPE_FREG,
    AMD64_ASM_OPERAND_TYPE_INDIRECT_REG,
    AMD64_ASM_OPERAND_TYPE_DISP_REG,
    AMD64_ASM_OPERAND_TYPE_SIB_REG,
    AMD64_ASM_OPERAND_TYPE_RIP_RELATIVE,
    AMD64_ASM_OPERAND_TYPE_SEG_OFFSET,
    AMD64_ASM_OPERAND_TYPE_SYMBOL,
    AMD64_ASM_OPERAND_TYPE_UINT8,
    AMD64_ASM_OPERAND_TYPE_UINT16,
    AMD64_ASM_OPERAND_TYPE_UINT32,
    AMD64_ASM_OPERAND_TYPE_UINT64,
    AMD64_ASM_OPERAND_TYPE_UINT,
    AMD64_ASM_OPERAND_TYPE_INT8,
    AMD64_ASM_OPERAND_TYPE_INT32,
    AMD64_ASM_OPERAND_TYPE_FLOAT32,
    AMD64_ASM_OPERAND_TYPE_FLOAT64,
} amd64_asm_operand_type;

typedef struct {
    uint8_t value;
} asm_uint8_t;

typedef struct {
    uint16_t value;
} asm_uint16_t;

typedef struct {
    uint32_t value;
} asm_uint32_t;

typedef struct {
    uint64_t value;
} asm_uint64_t;

typedef struct {
    int32_t value;
} asm_int32_t;

typedef struct {
    int8_t value;
} asm_int8_t;

typedef struct {
    float value;
} asm_float32_t;

typedef struct {
    double value;
} asm_float64_t;

typedef struct {
    reg_t *base;
    reg_t *index;
    uint8_t scale;
    int32_t disp;
} asm_sib_reg_t;

typedef struct {
    char *name; // 段寄存器 (fs, gs 等)
    int32_t offset; // TLS 偏移量
} asm_seg_offset_t;

typedef struct {
    reg_t *reg;
} asm_indirect_reg_t; // (%rax)

typedef struct {
    reg_t *reg;
    int32_t disp; // disp 为 0 不就退化为 indirect_reg_t 了么
} asm_disp_reg_t;

typedef struct {
    int32_t disp;
} asm_rip_relative_t;

typedef struct {
    string name; // 符号名称
    bool is_local; // 是内部符号，还是全局符号(global_fn,global_var 或者当前文件不存在的 var)
    bool is_tls;
} asm_symbol_t;


typedef struct {
    amd64_asm_operand_type type;
    uint8_t size;
    void *value; // asm_operand_register
} amd64_asm_operand_t;

/**
 * 汇编指令结构(即如何编写汇编指令)
 * 指令名称可以包含 label
 */
typedef struct {
    uint64_t op_id;
    string name; // 指令名称 operator symbol
    uint8_t prefix; // 自定义指令前缀，覆盖
    bool need_rex_ext; // reg index > 7, e.g. r8,r9,r8w...
    uint8_t count;
    amd64_asm_operand_t *operands[4]; // 最多 4 个参数
    int line;
    int column;
} amd64_asm_inst_t;

static inline amd64_asm_operand_t *amd64_asm_symbol_operand(amd64_asm_inst_t asm_inst) {
    for (int i = 0; i < asm_inst.count; ++i) {
        amd64_asm_operand_t *operand = asm_inst.operands[i];
        if (operand->type == AMD64_ASM_OPERAND_TYPE_SYMBOL) {
            return operand;
        }
    }
    return NULL;
}

static inline amd64_asm_operand_t *asm_match_int_operand(int64_t n) {
    // 正负数处理
    if (n >= INT8_MIN && n <= INT8_MAX) {
        return AMD64_UINT8(n);
    }

    if (n >= INT16_MIN && n <= INT16_MAX) {
        return AMD64_UINT16(n);
    }

    if (n >= INT32_MIN && n <= INT32_MAX) {
        return AMD64_UINT32(n);
    }

    if (n >= INT64_MIN && n <= INT64_MAX) {
        return AMD64_UINT64(n);
    }

    return NULL;
}


#endif //NATURE_SRC_ASSEMBLER_X86_64_ASM_H_
