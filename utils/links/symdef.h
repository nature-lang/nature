#ifndef NATURE_SYMDEF_H
#define NATURE_SYMDEF_H

#include <stdlib.h>
#include "utils/bitmap.h"
#include "utils/type.h"

// 由 linker 传递到 runtime,其中最重要的莫过于符号所在的虚拟地址
// 以及符号的内容摘要
// 同理目前 nature 最大的符号就是 8byte
typedef struct {
    uint64_t size;
    addr_t base; // data 中的数据对应的虚拟内存中的地址(通常在 .data section 中)
    bool need_gc; // 符号和栈中的 var 一样最大的值不会超过 8byte,所以使用 bool 就可以判断了
} symdef_t;

symdef_t *symdef_list;
uint64_t symdef_count;

#endif //NATURE_SYMDEF_H
