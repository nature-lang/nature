#ifndef NATURE_SRC_SEMANTIC_INTERFACE_H_
#define NATURE_SRC_SEMANTIC_INTERFACE_H_

#include "src/ast.h"


void interface(module_t *m);

bool check_impl_interface_contains(module_t *m, ast_typedef_stmt_t *stmt, type_t *find_target_interface);

#endif // NATURE_SRC_SEMANTIC_INTERFACE_H_
