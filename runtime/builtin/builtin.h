#ifndef NATURE_SRC_LIR_NATIVE_BUILTIN_H_
#define NATURE_SRC_LIR_NATIVE_BUILTIN_H_

#include "runtime/type/string.h"
#include "src/type.h"

typedef struct {
    type_base_t type;
    union {
        void *point_value; // 8bit
        double float_value; // 8bit
        bool bool_value; // 1bit
        uint64_t int_value; // 8bit
    };
} builtin_operand_t;

void builtin_print(int arg_count, ...);

void builtin_println(int arg_count, ...);

builtin_operand_t *builtin_new_operand(type_base_t type, void *value);

#endif //NATURE_SRC_LIR_NATIVE_BUILTIN_H_
