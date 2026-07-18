#include "syscall.h"

#ifdef __WINDOWS
#include <process.h>
#else
#include <arpa/inet.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "errno.h"
#include "fcntl.h"
#include "nutils.h"
#include "runtime/processor.h"
#include "string.h"
#include "vec.h"

// 基于 execve 进行改造
/**
 * n_list_t 中的元素是 string_t
 * @param path
 * @param argv
 * @param envp
 */
void syscall_exec(n_string_t path, n_vec_t argv, n_vec_t envp) {
    char *p_str = rt_string_ref(&path);

    // args 转换成 char* 格式并给到 execve
    char **c_args = mallocz(sizeof(char *) * (argv.length + 1));
    for (int i = 0; i < argv.length; ++i) {
        n_string_t arg = {0};
        rti_vec_access(&argv, i, &arg);
        if (arg.data == NULL) {
            continue;
        }

        char *c_arg = rt_string_ref(&arg);
        c_args[i] = c_arg;
    }
    c_args[argv.length] = NULL; // 最后一个元素为 NULL

    // envs
    DEBUGF("[syscall_exec] envp.length=%lu", envp.length);
    char **c_envs = mallocz(sizeof(char *) * (envp.length + 1));
    for (int i = 0; i < envp.length; ++i) {
        n_string_t env = {0};
        rti_vec_access(&envp, i, &env);
        if (env.data == NULL) {
            continue;
        }

        char *c_env = rt_string_ref(&env);
        DEBUGF("[syscall_exec] c_env=%p, data=%s, strlen=%lu", (void *) c_env, c_env, strlen(c_env));
        c_envs[i] = c_env;
    }
    c_envs[envp.length] = NULL;

    // 一旦调用成功,当前进程会被占用
#ifdef __WINDOWS
    int result = _execve(p_str, (const char *const *) c_args, (const char *const *) c_envs);
#else
    int result = execve(p_str, c_args, c_envs);
#endif
    if (result == -1) {
        rti_throw(strerror(errno), false);
        return;
    }

    rti_throw("execve failed", false);
}

// 使用 waitpid, 返回值为 exit status
n_u32_t syscall_wait(n_int_t pid) {
    int status;
#ifdef __WINDOWS
    intptr_t result = _cwait(&status, (intptr_t) pid, _WAIT_CHILD);
#else
    int result = waitpid((pid_t) pid, &status, 0);
#endif
    if (result == -1) {
        rti_throw(strerror(errno), false);
        return 0;
    }

    return (n_u32_t) status;
}

n_int_t syscall_call6(n_int_t number, n_uint_t a1, n_uint_t a2, n_uint_t a3, n_uint_t a4, n_uint_t a5, n_uint_t a6) {
#ifdef __WINDOWS
    (void) number;
    (void) a1;
    (void) a2;
    (void) a3;
    (void) a4;
    (void) a5;
    (void) a6;
    rti_throw("raw syscalls are not supported on Windows", false);
    return 0;
#else
    int64_t result = syscall(number, a1, a2, a3, a4, a5, a6);

    if (result == -1) {
        rti_throw(strerror(errno), false);
        return 0;
    }
    return (n_int_t) result;
#endif
}
