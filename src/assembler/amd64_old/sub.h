#ifndef NATURE_SRC_ASSEMBLER_AMD64_SUB_H_
#define NATURE_SRC_ASSEMBLER_AMD64_SUB_H_

#include "elf.h"
#include "asm.h"

/**
 * @return
 */
elf_text_item asm_inst_sub_lower(asm_inst inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_SUB_H_
