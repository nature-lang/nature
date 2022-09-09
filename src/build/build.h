#ifndef NATURE_BUILD_BUILD_H
#define NATURE_BUILD_BUILD_H

#include "src/module.h"

void build(char *build_entry);

/**
 * lir to arch asm insts
 * @return
 */
void cross_lower(module_t *m);


/**
 *  汇编，将 opcodes 和 var_decl 构造成目标平台 target(elf)
 */
void build_assembler(module_t *m);

/**
 * 链接器
 */
void build_linker(slice_t *module_list);

#endif //NATURE_BUILD_H
