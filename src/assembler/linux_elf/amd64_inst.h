#ifndef NATURE_AMD64_INSTS_H
#define NATURE_AMD64_INSTS_H

#include "utils/slice.h"
#include <stdlib.h>
#include <stdint.h>
#include "src/value.h"
#include "src/assembler/amd64/asm.h"

uint64_t global_text_offset; // 代码段偏移

/**
 * 指令存储结构
 * @param asm_inst
 */
typedef struct {
    uint8_t *data; // 指令二进制
    uint8_t count; // 指令长度
    uint64_t *offset; // 指令起始 offset
    amd64_asm_inst_t asm_inst; // 原始指令, 指令改写与二次扫描时使用(只有 amd64 用得上？)
    string rel_symbol; // 使用的符号
    amd64_asm_operand_t *rel_operand; // 引用自 asm_inst
    bool may_need_reduce; // jmp 指令可以从 rel32 优化为 rel8
    uint8_t reduce_count; // jmp rel32 => jmp rel8 导致的指令的长度的变化差值
} linux_elf_amd64_text_inst_t;

uint64_t *linux_elf_amd64_current_text_offset();

/**
 * @param insts amd64_insts
 * @return elf_text_insts_t
 */
list *linux_elf_amd64_insts_build(list *insts);

void elf_text_label_build(amd64_asm_inst_t asm_inst, uint64_t *offset);


// 如果 asm_inst 的参数是 label 或者 inst.as = label 需要进行符号注册与处理
// 其中需要一个 link 结构来引用最近 128 个字节的指令，做 jmp rel 跳转，原则上不能影响原来的指令
// 符号表的收集工作，符号表收集需要记录偏移地址，所以如果存在修改，也需要涉及到这里的数据修改
void linux_elf_amd64_text_inst_build(amd64_asm_inst_t asm_inst, uint64_t *offset);


void elf_text_inst_list_build(list *asm_inst_list); // 一次构建基于 asm_inst 列表
void linux_elf_adm64_text_inst_list_second_build(); // 二次构建(基于 linux_elf_text_inst_list)

void linux_elf_amd64_confirm_text_rel(string name);

/**
 * rel32 to rel8, count - 3
 * @param t
 */
void linux_elf_amd64_rewrite_text_rel(linux_elf_amd64_text_inst_t *t);


#endif //NATURE_AMD64_INSTS_H
