#ifndef NATURE_FNDEF_H
#define NATURE_FNDEF_H

#include "utils/value.h"
#include "utils/bitmap.h"
#include "utils/type.h"

#define SYMBOL_FNDEF_LIST  "fndef_data"
#define SYMBOL_FNDEF_COUNT "fndef_data_count"
#define SYMBOL_FN_MAIN_BASE "fndef_main_base"

typedef struct {
    addr_t base; // text 虚拟地址起点
    addr_t end; // text 虚拟地址终点
    int64_t stack_size; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    uint8_t *gc_bits; // 基于 stack_size 计算出的 gc_bits
} fndef_t;

// 主要是需要处理 gc_bits 数据
byte *fndefs_serialize(fndef_t *fndef, uint64_t *count);

fndef_t *fndefs_deserialize(byte *data, uint64_t *count);


#endif //NATURE_FNDEF_H
