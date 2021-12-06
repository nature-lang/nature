#ifndef NATURE_SRC_ASSEMBLER_AMD64_MOV_H_
#define NATURE_SRC_ASSEMBLER_AMD64_MOV_H_
#include "elf.h"
#include "asm.h"

/**
 * @param mov_inst
 * @return
 */
elf_text_item asm_inst_mov_lower(asm_inst mov_inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_MOV_H_
