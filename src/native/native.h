#ifndef NATURE_NATIVE_H
#define NATURE_NATIVE_H

#include <stdlib.h>
#include <stdint.h>
#include "utils/slice.h"
#include "src/lir/lir.h"

typedef struct {
    char *name; // 符号名称
    size_t size; // 符号大小，单位 byte, 生成符号表的时候需要使用
    uint8_t *value; // 符号值
} asm_var_decl_t;

slice_t *asm_var_decls; // 服务于 .data 和 symbol 段

int native_decl_unique_count;

#define NATIVE_VAR_DECL_PREFIX "v"

#define NATIVE_VAR_DECL_UNIQUE_NAME() \
({                                 \
   char *temp_name = malloc(strlen(NATIVE_VAR_DECL_PREFIX) + sizeof(int) + 2); \
   sprintf(temp_name, "%s_%d", NATIVE_VAR_DECL_PREFIX, native_decl_unique_count++); \
   temp_name;                                   \
})

void native_init();

void native(closure_t *c);


#endif //NATURE_NATIVE_H
