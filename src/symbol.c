#include "symbol.h"

#define BOOL_SIZE_BYTE 1
#define INT_SIZE_BYTE 8
#define FLOAT_SIZE_BYTE 16
#define POINT_SIZE_BYTE 8

void symbol_ident_table_init() {
  symbol_ident_table = table_new();
}

size_t type_sizeof(ast_type type) {
  switch (type.category) {
    case TYPE_BOOL: return BOOL_SIZE_BYTE;
    case TYPE_INT: return INT_SIZE_BYTE;
    case TYPE_FLOAT: return FLOAT_SIZE_BYTE;
    default:return POINT_SIZE_BYTE;
  }
}

/**
 * 默认 struct_decl 已经排序过了
 * @param struct_decl
 * @param property
 * @return
 */
size_t struct_offset(ast_struct_decl *struct_decl, char *property) {
  size_t offset = 0;
  for (int i = 0; i < struct_decl->count; ++i) {
    offset += type_sizeof(struct_decl->list[i].type);
  }
  return offset;
}
