#ifndef NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ASM_H_

#include <stdlib.h>

// 汇编数据通用结构

/**
 * 汇编指令通用结构
 * 操作符/目的操作数/源操作数
 * 如何正确的标识操作数？
 */
typedef struct {
  char* op; // movb movl jmp ..
} asm_inst;

#endif //NATURE_SRC_ASSEMBLER_AMD64_ASM_H_
