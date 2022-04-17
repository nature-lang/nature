#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

#define TYPE_NEW_POINT() ast_new_simple_type(TYPE_POINT)
#define TYPE_NEW_INT() ast_new_simple_type(TYPE_INT)

typedef enum {
  TYPE_BOOL = 1,
  TYPE_FLOAT,
  TYPE_INT,
  TYPE_VOID,
  TYPE_VAR,
  TYPE_STRING,
  TYPE_ANY,
  TYPE_STRUCT, // ast_struct_decl
  TYPE_DECL_IDENT, // char*
  TYPE_LIST,
  TYPE_MAP, // ast_map_decl
  TYPE_FUNCTION,
  TYPE_POINT,
  TYPE_NULL,
} type_category;

string type_to_string[TYPE_NULL + 1];

#endif //NATURE_SRC_TYPE_H_
