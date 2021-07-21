#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

typedef enum {
  TYPE_STRING,
  TYPE_BOOL,
  TYPE_FLOAT,
  TYPE_INT,
  TYPE_VOID,
  TYPE_VAR,
  TYPE_ANY,
  TYPE_NULL,
  TYPE_STRUCT, // ast_struct_decl
  TYPE_DECL_IDENT, // char*
  TYPE_LIST,
  TYPE_MAP, // ast_map_decl
  TYPE_FUNCTION,
} type_category;

string type_to_string[TYPE_FUNCTION+1];

#endif //NATURE_SRC_TYPE_H_
