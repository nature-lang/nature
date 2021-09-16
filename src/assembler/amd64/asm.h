#ifndef NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ASM_H_

#include <stdlib.h>
typedef enum {
  ASM_OPERAND_TYPE_REG, // rax,eax,ax  影响不大，可以通过标识符来识别
  ASM_OPERAND_TYPE_SYMBOL,
  ASM_OPERAND_TYPE_IMM, //1,1.1
  ASM_OPERAND_TYPE_DIRECT_ADDR, // movl 0x0001bc,%ebx
  ASM_OPERAND_TYPE_INDIRECT_ADDR, // movl 4(%ebp), %ebx
  ASM_OPERAND_TYPE_INDEX_ADDR, // movl 0x001bc(%ebp,%eax,8), %ebx; move base(offset,scale,size)
} asm_operand_type;

typedef struct {
  char *name;
  uint8_t size; // 单位字节
} asm_reg;

typedef struct {
  char *name; // 引用自 data 段的标识符
} asm_symbol;

typedef struct {
  uint8_t size; // 单位字节
  size_t value;
} asm_imm; // 浮点数也是立即数吗

typedef struct {
  size_t addr;
} asm_direct_addr; // 立即寻址

typedef struct {
  char *reg; // 寄存器名称
  size_t offset; // 单位字节，寄存器便宜，有符号
} asm_indirect_addr; // 间接寻址，都是基于寄存器的

typedef struct {
  size_t base;
  uint16_t offset;
  uint16_t scale;
  uint16_t size;
} asm_index_addr;

typedef struct {
  uint8_t op; // 操作指令 mov

  uint8_t size; // 操作长度，单位字节， 1，2，4，8 字节。

  void *dst;
  uint8_t dst_type;

  void *src;
  uint8_t src_type;
} asm_inst;

// 指令结构存储 text
typedef struct {
  asm_inst list[UINT16_MAX];
  uint16_t count;
} asm_insts;

// 数据段
typedef struct {
  char *name;
  uint8_t type;
  size_t size;
//  uint8_t repeat; // 暂时不实现重复功能
  void *value;
} asm_var_decl;

typedef struct {
  asm_var_decl list[UINT16_MAX];
  uint16_t count;
} asm_data;

void asm_insts_push(asm_inst inst);
void asm_data_push(asm_var_decl data);

int hello();

#endif //NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
