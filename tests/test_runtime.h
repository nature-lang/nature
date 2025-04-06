#ifndef NATURE_TEST_RUNTIME_H
#define NATURE_TEST_RUNTIME_H

#include "runtime/memory.h"
#include "runtime/nutils/fn.h"
#include "runtime/processor.h"
#include "runtime/runtime.h"

addr_t rt_fn_main_base;

uint64_t rt_symdef_count;
symdef_t rt_symdef_data;

uint64_t rt_fndef_count;
fndef_t rt_fndef_data; // 仅需要修复一下 gc_bits 数据即可

uint64_t rt_caller_count;
caller_t rt_caller_data; // 仅需要修复一下 gc_bits 数据即可

uint64_t rt_rtype_count;
rtype_t rt_rtype_data;

// Declare entry function with custom assembly name _main.main
#ifdef __DARWIN
__attribute__((used)) void test_main(void) asm("_main.main");
#else
__attribute__((used)) void test_main(void) __asm__("main.main");
#endif

void test_main() {
    printf("hello world in _main\n");
}


static void test_runtime_init() {
    // - heap memory init
    memory_init();

    // - env clsoure
    env_upvalue_table = table_new();
    mutex_init(&env_upvalue_locker, false);
}

static int test_runtime_main(void *main_fn) {
    assert(main_fn);

    // - coroutine init
    sched_init();

    // 可以多次调用，所以调用前需要重置状态
    processor_need_exit = false;

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = rt_coroutine_new(main_fn, FLAG(CO_FLAG_MAIN), NULL, NULL);
    rt_coroutine_dispatch(main_co);

    main_coroutine = main_co;

    sched_run();
    return 0;
}

#endif //NATURE_TEST_RUNTIME_H
