#ifndef NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ASM_H_

#include "src/value.h"
#include "src/lib/list.h"
//#include "opcode.h"

// 指令字符宽度
#define BYTE 1 // 1 byte = 8 位
#define WORD 2 // 2 byte = 16 位
#define DWORD 4 // 4 byte = 32 位
#define QWORD 8 // 8 byte = 64位
#define OWORD 16
#define YWORD 32 // 32 byte
#define ZWORD 64 // 64 byte

typedef enum {
  ASM_OPERAND_TYPE_REGISTER,
  ASM_OPERAND_TYPE_INDIRECT_REGISTER,
  ASM_OPERAND_TYPE_SIB_REGISTER,
  ASM_OPERAND_TYPE_RIP_RELATIVE,
  ASM_OPERAND_TYPE_DISP_REGISTER,
  ASM_OPERAND_TYPE_UINT8,
  ASM_OPERAND_TYPE_UINT16,
  ASM_OPERAND_TYPE_UINT32,
  ASM_OPERAND_TYPE_UINT64,
  ASM_OPERAND_TYPE_INT8,
  ASM_OPERAND_TYPE_INT32,
  ASM_OPERAND_TYPE_FLOAT32,
  ASM_OPERAND_TYPE_FLOAT64,
} asm_operand_type;

typedef struct {
  uint8_t value;
} asm_operand_uint8;

typedef struct {
  uint16_t value;
} asm_operand_uint16;

typedef struct {
  uint32_t value;
} asm_operand_uint32;

typedef struct {
  uint64_t value;
} asm_operand_uint64;

typedef struct {
  int32_t value;
} asm_operand_int32;

typedef struct {
  int8_t value;
} asm_operand_int8;

typedef struct {
  float value;
} asm_operand_float32;

typedef struct {
  double value;
} asm_operand_float64;

/**
 * 汇编指令参数
 */
typedef struct {
  string name;
  uint8_t index; // index 对应 intel 手册表中的索引，可以直接编译进 modrm 中
  uint8_t size;
} asm_operand_register; // size 是个啥？

typedef struct {
  asm_operand_register base;
  asm_operand_register index;
  uint8_t scale;
} asm_operand_sib_register;

typedef struct {
  asm_operand_register reg;
} asm_operand_indirect_register; // (%rax)

typedef struct {
  asm_operand_register reg;
  uint8_t disp;
} asm_operand_disp_register;

typedef struct {
  int32_t disp;
} asm_operand_rip_relative;

typedef struct {
  uint8_t type;
  uint8_t size;
  void *value; // asm_operand_register
} asm_operand_t;

typedef struct {
  string name;
} asm_operand_symbol_t;

/**
 * 汇编指令结构(即如何编写汇编指令)
 * 指令名称可以包含 label
 */
typedef struct {
  string name; // 指令名称
  uint8_t count;
  asm_operand_t *operands[4]; // 最多 4 个参数
} asm_inst_t;

// 数据段(编译进符号表即可，数据类型需要兼容高级类型)
//typedef struct {
//  char *name;
//  asm_var_decl_type type;
////  size_t size; // 单位 Byte
//  union {
//    int int_value;
//    float float_value;
//    char *string_value;
//  };
//} asm_var_decl;
//
//struct {
//  asm_var_decl list[UINT16_MAX];
//  uint16_t count;
//} asm_data;


list *asm_inst_list;

asm_inst_t asm_rewrite(asm_inst_t asm_inst);

asm_operand_t *asm_has_fn_operand(asm_inst_t asm_inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
