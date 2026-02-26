#ifndef NATURE_SRC_SEMANTIC_GENERICS_H_
#define NATURE_SRC_SEMANTIC_GENERICS_H_

#include "src/types.h"


void generics_fn(module_t *m, ast_fndef_t *fndef);

void generics(module_t *m);

bool generics_constraint_deny_type(module_t *m, type_t interface_type, type_t target_type);

void generics_interface_fill_builtin_alloc_types(type_t t);

#endif // NATURE_SRC_SEMANTIC_GENERIC_CHECK_H_
