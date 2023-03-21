#ifndef NATURE_RUNTIME_H
#define NATURE_RUNTIME_H

#include "processor.h"

/**
 * 发生了指令跳转，所以有去无回
 * @param addr
 */
static inline void linux_amd64_call(addr_t addr) {
    asm("callq *%[addr]"
            :
            :[addr] "r"(addr));
}

static inline void call_user_main() {
    linux_amd64_call(rt_fn_main_base);
}

void runtime_main();

#endif //NATURE_RUNTIME_H
