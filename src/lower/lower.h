#ifndef NATURE_LOWER_H
#define NATURE_LOWER_H

#include <stdlib.h>
#include <stdint.h>
#include "utils/slice.h"

typedef struct {
    char *name; // 符号名称
    size_t size; // 符号大小，单位 byte, 生成符号表的时候需要使用
    uint8_t *value; // 符号值
} lower_var_decl_t;

slice_t *lower_var_decls;

int lower_decl_temp_unique_count;

#define LOWER_VAR_DECL_PREFIX "v"


#define LOWER_VAR_DECL_UNIQUE_NAME() \
({                                 \
   char *temp_name = malloc(strlen(LOWER_VAR_DECL_PREFIX) + sizeof(int) + 2); \
   sprintf(temp_name, "%s_%d", LOWER_VAR_DECL_PREFIX, lower_decl_temp_unique_count++); \
   temp_name;                                   \
})


#endif //NATURE_LOWER_H
