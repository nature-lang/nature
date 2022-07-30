#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

#define TYPE_NEW_POINT() ast_new_simple_type(TYPE_POINT)
#define TYPE_NEW_NULL() ast_new_simple_type(TYPE_NULL)
#define TYPE_NEW_INT() ast_new_simple_type(TYPE_INT)

#define INT_SIZE_BYTE  8; //  // 根据编译的目标平台不同，可以通过编译参数确定大小
#define POINT_SIZE_BYTE  8;

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

    TYPE_UNKNOWN, // 未推断出类型之前的状态，比如数组

    TYPE_VAR,
    TYPE_STRING,
    TYPE_STRING_RAW, // 不使用 string_t 封装一层
    TYPE_ANY,
    TYPE_STRUCT, // ast_struct_decl
    // 可以理解为自定义类型的关键字，在没有进行类型还原之前，它就是类型！ type foo = int, foo 就是 type_decl_ident
    TYPE_DECL_IDENT,
    TYPE_ARRAY,
    TYPE_MAP, // ast_map_decl
    TYPE_FN, // fn<int,void> ast_function_type_decl
    TYPE_POINT,
} type_system;

string type_to_string[UINT8_MAX];

// 部分类型和平台挂钩，
uint8_t type_sizeof(type_system t);

#endif //NATURE_SRC_TYPE_H_
