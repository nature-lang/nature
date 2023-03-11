#ifndef NATURE_FNDEF_H
#define NATURE_FNDEF_H

#include "utils/value.h"
#include "utils/bitmap.h"

typedef struct {
    addr_t base; // text 虚拟地址起点
    addr_t end; // text 虚拟地址终点
    int stack_offset; // 基于当前函数 frame 占用的栈的大小(主要包括 args 和 locals，不包括 prev rbp 和 return addr)
    bitmap_t *gc_bits; // 标记了栈
} fndef_t;

#endif //NATURE_FNDEF_H
