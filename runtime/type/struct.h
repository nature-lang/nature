#ifndef NATURE_STRUCT_H
#define NATURE_STRUCT_H

#include "utils/type.h"

memory_struct_t *struct_new(uint64_t rtype_index);

void *struct_access(memory_struct_t *s, uint64_t offset);

void struct_assign(memory_struct_t *s, uint64_t offset, uint64_t property_index, void *property_ref);

#endif //NATURE_STRUCT_H
