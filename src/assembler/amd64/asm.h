#ifndef NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ASM_H_

#include <stdlib.h>
#include "src/value.h"
#include "string.h"
#include "src/lib/error.h"
#include "elf.h"

#define NUM32_TO_DATA(number) \
  result.data[i++] = (int8_t) (number);          \
  result.data[i++] = (int8_t) ((number) >> 8);\
  result.data[i++] = (int8_t) ((number) >> 16);\
  result.data[i++] = (int8_t) ((number) >> 24);\

#define NUM64_TO_DATA(number) \
  result.data[i++] = (int8_t) (number);\
  result.data[i++] = (int8_t) ((number) >> 8);\
  result.data[i++] = (int8_t) ((number) >> 16);\
  result.data[i++] = (int8_t) ((number) >> 24);\
  result.data[i++] = (int8_t) ((number) >> 32);\
  result.data[i++] = (int8_t) ((number) >> 40);\
  result.data[i++] = (int8_t) ((number) >> 48);\
  result.data[i++] = (int8_t) ((number) >> 56);\

#define INDIRECT_OFFSET_TO_DATA(modrm, offset) \
  if ((offset) > INT32_MAX || (offset) < INT32_MIN) {\
    error_exit(0, "offset %d to large", offset);\
  }\
  if (((modrm) & 0b11000000) != 0b00000000) {\
    result.data[i++] = (int8_t) (offset);        \
    if (((modrm) & 0b11000000) == 0b10000000) {\
      result.data[i++] = (int8_t) ((offset) >> 8);\
      result.data[i++] = (int8_t) ((offset) >> 16);\
      result.data[i++] = (int8_t) ((offset) >> 24);\
    }\
  }\


#define RETURN_FILL_RESULT(result) \
  (result).offset = current_text_offset;\
  (result).size = i;\
  current_text_offset += i; \
  return result; \


typedef enum {
  ASM_OPERAND_TYPE_REG, // rax,eax,ax  影响不大，可以通过标识符来识别
  ASM_OPERAND_TYPE_SYMBOL,
  ASM_OPERAND_TYPE_IMM, //1,1.1
  ASM_OPERAND_TYPE_DIRECT_ADDR, // movl 0x0001bc,%ebx
  ASM_OPERAND_TYPE_INDIRECT_ADDR, // movl 4(%ebp), %ebx
  ASM_OPERAND_TYPE_INDEX_ADDR, // movl 0x001bc(%ebp,%eax,8), %ebx; move base(offset,scale,size)
} asm_operand_type;

typedef enum {
  ASM_OP_TYPE_MOV,
  ASM_OP_TYPE_ADD,
  ASM_OP_TYPE_SUB,
  ASM_OP_TYPE_MUL,
  ASM_OP_TYPE_DIV,
  ASM_OP_TYPE_LABEL,
  ASM_OP_TYPE_JMP,
  ASM_OP_TYPE_SETG, // >
  ASM_OP_TYPE_SETGE, // >=
  ASM_OP_TYPE_SETE, // ==
  ASM_OP_TYPE_SETNE, // !=
  ASM_OP_TYPE_SETL, // <
  ASM_OP_TYPE_SETLE, // <=
} asm_op_type;

typedef struct {
  char *name;
  uint8_t size; // 单位字节
} asm_reg;

typedef struct {
  char *name;
} asm_symbol;

typedef struct {
//  uint8_t size; // 立即数没有长度定义，跟随指令长度
  int64_t value;
} asm_imm; // 浮点数

typedef struct {
  int64_t addr;
} asm_direct_addr; // 立即寻址

typedef struct {
  char *reg; // 寄存器名称
  int64_t offset; // 单位字节，寄存器偏移，有符号
} asm_indirect_addr; // 间接寻址，都是基于寄存器的

typedef struct {
  size_t base;
  uint16_t offset;
  uint16_t scale;
  uint16_t size;
} asm_index_addr;

typedef struct {
  asm_op_type op; // 操作指令 mov

  uint8_t size; // 操作长度，单位字节， 1，2，4，8 字节。

  void *dst;
  asm_operand_type dst_type;

  void *src;
  asm_operand_type src_type;

  elf_text_item *elf_data;
} asm_inst;

typedef enum {
  ASM_VAR_DECL_TYPE_STRING,
  ASM_VAR_DECL_TYPE_FLOAT,
  ASM_VAR_DECL_TYPE_INT,
} asm_var_decl_type;

// 数据段
typedef struct {
  char *name;
  asm_var_decl_type type;
//  size_t size; // 单位 Byte
  union {
    int int_value;
    float float_value;
    char *string_value;
  };
} asm_var_decl;

struct {
  asm_var_decl list[UINT16_MAX];
  uint16_t count;
} asm_data;

// 指令结构存储 text
struct {
  asm_inst list[UINT16_MAX];
  uint16_t count;
} asm_insts;

void asm_init();
void asm_insts_push(asm_inst inst);
void asm_data_push(asm_var_decl var_decl);

elf_text_item asm_inst_lower(asm_inst inst);

