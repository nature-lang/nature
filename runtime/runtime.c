#include "runtime.h"

// #include <signal.h>
// #include <unistd.h>
#include <stdio.h>

// #include "processor.h"
#include "runtime/nutils/nutils.h"
#include "sysmon.h"

pthread_mutex_t log_locker;

// 这里直接引用了 main 符号进行调整，ct 不需要在寻找 main 对应到函数位置了
extern int main();

/**
 * crt1.o _start -> runtime_main  -> user_main
 */
int runtime_main(int argc, char *argv[]) {
    // - init log
#ifndef DEBUG
    log_set_level(LOG_FATAL);
#endif

    // - tls 全局变量初始化
    uv_key_create(&tls_processor_key);
    uv_key_create(&tls_coroutine_key);

    // 初始化 aco 需要的 tls 变量(不能再线程中 create)
    uv_key_create(&aco_gtls_co);
    uv_key_create(&aco_gtls_last_word_fp);
    uv_key_create(&aco_gtls_fpucw_mxcsr);

    // - 初始化全局链表分配器
    fixalloc_init(&global_nodealloc, sizeof(rt_linked_node_t));
    pthread_mutex_init(&global_nodealloc_locker, NULL);

    // - read arg
    RDEBUGF("[runtime_main] start, argc=%d, argv=%p", argc, argv);
    command_argc = argc;
    command_argv = argv;

    // - heap memory init
    memory_init();
    RDEBUGF("[runtime_main] memory init success");

    // - coroutine init
    processor_init();
    RDEBUGF("[runtime_main] processor init success");

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = coroutine_new((void *)main, NULL, false, true);
    coroutine_dispatch(main_co);
    RDEBUGF("[runtime_main] main_co dispatch success")

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    wait_sysmon();

    RDEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}
