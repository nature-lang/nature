#ifndef NATURE_X86_64_H
#define NATURE_X86_64_H

#include "linker.h"
#include <stdlib.h>

int x86_64_gotplt_entry_type(uint relocate_type);

uint x86_64_create_plt_entry(linker_t *l, uint got_offset, sym_attr_t *attr);

int8_t x86_64_is_code_relocate(uint relocate_type);

#endif //NATURE_X86_64_H
