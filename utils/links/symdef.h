#ifndef NATURE_SYMDEF_H
#define NATURE_SYMDEF_H

#include <stdlib.h>
#include "utils/bitmap.h"
#include "src/type.h"

// 由 linker 传递到 runtime,其中最重要的莫过于符号所在的虚拟地址
// 以及符号的内容摘要
typedef struct {
    uint size;
    addr_t base; // data 中的数据对应的虚拟内存中的地址(通常在 .data section 中)
    uint last_ptr_count;
    type_kind_e kind;
    bitmap_t *gc_bits;
} symdef_t;

#endif //NATURE_SYMDEF_H
