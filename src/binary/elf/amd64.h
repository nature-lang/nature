#ifndef NATURE_AMD64_H
#define NATURE_AMD64_H

#include "linker.h"
#include "assembler.h"
#include "src/binary/opcode/amd64/asm.h"
#include "src/binary/opcode/amd64/opcode.h"

#include <stdlib.h>

#define AMD64_ELF_START_ADDR 0x400000
#define AMD64_64_ELF_PAGE_SIZE 0x200000
#define AMD64_PTR_SIZE 8 // 单位 byte

typedef struct {
    uint8_t *data;
    uint8_t data_count;
    uint64_t *offset; // 指令的其实位置
    asm_operation_t *operation; // 原始指令, 指令改写与二次扫描时使用(只有 amd64 用得上？)
    string rel_symbol; // 使用的符号
    asm_operand_t *rel_operand; // 引用自 asm_inst
    bool may_need_reduce; // jmp 指令可以从 rel32 优化为 rel8
    uint8_t reduce_count; // jmp rel32 => jmp rel8 导致的指令的长度的变化差值
    uint64_t sym_index; // 指令引用的符号在符号表的索引，如果指令发生了 slot 变更，则响应的符号的 value 同样需要变更
    Elf64_Rela *elf_rel;
} amd64_build_temp_t;

int amd64_gotplt_entry_type(uint relocate_type);

uint amd64_create_plt_entry(elf_context *ctx, uint got_offset, sym_attr_t *attr);

int8_t amd64_is_code_relocate(uint relocate_type);

void amd64_relocate(elf_context *ctx, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val);

/**
 * 经过两次遍历最终生成 section text、symbol、rela
 * @param ctx
 * @param operations amd64_opcode_t
 */
void amd64_operation_encodings(elf_context *ctx, slice_t *operations);

#endif //NATURE_AMD64_H
