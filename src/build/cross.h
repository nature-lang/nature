#ifndef NATURE_BUILD_CROSS_H
#define NATURE_BUILD_CROSS_H

#include "utils/list.h"
#include "src/module.h"

#define LINUX_BUILD_TMP_DIR  "/tmp/nature-build.XXXXXX"
#define DARWIN_BUILD_TMP_DIR  ""

// lower select by arch
typedef list *(*lower_fn)(closure *c);

// assembler select by goos + arch

char *cross_tmp_dir();

/**
 * lir to arch asm insts
 * @return
 */
void cross_lower(module_t *m);


/**
 *  汇编器，将 asm_insts 和 var_decl 构造成目标平台 target(elf)
 */
void cross_assembler(module_t *m);

/**
 * 链接器
 */
void cross_linker(slice_t *module_list);


#endif //NATURE_CROSS_H
