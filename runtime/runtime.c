#include "runtime.h"

#include <stdio.h>

#include "runtime/nutils/fn.h"
#include "runtime/nutils/nutils.h"
#include "sysmon.h"

// 用于链接链接全局 _main
extern int entry();

/**
 * crt1.o _start -> main  -> entry
 */
int main(int argc, char *argv[]) {
    // - init log
#ifndef NATURE_DEBUG
    log_set_level(LOG_FATAL);
#endif
    // - read arg
    RDEBUGF("[runtime_main] start, argc=%d, argv=%p", argc, argv);
    command_argc = argc;
    command_argv = argv;

    // - heap memory init
    memory_init();
    RDEBUGF("[runtime_main] memory init success");

    // - env clsoure
    env_upvalue_table = table_new();
    mutex_init(&env_upvalue_locker, false);

    sched_init();
    RDEBUGF("[runtime_main] processor init success");

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = rt_coroutine_new((void *) entry, FLAG(CO_FLAG_MAIN), 0);
    rt_coroutine_dispatch(main_co);
    RDEBUGF("[runtime_main] main_co dispatch success")

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    wait_sysmon();

    DEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}

void test_runtime_init() {
    // - heap memory init
    memory_init();

    // - env clsoure
    env_upvalue_table = table_new();
    mutex_init(&env_upvalue_locker, false);
}

int test_runtime_main(void *main_fn) {
    assert(main_fn);

    // - coroutine init
    sched_init();

    // 可以多次调用，所以调用前需要重置状态
    processor_need_exit = false;

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = rt_coroutine_new(main_fn, FLAG(CO_FLAG_MAIN), 0);
    rt_coroutine_dispatch(main_co);

    while (true) {
        if (processor_get_exit()) {
            DEBUGF("[wait_sysmon] processor need exit, will exit");
            usleep(WAIT_LONG_TIME * 1000);
            break;
        }
        usleep(WAIT_SHORT_TIME * 1000);// 5ms
    }

    return 0;
}
