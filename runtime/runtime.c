#include "runtime.h"

#include <stdio.h>

#include "nutils/http.h"
#include "runtime/nutils/fn.h"
#include "runtime/nutils/nutils.h"
#include "sysmon.h"

#ifdef __DARWIN
extern void user_main(void) __asm("_main.main");
#else
extern void user_main(void) __asm("main.main");
#endif

/**
 * crt1.o _start -> main  -> entry
 */
int runtime_main(int argc, char *argv[]) {
    // - init log
#ifndef DEBUG_LOG
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
    coroutine_t *main_co = rt_coroutine_new((void *) user_main, FLAG(CO_FLAG_MAIN), NULL, NULL);
    main_coroutine = main_co;
    rt_coroutine_dispatch(main_co);
    RDEBUGF("[runtime_main] main_co dispatch success")

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    wait_sysmond();

    // - wait_sysmon
    sched_run();

    DEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}
