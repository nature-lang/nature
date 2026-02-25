#include "runtime.h"

#include <stdatomic.h>
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

static _Atomic bool fn_mode_inited = false;

/**
 * fn 模式初始化，包含内存、调度器、协程等初始化
 * 内部包含原子锁判断，重复调用安全
 */
void fn_depend_init(bool use_t0) {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&fn_mode_inited, &expected, true)) {
        return; // 已初始化
    }

    RDEBUGF("[fn_depend_init] start");

    // - heap memory init
    memory_init();
    RDEBUGF("[fn_depend_init] memory init success");

    // - env closure
    env_upvalue_table = table_new();
    mutex_init(&env_upvalue_locker, false);

    sched_init(use_t0);
    RDEBUGF("[fn_depend_init] sched init success");

    // register const pool
    register_const_str_pool();

    // 启动 sysmon 进行抢占式调度
    wait_sysmond();
    RDEBUGF("[fn_depend_init] complete");
}

/**
 * crt1.o _start -> main  -> entry
 */
int runtime_main(int argc, char *argv[]) {
    // - read arg
    DEBUGF("[runtime_main] start, argc=%d, argv=%p, main_is_fn %d", argc, argv, user_main_is_fn());
    command_argc = argc;
    command_argv = argv;

    // 两种模式都需要 deserialize
    runtime_deserialize_init();
    RDEBUGF("[runtime_main] deserialize init success");

    if (user_main_is_fn()) {
        // fn 模式：初始化调度器，使用协程运行
        fn_depend_init(true);

        // - 提取 main 进行 coroutine 创建调度
        coroutine_t *main_co = rt_coroutine_new((void *) user_main, FLAG(CO_FLAG_MAIN) | FLAG(CO_FLAG_DIRECT), NULL,
                                                NULL);
        rt_coroutine_dispatch(main_co);
        RDEBUGF("[runtime_main] main_co dispatch success");

        // - sched_run
        sched_run();

        DEBUGF("[runtime_main] fn mode user code run completed, will exit");
    } else {
        // fx 模式：直接调用 user_main，无需 GC 和调度器
        TDEBUGF("[runtime_main] fx mode, calling user_main directly");
        user_main();
        TDEBUGF("[runtime_main] fx mode user code run completed, will exit");
    }

    return 0;
}
