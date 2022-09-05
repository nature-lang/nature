#ifndef NATURE_X86_64_H
#define NATURE_X86_64_H

#include "linker.h"
#include <stdlib.h>

#define X86_64_ELF_START_ADDR 0x400000
#define X86_64_ELF_PAGE_SIZE 0x200000
#define X86_64_PTR_SIZE 8 // 单位 byte

int x86_64_gotplt_entry_type(uint relocate_type);

uint x86_64_create_plt_entry(linker_t *l, uint got_offset, sym_attr_t *attr);

int8_t x86_64_is_code_relocate(uint relocate_type);

void x86_64_relocate(linker_t *l, Elf64_Rela *rel, int type, uint8_t* ptr, addr_t addr, addr_t val);

#endif //NATURE_X86_64_H
