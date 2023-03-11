#include "processor.h"

/**
 * 从 current rsp 中截取大概 64KB 作为 process main 的 system_stack, 起点直接从 rsp 开始算
 */
static void processor_main_init(processor_t *p) {
    // 初始化系统栈
    mstack_t *s = &p->system_stack;
    s->size = MAIN_SYSTEM_STACK_SIZE;
    s->base = get_stack_top();
    s->end = s->base - s->size;
    // rbp 是一条线，其没有值,内存访问时是从低地址到高地址
    // 所以 *rbp 访问的是 rbp ~ rbp + 8 这个范围内的值
    s->frame_base = s->base - 16;
    s->top = s->base - 1024;

    s = &p->user_stack;
    s->size = MSTACK_SIZE;
    s->base = mstack_new(s->size);
    s->end = s->base + s->size;
    s->frame_base = s->base;
    s->top = s->base;
}

void processor_init() {
    processor_list = mallocz(1 * sizeof(processor_t));
    processor_count = 1;
    processor_main_init(&processor_list[0]);
}

processor_t processor_get() {
    processor_t result = processor_list[0];
    return result;
}
