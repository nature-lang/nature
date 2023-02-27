#ifndef NATURE_SYMDEF_H
#define NATURE_SYMDEF_H

#include <stdlib.h>
#include "utils/bitmap.h"
#include "src/type.h"

// 由 linker 传递到 runtime,其中最重要的莫过于符号所在的虚拟地址
// 以及符号的内容摘要
typedef struct {
    uint size;
    uint64_t addr;
    uint last_ptr_count;
    type_kind_e kind;
    bitmap_t *gc_bits;
} symdef_t;

#endif //NATURE_SYMDEF_H
