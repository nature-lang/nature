#ifndef NATURE_ARCH_H
#define NATURE_ARCH_H

#include "x86_64.h"
#include <stdlib.h>

typedef enum {
    ARCH_X86_64 = 1, // 对应 ARCH_AMD64
} arch_e;

arch_e arch;

int gotplt_entry_type(uint relocate_type);

int got_rel_type(bool is_code_rel);

int8_t is_code_relocate(uint relocate_type);

uint8_t ptr_size();

uint64_t elf_start_addr();

uint64_t elf_page_size();

void relocate(linker_t *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val);

#endif //NATURE_ARCH_H
