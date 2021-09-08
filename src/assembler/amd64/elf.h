#ifndef NATURE_SRC_ASSEMBLER_AMD64_ELF_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ELF_H_

#include <stdlib.h>

// 头结构 header

// 段表结构

// 符号表

typedef struct {
  uint32_t name; // 符号名称,真实字符串保存在串表中
  uint32_t value; // 符号值，在可重定位文件内记录为段基址的偏移，可执行文件记录符号线性地址 理论上保存在 .data
  uint32_t size; // 符号大小
  uint8_t section_index; // 符号所在段, 0 表示未定义？
} elf_symbol;


// 字符串表

// 段表

// 段表字符串表

// 代码段(16进制)

// 数据段
#endif //NATURE_SRC_ASSEMBLER_AMD64_ELF_H_
