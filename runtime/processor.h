#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "allocator.h"
#include <stdio.h>

#define STACK_TOP() ({\
    uint64_t addr = 0; \
    __asm__ __volatile__("movq %%rsp, %[addr]" :  [addr] "=r"(addr) : ); \
    addr;})           \

#define STACK_FRAME_BASE() ({\
    uint64_t addr = 0; \
    __asm__ __volatile__("movq %%rbp, %[addr]" :  [addr] "=r"(addr) : ); \
    addr;})\

#define DEBUG_STACK()  {\
    processor_t *_p = processor_get(); \
    mmode_t _s = _p->user_mode;      \
    DEBUGF("FN: %s", __func__);                   \
    DEBUGF("user_stack:  base=%lx, size=%lx, ctx_sp=%p", _s.stack_base, _s.stack_size, _s.ctx.uc_stack.ss_sp); \
    _s = _p->system_mode; \
    DEBUGF("user_stack:  base=%lx, size=%lx, ctx_sp=%p", _s.stack_base, _s.stack_size, _s.ctx.uc_stack.ss_sp); \
    DEBUGF("actual:  top=%lx, frame=%lx", STACK_TOP(), STACK_FRAME_BASE());                                    \
} \

#define MODE_CALL(_new, _old, _fn) \
    getcontext(&_new.ctx);                                 \
    _new.ctx.uc_stack.ss_sp = (void *) _new.stack_base; \
    _new.ctx.uc_stack.ss_size = _new.stack_size; \
    _new.ctx.uc_link = &_old.ctx; \
    makecontext(&_new.ctx, _fn, 0); \
    swapcontext(&_old.ctx, &_new.ctx);\


uint processor_count; // 逻辑处理器数量,当前版本默认为 1 以单核的方式运行
processor_t *processor_list;

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

void processor_init();


#endif //NATURE_PROCESSOR_H
