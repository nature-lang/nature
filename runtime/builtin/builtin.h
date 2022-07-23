#ifndef NATURE_SRC_LIR_LOWER_BUILTIN_H_
#define NATURE_SRC_LIR_LOWER_BUILTIN_H_

#include "runtime/type/string.h"
#include "src/type.h"

typedef struct {
    type_category type;
    union {
        void *point_value; // 8bit
        float float_value; // 4bit
        bool bool_value; // 1bit
        int64_t int_value; // 8bit
    };
} builtin_operand_t;

void builtin_print(int arg_count, ...);

builtin_operand_t *builtin_new_operand(type_category type, uint8_t *value);

#endif //NATURE_SRC_LIR_LOWER_BUILTIN_H_
