#ifndef NATURE_ASSEMBLER_H
#define NATURE_ASSEMBLER_H

#include "src/binary/linker.h"
#include "utils/linked.h"
#include "src/types.h"

/**
 * - 编译指令
 */
void object_file_format(linker_context *ctx);


/**
 * 变量声明编码
 * @param ctx
 * @param asm_symbols
 */
void object_load_symbols(linker_context *ctx, slice_t *asm_symbols);


#endif //NATURE_ASSEMBLER_H
