#ifndef NATURE_SRC_AST_SYMBOL_H_
#define NATURE_SRC_AST_SYMBOL_H_

#include <stdlib.h>
#include "src/value.h"
#include "src/lib/table.h"
#include "ast.h"

table *symbol_ident_table; // analysis_local_ident

typedef enum {
  SYMBOL_TYPE_VAR,
  SYMBOL_TYPE_CUSTOM_TYPE,
//  SYMBOL_TYPE_FUNCTION,
} symbol_type;

int64_t list_offset(ast_type type, uint64_t index);

size_t struct_offset(ast_struct_decl *struct_decl, string property);

/**
 * 单位 byte
 * 字符串类型，list 类型， map 类型,都按照指针来计算大小
 * @param type
 * @return
 */
size_t type_sizeof(ast_type type);

void symbol_ident_table_init();

#endif //NATURE_SRC_AST_SYMBOL_H_
