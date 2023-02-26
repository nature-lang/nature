#ifndef NATURE_TYPEDEF_H
#define NATURE_TYPEDEF_H

#include <stdlib.h>
#include "utils/bitmap.h"
#include "src/type.h"

/**
 * 无论是标量类型还是 struct， 又或者是 array 和 map 都可以使用该结构体进行描述。
 * struct 的 size 应该是其对齐后的 size
 * array 的 size 应该是 包含 len 和 cap 的 size
 */
typedef struct {
    uint size;
    uint last_ptr_count; // 最后一个包含指针的字节数，默认为 0
    type_kind_e kind;
    bitmap_t *gc_bits; // 1bit 标记 8byte 是否为指针数据
} typedef_t;

#endif //NATURE_TYPEDEF_H
