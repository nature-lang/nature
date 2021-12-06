#ifndef NATURE_SRC_ASSEMBLER_AMD64_ADD_H_
#define NATURE_SRC_ASSEMBLER_AMD64_ADD_H_

#include "elf.h"
#include "asm.h"

/**
 * @param mov_inst
 * @return
 */
elf_text_item asm_inst_add_lower(asm_inst mov_inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_ADD_H_
