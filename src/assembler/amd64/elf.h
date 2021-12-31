#ifndef NATURE_SRC_ASSEMBLER_AMD64_ELF_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ELF_H_

#include <stdlib.h>

#define NEW_EFL_TEXT_ITEM() \
{                           \
.data = {0}, \
.size = 0, \
.offset = 0, \
.disp_index = 0, \
.disp_size = 0, \
.imm_index = 0, \
.imm_size = 0 \
}

typedef uint64_t elf_address;
typedef uint32_t elf_offset;
typedef uint32_t elf_size;
typedef uint8_t byte;

// 头结构 header

// 段表
typedef struct {
  elf_offset name;
  elf_address address;
  uint8_t type;
  elf_offset offset;
  elf_size size;
} elf_section_header;

typedef struct {
  int count;
  elf_section_header list[UINT8_MAX];
} elf_section_header_table;

// 符号表
typedef struct {
  elf_offset name; // 符号名称,真实字符串保存在串表中
  elf_address value; // 符号值，在可重定位文件内记录为段基址的偏移，可执行文件记录符号线性地址 理论上保存在 .data
  elf_size size; // 符号大小
  uint8_t section_index; // 符号所在段, 0 表示未定义？
} elf_symbol;

typedef struct {
  int count;
  elf_symbol list[UINT8_MAX];
} elf_symbol_table;

// 段表名称字符串表
typedef struct {
  char *value;
  elf_offset offset; // 对应 section_header.name
} elf_str;

struct {
  int count;
  elf_str list[UINT8_MAX];
} elf_section_name_table, elf_symbol_name_table;

// 数据段存储的是一个一个值，按顺序排列的内存位置
typedef struct {
  char *name; // 符号名称
  elf_offset offset; // 数据起始位置
  uint8_t type; // 数据类型,以汇编语言类型为准, int,float,string
  elf_size size; // 单位字节
  void *value; // 数据值，空间基于 size 申请
} elf_data_item;

struct {
  int count;
  elf_data_item list[UINT8_MAX];
} elf_data_section;

// 代码段中的每一项如何定义
typedef struct {
  elf_offset offset; // 指令起始位置，基于段,可以方便的计算出段内偏移，从而编译 label
  elf_size size; // 指令长度,可以方便计算出下一次的偏移， Byte

  byte data[30]; // 指令二进制
//  uint8_t data_count; // 指令长度，毕竟不可能 30 位都用干净, 直接使用上面的 size 即可

  uint8_t disp_index; // 偏移部分在 data 中的索引， 0 表示没有偏移部分
  uint8_t disp_size; // 偏移部分长度， 单位 Byte， 并不确定用多少，暂时选定 16 字节 128(%ebp)

  uint8_t imm_index; // 立即数部分在 data 中的索引，为 0 表示没有立即数部分
  uint8_t imm_size; // 立即数长度, 单位 Byte 直接由汇编指令中的 size 确定
  void *inst; // 指令冗余
} elf_text_item; // 代码段

struct {
  int count;
  elf_text_item list[UINT8_MAX];
} elf_text_section;

elf_offset current_text_offset;

void elf_data_push(elf_text_item text_item);

void elf_text_push();

#endif //NATURE_SRC_ASSEMBLER_AMD64_ELF_H_
