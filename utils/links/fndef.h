#ifndef NATURE_FNDEF_H
#define NATURE_FNDEF_H

#include "utils/value.h"
#include "utils/bitmap.h"

typedef struct {
    addr_t start; // text 虚拟地址起点
    addr_t end; // text 虚拟地址终点
    int stack_offset; // 基于 frame 占用的栈的大小(更加需要的是对象) return addr -> previous rbp -> stack_offset
    bitmap_t *gc_bits; // 标记了栈
} fndef_t;

#endif //NATURE_FNDEF_H
