#ifndef NATURE_ARCH_H
#define NATURE_ARCH_H

#include "amd64.h"
#include <stdlib.h>

int gotplt_entry_type(uint relocate_type);

int got_rel_type(bool is_code_rel);

int8_t is_code_relocate(uint relocate_type);

uint8_t ptr_size();

uint64_t elf_start_addr();

uint64_t elf_page_size();

uint64_t opcode_encodings(elf_context *ctx, slice_t *opcodes);

void relocate(elf_context *l, Elf64_Rela *rel, int type, uint8_t *ptr, addr_t addr, addr_t val);

uint16_t ehdr_machine();

#endif //NATURE_ARCH_H
