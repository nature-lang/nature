#ifndef NATURE_NATIVE_H
#define NATURE_NATIVE_H

#include <stdlib.h>
#include <stdint.h>
#include "utils/slice.h"
#include "src/lir.h"

typedef struct {
    char *name; // 符号名称
    size_t size; // 符号大小，单位 byte, 生成符号表的时候需要使用
    uint8_t *value; // 符号值
} asm_global_symbol_t;

#define ASM_VAR_DECL_PREFIX "v"
#define ASM_VAR_DECL_UNIQUE_NAME(_module) \
({                                 \
   char *temp_name = malloc(strlen(ASM_VAR_DECL_PREFIX) + sizeof(int) + 2); \
   sprintf(temp_name, "%s_%d", ASM_VAR_DECL_PREFIX, _module->asm_temp_var_decl_count++); \
   temp_name;                                   \
})

void native(closure_t *c);


#endif //NATURE_NATIVE_H
