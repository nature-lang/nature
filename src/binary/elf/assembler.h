#ifndef NATURE_ASSEMBLER_H
#define NATURE_ASSEMBLER_H

#include "linker.h"
#include "utils/list.h"

/**
 * 调用 opcode 生成汇编码二进制，以及构造代码段，可重定位段落等拼凑出 elf 文件的各个段落
 * 最终经过 output 输出可重定位文件
 */
/**
 * - 编译变量
 * - 编译指令
 */
void linkable_object_format(elf_context *ctx, slice_t *opcodes, slice_t *var_decls);


/**
 * 变量声明编码
 * @param ctx
 * @param var_decls
 */
void var_decl_encodings(elf_context *ctx, slice_t *var_decls);

#endif //NATURE_ASSEMBLER_H
