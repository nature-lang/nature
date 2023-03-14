#ifndef NATURE_FNDEF_H
#define NATURE_FNDEF_H

#include "utils/value.h"
#include "utils/bitmap.h"
#include "utils/type.h"

typedef struct {
    addr_t base; // text 虚拟地址起点
    addr_t end; // text 虚拟地址终点
    int64_t stack_size; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    bitmap_t *gc_bits; // 基于 stack_size 计算出的 gc_bits
} fndef_t;

byte *fndefs_serialize(fndef_t *fndef, uint64_t *count);

fndef_t *fndefs_deserialize(byte *data, uint64_t *count);


#endif //NATURE_FNDEF_H
