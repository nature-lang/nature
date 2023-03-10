#include "processor.h"

/**
 * 从 current rsp 中截取大概 64KB 作为 process main 的 system_stack, 起点直接从 rsp 开始算
 */
static void processor_main_init(processor_t *p) {
    // 初始化系统栈
    stack_t *s = &p->system_stack;
    s->base = get_stack_top();
    s->size = P_MAIN_SYSTEM_STACK_SIZE;
    s->end = s->base - s->size;
    // rbp 是一条线，其没有值,内存访问时是从低地址到高地址
    // 所以 *rbp 访问的是 rbp ~ rbp + 8 这个范围内的值
    s->frame_base = s->base - 16;
    s->top = s->base - 1024;

    // 初始化用户栈 ?? user main 的空间是怎么样的？首次调用时其实应该一 call 的方式调用过去，而不是 ret 的方式？
}

void processor_init() {
    processor_list = mallocz(1 * sizeof(processor_t));
    processor_count = 1;
    processor_main_init(&processor_list[0]);
}