#include "type.h"
#include "utils/value.h"

string type_to_string[] = {
        [TYPE_STRING] = "string",
        [TYPE_STRING_RAW] = "string_RAW",
        [TYPE_BOOL] = "bool",
        [TYPE_FLOAT] = "float",
        [TYPE_INT] = "int",
        [TYPE_VOID] = "void",
        [TYPE_UNKNOWN] = "unknown",
//        [TYPE_BUILTIN_ANY] = "builtin_any",
        [TYPE_ANY] = "any",
        [TYPE_STRUCT] = "struct", // ast_struct_decl
        [TYPE_DECL_IDENT] = "decl", // char*
        [TYPE_ARRAY] = "array",
        [TYPE_MAP] = "map", // ast_map_decl
        [TYPE_FN] = "fn",
        [TYPE_POINT] = "point",
        [TYPE_NULL] = "null",
};

uint8_t type_base_sizeof(type_kind_e t) {
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

type_t type_base_new(type_kind_e type) {
    type_t result = {
            .is_origin = true,
            .value = NULL,
            .kind = type
    };
    return result;
}

type_t type_new_point(type_t ast_type, uint8_t point) {
    type_t result;
    result.is_origin = ast_type.is_origin;
    result.kind = ast_type.kind;
    result.value = ast_type.value;
    result.point = point;
    return result;
}
