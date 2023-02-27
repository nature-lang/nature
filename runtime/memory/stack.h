//
// 用户态将有单独的虚拟栈，避免 runtime 中的 gc 扫描到多余的 runtime fn
// 进入到 runtime 后需要尽快切换到系统栈
//

#ifndef NATURE_STACK_H
#define NATURE_STACK_H

#include <stdlib.h>
#include <stdint.h>
#include "memory.h"

typedef struct {
    addr_t base; // 虚拟其实地址
    uint64_t size; // 栈空间
    addr_t end; // 栈结束地址
    addr_t frame; // BP register
    addr_t top; // SP register
} stack_t;

#endif //NATURE_STACK_H
