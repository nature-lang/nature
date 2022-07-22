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
        [TYPE_LIST] = "list",
        [TYPE_MAP] = "map", // ast_map_decl
        [TYPE_FN] = "function",
        [TYPE_POINT] = "point",
        [TYPE_NULL] = "null",
};