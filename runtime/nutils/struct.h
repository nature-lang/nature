#ifndef NATURE_STRUCT_H
#define NATURE_STRUCT_H

#include "utils/type.h"
#include "utils/autobuf.h"

n_struct_t *struct_new(uint64_t rtype_hash);

void *struct_access(n_struct_t *s, uint64_t offset);

void struct_assign(n_struct_t *s, uint64_t offset, uint64_t property_index, void *property_ref);

#endif //NATURE_STRUCT_H
