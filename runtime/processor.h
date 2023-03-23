#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "allocator.h"
#include <stdio.h>

#define DEBUG_STACK() { \
    processor_t *_p = processor_get(); \
    mstack_t _s = _p->user_stack;      \
    DEBUGF("FN: %s", __func__);                   \
    DEBUGF("user_stack:  base=%lx, end=%lx, top=%lx, frame=%lx", _s.base, _s.end, _s.top, _s.frame_base); \
    _s = _p->system_stack; \
    DEBUGF("system_stack:  base=%lx, end=%lx, top=%lx, frame=%lx", _s.base, _s.end, _s.top, _s.frame_base); \
    DEBUGF("actual:  top=%lx, frame=%lx", STACK_TOP(), STACK_FRAME_BASE()); \
}                             \


#define MAIN_SYSTEM_STACK_SIZE (64 * 1024)

// TODO linux amd64
#define STORE_STACK(_stack)  \
    __asm__ __volatile__("movq %%rsp, %[addr]":  [addr] "=r"((_stack)->top) :); \
    __asm__ __volatile__("movq %%rbp, %[addr]" :  [addr] "=r"((_stack)->frame_base) :);

#define RESTORE_STACK(_stack) \
    __asm__ __volatile__("movq %[addr], %%rsp": : [addr] "r"((_stack).top)); \
    __asm__ __volatile__("movq %[addr], %%rbp" : : [addr] "r"((_stack).frame_base)); \

#define STACK_TOP() ({\
    uint64_t addr = 0; \
    __asm__ __volatile__("movq %%rsp, %[addr]" :  [addr] "=r"(addr) : ); \
    addr;})           \

#define STACK_FRAME_BASE() ({\
    uint64_t addr = 0; \
    __asm__ __volatile__("movq %%rbp, %[addr]" :  [addr] "=r"(addr) : ); \
    addr;})\

#define SYSTEM_STACK(_p) \
    STORE_STACK(&_p->user_stack) \
    RESTORE_STACK(_p->system_stack)\

#define USER_STACK(_p) \
    RESTORE_STACK(_p->user_stack) \



uint processor_count; // 逻辑处理器数量,当前版本默认为 1 以单核的方式运行
processor_t *processor_list;


static void system_stack_init(processor_t *p) {
    // 初始化系统栈
    mstack_t *s = &p->system_stack;
    // 实时读取当前的 rsp 作为 base 就行了， system_stack 是非常无关紧要的 stack, 并预留一定的空间,方便测试时调用
    s->size = MAIN_SYSTEM_STACK_SIZE;
    s->base = STACK_TOP() - MAIN_SYSTEM_STACK_SIZE * 2; // 预留 128K 空间，给测试使用，避免切换到 system stack 时造成污染
    s->end = s->base - s->size;
    // rbp 是一条线，其没有值,内存访问时是从低地址到高地址
    // 所以 *rbp 访问的是 rbp ~ rbp + 8 这个范围内的值
    s->frame_base = s->base - 16;
    s->top = s->base - 1024;
}

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t *processor_get();

void processor_init();


#endif //NATURE_PROCESSOR_H
