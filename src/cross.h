#ifndef NATURE_CROSS_H
#define NATURE_CROSS_H

#include "utils/list.h"
#include "src/module.h"

char *goos; // linux
char *arch; // amd64

// lower select by arch
typedef list *(*lower_fn)(closure *c);

// assembler select by goos + arch

/**
 * lir to arch asm insts
 * @return
 */
list *cross_lower(module_t *m);


/**
 *  汇编器，将 asm_insts 和 var_decl 构造成目标平台 target(elf)
 */
void cross_assembler(module_t *m);

/**
 * 链接器
 */
void cross_linker(slice_t *list);


#endif //NATURE_CROSS_H
