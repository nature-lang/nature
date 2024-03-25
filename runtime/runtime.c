#include "runtime.h"

#include <stdio.h>

#include "runtime/nutils/fn.h"
#include "runtime/nutils/nutils.h"
#include "sysmon.h"

fixalloc_t global_nodealloc;
pthread_mutex_t global_nodealloc_locker;

fixalloc_t mutex_global_nodealloc;
pthread_mutex_t mutex_global_nodealloc_locker;

extern void user_main();
/**
 * crt1.o _start -> runtime_main  -> user_main
 */
int runtime_main(int argc, char *argv[]) {
    // - init log
#ifndef NATURE_DEBUG
    log_set_level(LOG_FATAL);
#endif
    // - 初始化 runtime 全局链表分配器
    fixalloc_init(&global_nodealloc, sizeof(rt_linked_node_t));
    pthread_mutex_init(&global_nodealloc_locker, NULL);

    fixalloc_init(&mutex_global_nodealloc, sizeof(rt_linked_node_t));
    pthread_mutex_init(&mutex_global_nodealloc_locker, NULL);

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

    // - coroutine init
    processor_init();
    RDEBUGF("[runtime_main] processor init success");

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = rt_coroutine_new((void *) user_main, FLAG(CO_FLAG_MAIN), 0);
    rt_coroutine_dispatch(main_co);
    RDEBUGF("[runtime_main] main_co dispatch success")

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    wait_sysmon();

    DEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}
