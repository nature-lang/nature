#include "type.h"
#include "value.h"

string type_to_string[] = {
        [TYPE_STRING] = "string",
        [TYPE_BOOL] = "bool",
        [TYPE_FLOAT] = "float",
        [TYPE_INT] = "int",
        [TYPE_VOID] = "void",
        [TYPE_VAR] = "var",
        [TYPE_ANY] = "any",
        [TYPE_STRUCT] = "struct", // ast_struct_decl
        [TYPE_DECL_IDENT] = "decl", // char*
        [TYPE_ARRAY] = "array",
        [TYPE_MAP] = "map", // ast_map_decl
        [TYPE_FN] = "fn",
        [TYPE_POINT] = "point",
        [TYPE_NULL] = "null",
};

uint8_t type_sizeof(type_system t) {
    switch (t) {
        case TYPE_BOOL:
        case TYPE_INT8:
            return 1;
        case TYPE_INT16:
            return 2;
        case TYPE_INT32:
            return 4;
        case TYPE_INT64:
        case TYPE_FLOAT:
            return 8; // 固定大小
            // 随平台不通而不同
        case TYPE_INT:
            return INT_SIZE_BYTE;
        default:
            return POINT_SIZE_BYTE;
    }
}
