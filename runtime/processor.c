#include "processor.h"

/**
 * 从 current rsp 中截取大概 64KB 作为 process main 的 system_stack, 起点直接从 rsp 开始算
 */
static void processor_main_init(processor_t *p) {
    // 初始化系统栈与上下文
    mmode_t *mode = &p->system_mode;
    mode->stack_base = (addr_t) mode->ctx.uc_stack.ss_sp;
    mode->stack_size = (addr_t) mode->ctx.uc_stack.ss_size; // getcontext 给的 0

    mode = &p->user_mode;
    mode->stack_size = MSTACK_SIZE;
    mode->stack_base = (addr_t) sys_memory_map((void *) 0x4000000000, MSTACK_SIZE);
//    mode->ctx.uc_stack.ss_size = MSTACK_SIZE;
//    mode->ctx.uc_stack.ss_sp = (void *) mode->stack_base;

    // 临时栈
    mode = &p->temp_mode;
    mode->stack_size = MSTACK_SIZE;
    mode->stack_base = (addr_t) sys_memory_map((void *) 0x5000000000, MSTACK_SIZE);
//    mode->ctx.uc_stack.ss_size = MSTACK_SIZE;
//    mode->ctx.uc_stack.ss_sp = (void *) mode->stack_base;
}

void processor_init() {
    processor_list = mallocz(1 * sizeof(processor_t));
    processor_count = 1;
    processor_main_init(&processor_list[0]);
}

processor_t *processor_get() {
//    assertf(processor_count > 0, "processor not init");
    processor_t *result = &processor_list[0];
    return result;
}
