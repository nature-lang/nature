#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

#define TYPE_NEW_POINT() ast_new_simple_type(TYPE_POINT)
#define TYPE_NEW_NULL() ast_new_simple_type(TYPE_NULL)
#define TYPE_NEW_INT() ast_new_simple_type(TYPE_INT)

typedef enum {
    TYPE_NULL = 1,
    TYPE_BOOL,
    TYPE_FLOAT, // 默认 8bit = double
    TYPE_INT, // 默认 8bit
    TYPE_INT8,
    TYPE_INT16,
    TYPE_INT32,
    TYPE_INT64,
    TYPE_VOID,
    TYPE_VAR,
    TYPE_STRING,
    TYPE_STRING_RAW, // 不使用 string_t 封装一层
    TYPE_ANY,
    TYPE_STRUCT, // ast_struct_decl
    // 可以理解为自定义类型的关键字，在没有进行类型还原之前，它就是类型！ type foo = int, foo 就是 type_decl_ident
    TYPE_DECL_IDENT,
    TYPE_LIST,
    TYPE_MAP, // ast_map_decl
    TYPE_FN, // fn<int,void> ast_function_type_decl
    TYPE_POINT,
} type_category;

string type_to_string[UINT8_MAX];

#endif //NATURE_SRC_TYPE_H_
