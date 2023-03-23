#include "runtime.h"

extern void main();

/**
 * crt1.o _start -> main  -> user_main
 */
void runtime_main() {
    // - processor 初始化(包括当前执行栈记录,用户栈生成)
    processor_init();

    // - 堆内存管理初始化
    memory_init();

    // - 初始化 stack return addr 为 main
    processor_t *p = processor_get();

    // 切换到用户栈并执行目标函数(寄存器等旧数据会存到 p->system_mode)
    MODE_CALL(p->user_mode, p->system_mode, main);

    printf("user code run completed,will exit");
}
