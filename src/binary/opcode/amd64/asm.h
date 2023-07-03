#ifndef NATURE_SRC_OPCODE_AMD64_ASM_H_
#define NATURE_SRC_OPCODE_AMD64_ASM_H_

#include <string.h>
#include "utils/helper.h"
#include "utils/linked.h"
#include "src/register/register.h"

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16 // 16 byte = 128位 xmm
#define YWORD 32 // 32 byte = ymm
#define ZWORD 64 // 64 byte

#define MOVSQ(_prefix) ({\
  asm_inst_t *_inst = NEW(asm_operation_t);\
  _inst->name = "movsq"; \
  _inst->prefix = _prefix; \
  _inst->count = 0;\
  _inst;\
})

// ASM_INST("mov", { to, from });
#define ASM_INST(_name, ...) ({ \
  asm_operation_t *_inst = NEW(asm_operation_t); \
  memset(_inst, 0, sizeof(asm_operation_t)); \
  _inst->op_id = op->id; \
  _inst->name = _name;\
  asm_operand_t *_temp_operands[4] = __VA_ARGS__;\
  for (int _i = 0; _i < 4; ++_i) {\
    if (_temp_operands[_i] != NULL) {\
      _inst->operands[_i] = _temp_operands[_i];\
      _inst->count++;\
    }\
  }\
  _inst;\
})

#define REG(_reg) ({ \
    asm_operand_t *reg_operand = NEW(asm_operand_t); \
    if (FLAG(LIR_FLAG_ALLOC_FLOAT) & _reg->flag) {\
        reg_operand->type = ASM_OPERAND_TYPE_FREG; \
    } else { \
        reg_operand->type = ASM_OPERAND_TYPE_REG; \
    }                 \
    reg_operand->size = _reg->size;\
    reg_operand->value = _reg;    \
    reg_operand;\
})

#define SYMBOL(_name, _is_local) ({ \
     asm_operand_t *_operand = NEW(asm_operand_t); \
     _operand->type = ASM_OPERAND_TYPE_SYMBOL;  \
     asm_symbol_t *_symbol = NEW(asm_symbol_t); \
     _symbol->name = _name;    \
     _symbol->is_local = _is_local;    \
     _operand->size = 0;\
     _operand->value = _symbol;    \
     _operand;\
})

#define LABEL(_name) (SYMBOL(_name, false))

#define DISP_REG(_reg, _disp, _size) ({ \
     asm_operand_t *_operand = NEW(asm_operand_t); \
     _operand->type = ASM_OPERAND_TYPE_DISP_REG;  \
     asm_disp_reg_t *_disp_reg = NEW(asm_disp_reg_t); \
     _disp_reg->reg = (reg_t*)(_reg);    \
     _disp_reg->disp = _disp;    \
     _operand->size = _size; \
     _operand->value = _disp_reg;    \
     _operand;\
})

#define INDIRECT_REG(_reg, _size) ({ \
     asm_operand_t *_operand = NEW(asm_operand_t); \
     _operand->type = ASM_OPERAND_TYPE_INDIRECT_REG;  \
     asm_indirect_reg_t *_indirect = NEW(asm_indirect_reg_t); \
     _indirect->reg = (reg_t*)(_reg);    \
     _operand->size = _size;\
     _operand->value = _indirect;    \
     _operand;\
})

#define SIB_REG(_base, _index, _scale) ({ \
     asm_operand_t *operand = NEW(asm_operand_t); \
     operand->type = ASM_OPERAND_TYPE_SIB_REG;  \
     asm_sib_register_t *_sib = NEW(asm_sib_reg_t); \
     _sib->base = _base;\
     _sib->index = _index;\
     _sib->scale = _scale;\
     operand->size = _base->size;\
     operand->value = _sib;    \
     operand;\
})

