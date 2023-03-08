#ifndef NATURE_PROCESSOR_H
#define NATURE_PROCESSOR_H

#include "memory.h"
#include "allocator.h"

uint processor_count; // 逻辑处理器数量,当前版本默认为 1 以单核的方式运行
processor_t *processor_list;

// 第一版只有当前 process_main, 其值将在初始化时进行填充
processor_t processor_main;

// 第一版总是返回 processor_main
processor_t processor_get();

void processor_init();

void system_stack(processor_t processor);

void user_stack(processor_t processor);

#endif //NATURE_PROCESSOR_H
