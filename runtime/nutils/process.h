#ifndef NATURE_PROCESS_H
#define NATURE_PROCESS_H

#include <uv.h>
#include "vec.h"
#include "utils/helper.h"
#include "runtime/processor.h"

typedef struct {
    n_string_t stdout_text;
    n_string_t stderr_text; // 如何组合,输出
    int32_t exit_code;
    int32_t exit_sig;
} process_state_t;

typedef struct {
    n_string_t name;
    n_vec_t args;
    n_string_t cwd;
    n_vec_t env;

    n_interface_t io_stdin;
    n_interface_t io_stdout;
    n_interface_t io_stderr;
} command_t;

typedef struct {
    uv_pipe_t pipe;
    char buffer[1024];
    int64_t read_buffer_count;
    const char *name;
    bool closed;
} pipe_context_t;

typedef struct {
    int64_t pid;
    char **args; // exit 释放
    char **envs; // exit 释放
    n_processor_t *p;
    coroutine_t *co;
    bool exited;
    int32_t exit_code;
    int32_t term_sig;
    command_t cmd;

    pipe_context_t stdout_pipe;
    pipe_context_t stderr_pipe;
    uv_process_t req; // 程序启动成功后, pid 存储在 req 中
} process_context_t;

n_string_t rt_uv_process_read_stdout(process_context_t *ctx);

n_string_t rt_uv_process_read_stderr(process_context_t *ctx);

void rt_uv_process_wait(process_context_t *ctx);

process_context_t *rt_uv_process_spawn(command_t *cmd);

#endif //NATURE_PROCESS_H
