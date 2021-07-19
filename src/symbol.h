#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include "src/value.h"
#include "src/lib/table.h"
#include <stdlib.h>
#include "ast.h"

table *symbol_ident_table; // analysis_local_ident

typedef enum {
  SYMBOL_TYPE_VAR,
  SYMBOL_TYPE_CUSTOM_TYPE,
//  SYMBOL_TYPE_FUNCTION,
} symbol_type;

int64_t list_offset(string type, uint64_t index);

uint64_t type_sizeof(string type);

void symbol_table_init();

void *symbol_get_type(string unique_ident);

ast_struct_decl *symbol_struct(string struct_ident);

#endif //NATURE_SRC_AST_SYMBOL_H_
