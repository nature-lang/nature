#ifndef NATURE_ASSEMBLER_H
#define NATURE_ASSEMBLER_H

#include "linker.h"
#include "utils/list.h"

typedef struct {
    slice_t *sections;
    section_t *data_section;
    section_t *text_section;
    section_t *rel_text_section;
    section_t *symtab_section;
    table *symtab_hash;
    char *output; // 目录名称
} assembler_t;


/**
 * 调用 opcode 生成汇编码二进制，以及构造代码段，可重定位段落等拼凑处 elf 文件
 * 最终经过 output 输出可重定位文件
 */
/**
 *
 */
void linkable_object_format();

#endif //NATURE_ASSEMBLER_H
