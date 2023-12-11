#include "runtime.h"

#include <signal.h>
#include <unistd.h>

#include "processor.h"
#include "runtime/nutils/basic.h"
#include "sysmon.h"

// 这里直接引用了 main 符号进行调整，ct 不需要在寻找 main 对应到函数位置了
extern int main();

static void wait_processor() {
    bool change;
    while (true) {
        change = false;
        SLICE_FOR(share_processor_list) {
            processor_t *p = SLICE_VALUE(share_processor_list);
            uv_thread_join(&p->thread_id);
            change = true;
        }

        if (!change) {
            break;
        }
    }
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
    // Monitor thread
    processor_init();

    // - 提取 main 进行 coroutine 创建调度
    coroutine_dispatch(coroutine_new((void *)runtime_main, NULL, false));

    // 开启调度 GC 监控线程(单独开一个线程进行监控)
    sysmon_run();

    // 阻塞等待多线程模型执行
    wait_processor();

    DEBUGF("[runtime_main] user code run completed,will exit");
}
