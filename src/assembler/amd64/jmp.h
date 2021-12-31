#ifndef NATURE_SRC_ASSEMBLER_AMD64_JMP_H_
#define NATURE_SRC_ASSEMBLER_AMD64_JMP_H_

#include "elf.h"
#include "asm.h"

elf_text_item asm_inst_jmp_lower(asm_inst inst);

#endif //NATURE_SRC_ASSEMBLER_AMD64_JMP_H_
