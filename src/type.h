#ifndef NATURE_SRC_TYPE_H_
#define NATURE_SRC_TYPE_H_

#include "value.h"

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

    TYPE_VOID, // 仅用于函数无返回值
    TYPE_ANY, // any a = 1 可以，但是 int a = any 不行

    TYPE_UNKNOWN, // 未推断出类型之前的状态，比如数组, 又比如 var a; 此时 a 的类型就是 unknown

    TYPE_BUILTIN_ANY, // runtime/builtin 函数推导时使用，能够接受或者赋值给任意 nature type

    TYPE_STRING,
    TYPE_STRING_RAW, // 不使用 string_t 封装一层
    TYPE_STRUCT, // ast_struct_decl
    // 可以理解为自定义类型的关键字，在没有进行类型还原之前，它就是类型！ type foo = int, foo 就是 type_decl_ident
    TYPE_DECL_IDENT,
    TYPE_ARRAY,
    TYPE_MAP, // ast_map_decl
    TYPE_FN, // fn<int,void> ast_function_type_decl
    TYPE_POINT,
} type_base_t;

typedef struct {
    void *value; // ast_ident(type_decl_ident),ast_map_decl*....
    type_base_t base; // type_fn,type_int
    bool is_origin; // type a = int, type b = a，int is origin
    uint8_t point; // 指针等级, 如果等于0 表示非指针
} type_t;


typedef struct {
    type_t return_type; // 基础类型 + 动态类型
    type_t formal_param_types[UINT8_MAX];
    uint8_t formal_param_count;
} type_fn_t;


string type_to_string[UINT8_MAX];

uint8_t type_base_sizeof(type_base_t t);

type_t type_new_point(type_t ast_type, uint8_t point);

type_t type_new_base(type_base_t type);

//type_t type_builtin_println();

type_t type_runtime_array_push();

#endif //NATURE_SRC_TYPE_H_
