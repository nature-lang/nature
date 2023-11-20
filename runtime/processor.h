#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "nutils/errort.h"
#include <stdio.h>
#include <stdint.h>

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
    DEBUGF("\nFN: %s", __func__);                   \
    DEBUGF("user_stack:  base=0x%lx, size=0x%lx, ctx_sp=%p, bp=0x%lx", _s.stack_base, _s.stack_size, _s.ctx.uc_stack.ss_sp, extract_frame_base(_s)); \
    _s = _p->temp_mode; \
    DEBUGF("temp_stack:  base=0x%lx, size=0x%lx, ctx_sp=%p, bp=0x%lx", _s.stack_base, _s.stack_size, _s.ctx.uc_stack.ss_sp, extract_frame_base(_s)); \
    DEBUGF("actual:  top=%lx, frame=%lx\n", STACK_TOP(), STACK_FRAME_BASE());                                    \
} \

#define MODE_CALL(_new, _old, _fn) \
    getcontext(&_new.ctx);                                 \
    _new.ctx.uc_stack.ss_sp = (void *) _new.stack_base; \
    _new.ctx.uc_stack.ss_size = _new.stack_size; \
    _new.ctx.uc_link = &_old.ctx; \
    makecontext(&_new.ctx, _fn, 0); \
    swapcontext(&_old.ctx, &_new.ctx);                     \

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

void processor_init();

void rt_processor_attach_errort(char *msg);

void processor_dump_errort(n_errort *errort);

#endif //NATURE_PROCESSOR_H
