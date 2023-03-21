#include "runtime.h"

/**
 * crt1.o _start -> main  -> user_main
 */
void runtime_main() {
    // - processor 初始化(包括当前执行栈记录,用户栈生成)
    processor_init();

    // TODO - 将系统参数传递给用户程序
    // - 堆内存管理初始化
    memory_init();

    // - 初始化 stack return addr 为 main
    processor_t *p = processor_get();

    USER_STACK(p);
    // 切换 user stack 后必须立刻进行跳转指令或者 ret 指令
    // to user main, 这里发出了指令跳转
    call_user_main();

    printf("hello world");
}
