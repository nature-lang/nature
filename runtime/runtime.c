#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __WINDOWS
extern wchar_t **CommandLineToArgvW(const wchar_t *command_line,
                                    int *argument_count);
#endif

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
    // - read arg
    DEBUGF("[runtime_main] start, argc=%d, argv=%p", argc, argv);
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

    // register const pool
    register_const_str_pool();

    // - 提取 main 进行 coroutine 创建调度，需要等待 processor init 加载完成
    coroutine_t *main_co = rt_coroutine_new((void *) user_main, FLAG(CO_FLAG_MAIN) | FLAG(CO_FLAG_DIRECT), NULL, NULL);
    rt_coroutine_dispatch(main_co);
    RDEBUGF("[runtime_main] main_co dispatch success")

    // 等待 processor init 注册完成运行后再启动 sysmon 进行抢占式调度
    wait_sysmond();

    // - wait_sysmon
    sched_run();

    DEBUGF("[runtime_main] user code run completed,will exit");

    return 0;
}

#ifdef __WINDOWS
static void windows_free_utf8_argv(char **argv, int argc) {
    if (argv == NULL) {
        return;
    }
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
}

static char **windows_utf8_argv(int *argc_out) {
    int wide_argc = 0;
    wchar_t **wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == NULL || wide_argc < 0) {
        return NULL;
    }

    char **utf8_argv = calloc((size_t) wide_argc + 1U, sizeof(char *));
    if (utf8_argv == NULL) {
        LocalFree(wide_argv);
        return NULL;
    }

    for (int i = 0; i < wide_argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, NULL,
                                        0, NULL, NULL);
        if (bytes <= 0) {
            windows_free_utf8_argv(utf8_argv, i);
            LocalFree(wide_argv);
            return NULL;
        }
        utf8_argv[i] = malloc((size_t) bytes);
        if (utf8_argv[i] == NULL ||
            WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, utf8_argv[i],
                                bytes, NULL, NULL) != bytes) {
            windows_free_utf8_argv(utf8_argv, i + 1);
            LocalFree(wide_argv);
            return NULL;
        }
    }

    LocalFree(wide_argv);
    *argc_out = wide_argc;
    return utf8_argv;
}

/**
 * `crt2.obj` owns mainCRTStartup and performs UCRT process initialization,
 * wildcard setup and termination. The UCRT narrow argv follows the active
 * ANSI code page, so rebuild argv from the UTF-16 command line before handing
 * it to Nature, whose strings use UTF-8.
 */
int main(int argc, char *argv[]) {
    rt_install_windows_exception_handler();

    int utf8_argc = 0;
    char **utf8_argv = windows_utf8_argv(&utf8_argc);
    if (utf8_argv == NULL) {
        return runtime_main(argc, argv);
    }

    int status = runtime_main(utf8_argc, utf8_argv);
    windows_free_utf8_argv(utf8_argv, utf8_argc);
    return status;
}
#endif
