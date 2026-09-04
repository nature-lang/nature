#include "runtime.h"

#include <stdatomic.h>
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
extern n_tagged_union_t user_main(void) __asm("_main.main");
#else
extern n_tagged_union_t user_main(void) __asm("main.main");
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
        // x mode: call user_main directly, no GC and no scheduler.
        // main returns errable<void>; only the error variant carries a descriptor.
        DEBUGF("[runtime_main] x mode, calling user_main directly");
        n_tagged_union_t result = user_main();
        if (result.tag_hash == hash_string(X_ERRABLE_ERROR_TAG)) {
            n_string_t msg = rt_x_errdesc_msg(result.value.ptr_value);
            char *dump = tlsprintf("uncaught error: '%s'\n", (char *) rt_string_ref(&msg));
            VOID write(STDOUT_FILENO, dump, strlen(dump));
            return 1;
        }
        DEBUGF("[runtime_main] x mode user code run completed, will exit");
    }

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
