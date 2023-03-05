#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "stack.h"
#include "allocator.h"

/**
 * linux 线程由自己的系统栈，多个线程就有多个 system stack
 * 由于进入到 runtime 的大多数情况中都需要切换栈区，所以必须知道当前线程的栈区
 * 如果 processor 是第一个线程，那么其 system_stack 和 linux main stack 相同,
 * copy linux stack 时并不需要精准的范围，base 地址直接使用当前 rsp, end 地址使用自定义大小比如 64kb 就行了
 * 关键是切换到 user stack 时记录下 rsp 和 rbp pointer
 */
typedef struct processor_t {
    stack_t user_stack;
    stack_t system_stack;
    mcache_t *mcache;
} processor_t;

uint processor_count; // 逻辑处理器数量,当前版本默认为 1 以单核的方式运行
processor_t *processor_list;

// 第一版只有当前 process_main, 其值将在初始化时进行填充
processor_t *processor_main;

// 第一版总是返回 processor_main
processor_t processor_get();

void processor_init();

void system_stack(processor_t processor);

void user_stack(processor_t processor);

#endif //NATURE_PROCESSOR_H
