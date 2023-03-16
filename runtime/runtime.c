#include "runtime.h"
#include "processor.h"

/**
 * crt1.o _start -> main  -> user_main
 */
int runtime_main() {
    // - processor 初始化(包括当前执行栈记录,用户栈生成)
    processor_init();

    // TODO - 将系统参数传递给用户程序
    // - 堆内存管理初始化
    memory_init();

    // - 初始化 stack return addr 为 main
    processor_t p = processor_get();

    user_stack(p);

    // to user main, 这里已经发出了指令跳转
    call_user_main();

    printf("hello world");
}
