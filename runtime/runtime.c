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
 * crt1.o _start -> runtime_main  -> user_main
 */
int runtime_main(int argc, char *argv[]) {
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
    coroutine_t *main_co = coroutine_new((void *) main, NULL, false, true);
    coroutine_dispatch(main_co);

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    // 阻塞等待多线程模型执行
    wait_sysmon();

    DEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}
