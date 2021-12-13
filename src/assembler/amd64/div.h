#ifndef NATURE_SRC_ASSEMBLER_AMD64_DIV_H_
#define NATURE_SRC_ASSEMBLER_AMD64_DIV_H_

#include "elf.h"
#include "asm.h"

/**
 * @return
 */
elf_text_item asm_inst_div_lower(asm_inst inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_DIV_H_
