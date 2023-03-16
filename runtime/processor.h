#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "allocator.h"

#define MAIN_SYSTEM_STACK_SIZE (64 * 1024)

uint processor_count; // 逻辑处理器数量,当前版本默认为 1 以单核的方式运行
processor_t *processor_list;

/**
 * TODO 架构兼容
 * @return
 */
static addr_t get_stack_top() {
    uint64_t addr;
    // 暂存 rsp 的值
    asm("movq %%rsp, %[addr]"
            :  [addr] "=r"(addr)// output
    :  // input
    );

    return addr;
}

inline void linux_amd64_store_stack(mstack_t *stack) {
    // 暂存 rsp 的值
    asm("movq %%rsp, %[addr]"
            :  [addr] "=r"(stack->top)// output
    :  // input
    );

    // 暂存 rbp 的值
    asm("movq %%rbp, %[addr]"
            :  [addr] "=r"(stack->frame_base)// output
    :  // input
    );
}

inline void linux_amd64_restore_stack(mstack_t stack) {
    // 修改 rsp 的值
    asm("movq %[addr], %%rsp"
            : // output
            : [addr] "r"(stack.top)// input
    );

    // 修改 rbp 的值
    asm("movq %[addr], %%rbp"
            : // output
            : [addr] "r"(stack.frame_base)// input
    );
}


/**
 *  暂存用户栈，并切换到系统栈,切换到系统栈的什么位置其实并不重要，只要是在系数栈中即可
 *  因为最终都会回到 user_stack 中。
 *  so ~ 在最开始的初始化时，为 system_stack 初始化一个差不多的起点就行了。
 *  但是需要注意的时，此处修改逻辑不能嵌套的太深，否则会造成多次回退，最终回不到想要的地方
 *  所以这里统一采用内联函数的方式实现,避免 ret 指令的调用导致栈异常
 */
inline void linux_amd64_system_stack(processor_t p) {
    // 暂存当前的 rsp 和 rbp 的值到 user_stack 中
    linux_amd64_store_stack(&p.user_stack);

    linux_amd64_restore_stack(p.system_stack);
}

inline void system_stack(processor_t p) {
    linux_amd64_system_stack(p);
}

/**
 * 该函数由于修改了 rbp 所以到该函数返回时会直接返回到用户态
 * 切换到 user_stack
 * @param processor
 */
inline void linux_amd64_user_stack(processor_t p) {
    linux_amd64_restore_stack(p.user_stack);
}

/**
 * TODO 架构相关
 * 在 amd64 中，不能直接使用 mov 为 rip 寄存器赋值，要改变 rip 必须使用 jmp 或 call, 或 ret 指令
 * return addr 保存在当前 stack 中，所以只需要改动 rsp 和 rbp 执行的内存地址即可，此时调用 return
 * 将会自动回到用户地址中，不需要再操作 rip 指针
 * @param processor
 */
inline void user_stack(processor_t p) {
    linux_amd64_user_stack(p);
}

/**
 * 正常需要根据线程 id 返回，第一版返回 id 就行了
 * 第一版总是返回 processor_main
 * @return
 */
processor_t processor_get();

void processor_init();

#endif //NATURE_PROCESSOR_H
