#ifndef NATURE_AMD64_H
#define NATURE_AMD64_H

#include "src/binary/linker.h"
#include "assembler.h"
#include "src/binary/opcode/amd64/asm.h"
#include "src/binary/opcode/amd64/opcode.h"

#include <stdlib.h>

typedef struct {
    inst_t *inst;
    uint8_t *data;
    uint8_t data_count;
    uint64_t *offset; // 指令的位置
    asm_operation_t *operation; // 原始指令, 指令改写与二次扫描时使用(只有 amd64 用得上？)
    string rel_symbol; // 使用的符号, 二次扫描时用于判断是否需要重定位，目前都只适用于 label
    asm_operand_t *rel_operand; // 引用自 asm_operations
//    bool may_need_reduce; // jmp 指令可以从 rel32 优化为 rel8
//    uint8_t reduce_count; // jmp rel32 => jmp rel8 导致的指令的长度的变化差值
    uint64_t sym_index; // 指令引用的符号在符号表的索引，如果指令发生了 slot 变更，则响应的符号的 value 同样需要变更
    Elf64_Rela *elf_rel;
} amd64_build_temp_t;


#endif //NATURE_AMD64_H