#define RIP_RELATIVE(_disp) ({ \
     asm_operand_t *operand = NEW(asm_operand_t); \
     operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;  \
     asm_rip_relative_t *rip = NEW(asm_rip_relative_t); \
     rip->disp = _disp;\
     operand->size = QWORD;\
     operand->value = rip;  \
     operand;\
})

#define UINT8(_value) VALUE_OPERAND(asm_uint8_t, ASM_OPERAND_TYPE_UINT8, (_value), BYTE)
#define UINT16(_value) VALUE_OPERAND(asm_uint16_t, ASM_OPERAND_TYPE_UINT16, (_value), WORD)
#define UINT32(_value) VALUE_OPERAND(asm_uint32_t, ASM_OPERAND_TYPE_UINT32, (_value), DWORD)
#define UINT64(_value) VALUE_OPERAND(asm_uint64_t, ASM_OPERAND_TYPE_UINT64, (_value), QWORD)
#define UINT(_value) VALUE_OPERAND(asm_uint32_t, ASM_OPERAND_TYPE_UINT, (_value), QWORD)
#define INT8(_value) VALUE_OPERAND(asm_int8_t, ASM_OPERAND_TYPE_INT8, (_value), BYTE)
#define INT32(_value) VALUE_OPERAND(asm_int32_t, ASM_OPERAND_TYPE_INT32, (_value), DWORD)
#define FLOAT32(_value) VALUE_OPERAND(asm_float32_t, ASM_OPERAND_TYPE_FLOAT32, (_value), OWORD)
#define FLOAT64(_value) VALUE_OPERAND(asm_float64_t, ASM_OPERAND_TYPE_FLOAT64, (_value), OWORD)

#define VALUE_OPERAND(_type, _operand_type, _value, _size) ({ \
    asm_operand_t *_number_operand = malloc(sizeof(asm_operand_t));\
    _number_operand->type = (_operand_type);\
    _type *_number = malloc(sizeof(_type));\
    _number->value = (_value);\
    _number_operand->size = (_size);\
    _number_operand->value = _number;\
    _number_operand;\
})

typedef enum {
    ASM_OPERAND_TYPE_REG = 1,
    ASM_OPERAND_TYPE_FREG,
    ASM_OPERAND_TYPE_INDIRECT_REG,
    ASM_OPERAND_TYPE_SIB_REG,
    ASM_OPERAND_TYPE_RIP_RELATIVE,
    ASM_OPERAND_TYPE_DISP_REG, // 这是啥？有区别？
    ASM_OPERAND_TYPE_SYMBOL,
    ASM_OPERAND_TYPE_UINT8,
    ASM_OPERAND_TYPE_UINT16,
    ASM_OPERAND_TYPE_UINT32,
    ASM_OPERAND_TYPE_UINT64,
    ASM_OPERAND_TYPE_UINT,
    ASM_OPERAND_TYPE_INT8,
    ASM_OPERAND_TYPE_INT32,
    ASM_OPERAND_TYPE_FLOAT32,
    ASM_OPERAND_TYPE_FLOAT64,
} asm_operand_type;

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

// TODO offset?
typedef struct {
    reg_t *base; // 决定了宽度
    reg_t *index;
    uint8_t scale;
} asm_sib_reg_t;

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
} asm_symbol_t;


typedef struct {
    asm_operand_type type;
    uint8_t size;
    void *value; // asm_operand_register
} asm_operand_t;

/**
 * 汇编指令结构(即如何编写汇编指令)
 * 指令名称可以包含 label
 */
typedef struct {
    uint64_t op_id;
    string name; // 指令名称 operator symbol
    uint8_t prefix; // 自定义指令前缀，覆盖
    uint8_t count;
    asm_operand_t *operands[4]; // 最多 4 个参数
} asm_operation_t;

asm_operand_t *amd64_asm_symbol_operand(asm_operation_t asm_inst);

asm_operand_t *asm_match_int_operand(int64_t n);

#endif //NATURE_SRC_ASSEMBLER_X86_64_ASM_H_
