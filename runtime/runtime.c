#include "runtime.h"

// #include <signal.h>
// #include <unistd.h>
#include <stdio.h>

// #include "processor.h"
#include "runtime/nutils/nutils.h"
#include "sysmon.h"

// 这里直接引用了 main 符号进行调整，ct 不需要在寻找 main 对应到函数位置了
extern int main();

/**
 * 如果所有的共享协程都已退出，则 runtime 退出
 */
static void wait_processor() {
    bool all_exit;
    while (true) {
        all_exit = true;
        SLICE_FOR(share_processor_list) {
            processor_t *p = SLICE_VALUE(share_processor_list);
            if (p->exit == false) {
                all_exit = false;
            }
        }

        if (all_exit) {
            break;
        }

        uv_sleep(20);
    }

    DEBUGF("[wait_processor] all processor thread exit");
}

/**
 * crt1.o _start -> runtime_main  -> user_main
 */
void runtime_main(int argc, char *argv[]) {
    // - read arg
    DEBUGF("[runtime_main] start, argc=%d, argv=%p", argc, argv);
    command_argc = argc;
    command_argv = argv;

    // - heap memory init
    memory_init();
    DEBUGF("[runtime_main] memory init success")

    // - coroutine init
    processor_init();
    DEBUGF("[runtime_main] processor init success");

    // - 提取 main 进行 coroutine 创建调度
    coroutine_t *main_co = coroutine_new((void *)main, NULL, false, true);
    coroutine_dispatch(main_co);

    // 开启调度 GC 监控线程(单独开一个线程进行监控)
    // 启用一个新的线程来运行 sysmon_run
    uv_thread_t sysmon_thread_id;
    uv_thread_create(&sysmon_thread_id, sysmon_run, NULL);
    // 阻塞等待多线程模型执行
    wait_processor();

    DEBUGF("[runtime_main] user code run completed,will exit");
}