/**
 * 取值 0~7
 * 0: 00000000 ax
 * 1: 00000001 cx
 * 2: 00000010 dx
 * 3: 00000011 bx
 * 4: 00000100 sp
 * 5: 00000101 bp
 * 6: 00000110 si
 * 7: 00000111 di
 * @return
 */
static byte reg_to_number(string reg) {
  if (strcmp(reg, "rax") == 0) {
    return 0;
  }
  if (strcmp(reg, "rcx") == 0) {
    return 1;
  }
  if (strcmp(reg, "rdx") == 0) {
    return 2;
  }
  if (strcmp(reg, "rbx") == 0) {
    return 3;
  }
  if (strcmp(reg, "rsp") == 0) {
    return 4;
  }
  if (strcmp(reg, "rbp") == 0) {
    return 5;
  }
  if (strcmp(reg, "rsi") == 0) {
    return 6;
  }
  if (strcmp(reg, "rdi") == 0) {
    return 7;
  }

  // byte
  if (strcmp(reg, "al") == 0) {
    return 0;
  }
  if (strcmp(reg, "cl") == 0) {
    return 1;
  }
  if (strcmp(reg, "dl") == 0) {
    return 2;
  }
  if (strcmp(reg, "bl") == 0) {
    return 3;
  }
  if (strcmp(reg, "ah") == 0) {
    return 4;
  }
  if (strcmp(reg, "ch") == 0) {
    return 5;
  }
  if (strcmp(reg, "dh") == 0) {
    return 6;
  }
  if (strcmp(reg, "bh") == 0) {
    return 7;
  }

  error_exit(0, "cannot parser '%s' reg", reg);
  exit(0);
}

static bool is_imm(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_IMM) {
    return true;
  }
  return false;
}

static bool is_rax(asm_reg *reg) {
  return strcmp(reg->name, "rax") == 0;
}

static bool is_reg(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_REG) {
    return true;
  }
  return false;
}

static bool is_indirect_addr(asm_operand_type t) {
  return t == ASM_OPERAND_TYPE_INDIRECT_ADDR;
}

static bool is_symbol(asm_operand_type t) {
  return t == ASM_OPERAND_TYPE_SYMBOL;
}

static bool is_direct_addr(asm_operand_type t) {
  return t == ASM_OPERAND_TYPE_DIRECT_ADDR;
}

static bool is_addr(asm_operand_type t) {
  if (t == ASM_OPERAND_TYPE_DIRECT_ADDR || t == ASM_OPERAND_TYPE_INDEX_ADDR || t == ASM_OPERAND_TYPE_INDIRECT_ADDR) {
    return true;
  }
  return false;
}

/**
 * modrm reg 部分为 /0
 * @param inst
 * @param opcode
 * @return
 */
static elf_text_item imm32_to_reg64(asm_inst inst, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  byte rex = 0b01001000;
  asm_reg *dst_reg = inst.dst;
  asm_imm *src_imm = inst.src;
  byte modrm = 0b11000000;
  modrm |= reg_to_number(dst_reg->name);

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // 数字截取 32 位,并转成小端序
  NUM32_TO_DATA((int32_t) src_imm->value)

  RETURN_FILL_RESULT(result)
}

static elf_text_item reg64_to_reg64(asm_inst inst, byte opcode) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();

  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  result.data[i++] = 0b01001000; // rex.w
  result.data[i++] = opcode; // opcode, intel 手册都是 16 进制的，所以使用 16 进制表示比较直观，其余依旧使用 2 进制表示
  byte modrm = 0b11000000; // ModR/M mod(11 表示 r/m confirm to reg) + reg + r/m

  asm_reg *src_reg = inst.src;
  asm_reg *dst_reg = inst.dst;
  modrm |= reg_to_number(src_reg->name) << 3; // reg
  modrm |= reg_to_number(dst_reg->name); // r/m
  result.data[i++] = modrm;

  RETURN_FILL_RESULT(result);
}

static byte indirect_disp_mod(int64_t offset) {
  // 寄存器间址 [rax]
  if (offset == 0) {
    return 0b00000000;
  }

  // 8bit [rax+8]
  if (IN_INT8(offset)) {
    return 0b01000000;
  }

  return 0b10000000;
}

static elf_text_item indirect_addr_with_reg64(asm_reg *reg,
                                              asm_indirect_addr *indirect_addr,
                                              byte opcode, asm_inst inst) {
  elf_text_item result = NEW_EFL_TEXT_ITEM();
  uint8_t i = 0;

  // REX.W => 48H (0100 1000)
  byte rex = 0b01001000;
  byte modrm = indirect_disp_mod(indirect_addr->offset);
  // reg
  modrm |= (reg_to_number(reg->name) << 3);
  // r/m
  modrm |= reg_to_number(indirect_addr->reg);

  result.data[i++] = rex;
  result.data[i++] = opcode;
  result.data[i++] = modrm;

  // disp 如果 disp 为负数，c 语言已经使用了补码表示，所以直接求小端即可
  INDIRECT_OFFSET_TO_DATA(modrm, indirect_addr->offset);

  RETURN_FILL_RESULT(result);
}

#endif //NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
