#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

#define TYPE_NEW_POINT() ast_new_simple_type(TYPE_POINT)
#define TYPE_NEW_INT() ast_new_simple_type(TYPE_INT)

typedef enum {
    TYPE_NULL,
    TYPE_BOOL,
    TYPE_FLOAT,
    TYPE_INT,
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_VOID,
    TYPE_VAR,
    TYPE_STRING,
    TYPE_ANY,
    TYPE_STRUCT, // ast_struct_decl
    TYPE_DECL_IDENT, // char*
    TYPE_LIST,
    TYPE_MAP, // ast_map_decl
    TYPE_FN, // fn<int,void> ast_function_type_decl
    TYPE_POINT,
} type_category;

string type_to_string[UINT8_MAX];

#endif //NATURE_SRC_TYPE_H_
