#include "process.h"

#define WRITER_METHOD_INDEX 0

typedef n_int_t *(*io_write_fn)(void *self, n_vec_t *buf);

typedef n_int_t *(*io_read_fn)(void *self, n_vec_t *buf);

static inline bool is_real_exit(process_context_t *ctx) {
    return ctx->exited && ctx->stderr_pipe.closed && ctx->stdout_pipe.closed;
}

static inline void real_exit(process_context_t *ctx) {
    assert(ctx);
    assert(ctx->exited);
    assert(ctx->stdout_pipe.closed);
    assert(ctx->stderr_pipe.closed);

    // 关闭 pipe
    uv_close((uv_handle_t *) &ctx->stdout_pipe.pipe, NULL);
    uv_close((uv_handle_t *) &ctx->stderr_pipe.pipe, NULL);

    // 唤醒主协程
    coroutine_t *co = ctx->req.data;
    if (co) {
        DEBUGF("[real_exit] will ready main co")
        co_ready(co);
    } else {
        DEBUGF("[real_exit] not co, not need ready")
    }
}

static inline void on_exit_cb(uv_process_t *req, int64_t exit_status, int term_signal) {
    DEBUGF("[on_exit_cb] process exited, status: %ld, sig: %d", exit_status, term_signal);
    process_context_t *ctx = CONTAINER_OF(req, process_context_t, req);
    ctx->exited = true;
    ctx->exit_code = exit_status;
    ctx->term_sig = term_signal;

    free(ctx->args);
    if (ctx->envs) {
        free(ctx->envs);
    }
    uv_close((uv_handle_t *) req, NULL);

    if (is_real_exit(ctx)) {
        real_exit(ctx);
    }
}

static inline void process_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    pipe_context_t *ctx = CONTAINER_OF(handle, pipe_context_t, pipe);
    *buf = uv_buf_init(ctx->buffer, sizeof(ctx->buffer));
}

static inline void on_read_stderr_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    pipe_context_t *pipe_ctx = CONTAINER_OF(stream, pipe_context_t, pipe);
    coroutine_t *co = pipe_ctx->pipe.data;

    process_context_t *ctx = CONTAINER_OF(pipe_ctx, process_context_t, stderr_pipe);

    // 单次读取
    uv_read_stop(stream);

    if (nread < 0) {
        if (nread == UV_EOF) {
            rti_co_throw(co, "read eof", false);
        } else {
            rti_co_throw(co, "read pipe failed", false);
        }
        pipe_ctx->closed = true;

        DEBUGF("[on_read_stderr_cb] nread %ld, will return", nread);

        // 没有 buf 需要处理，判断退出状态, 如果满足退出状态，则进行主协程退出
        if (is_real_exit(ctx)) {
            real_exit(ctx);
        }

        co_ready(co); // 唤醒 stdio 协程, 唤醒后也是直接退出协程处理
        return;
    }

    uv_read_stop(stream);

    // 尝试打印读取到的数据, 尝试将读取的数据写入到 process_context 设置的 stdin 和 stdout 中
    DEBUGF("[on_read_stderr_cb] %s: nread: %d", pipe_ctx->name, (int) nread);

    pipe_ctx->read_buffer_count = nread;

    // 唤醒应用层消化相关数据
    co_ready(co);
}

static inline void on_read_stdout_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    pipe_context_t *pipe_ctx = CONTAINER_OF(stream, pipe_context_t, pipe);
    coroutine_t *co = pipe_ctx->pipe.data;
    process_context_t *ctx = CONTAINER_OF(pipe_ctx, process_context_t, stdout_pipe);

    // 单次读取
    uv_read_stop(stream);

    if (nread < 0) {
        if (nread == UV_EOF) {
            rti_co_throw(co, "read eof", false);
        } else {
            rti_co_throw(co, "read pipe failed", false);
        }
        pipe_ctx->closed = true;

        DEBUGF("[on_read_stdout_cb] nread %d, will return", nread);

        // 没有 buf 需要处理，判断退出状态, 如果满足退出状态，则进行主协程退出
        if (is_real_exit(ctx)) {
            real_exit(ctx);
        }

        co_ready(co); // 唤醒 stdio 协程, 唤醒后也是直接退出协程处理
        return;
    }

    uv_read_stop(stream);

    // 尝试打印读取到的数据, 尝试将读取的数据写入到 process_context 设置的 stdin 和 stdout 中
    DEBUGF("[on_read_stdout_cb] %s: nread: %d", pipe_ctx->name, (int) nread);


    pipe_ctx->read_buffer_count = nread;

    // 唤醒应用层消化相关数据
    co_ready(co);
}

static void uv_async_process_spawn(process_context_t *ctx, coroutine_t *co) {
    // 初始化
    uv_pipe_init(&global_loop, &ctx->stderr_pipe.pipe, 0);
    uv_pipe_init(&global_loop, &ctx->stdout_pipe.pipe, 0);

    uv_process_options_t options = {0};
    options.exit_cb = on_exit_cb;
    options.file = rt_string_ref(&ctx->cmd.name);
    options.args = ctx->args;
    options.env = ctx->envs;

    // 设置标准输出重定向到当前进程
    uv_stdio_container_t stdio[3];
    stdio[0].flags = UV_IGNORE;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *) &ctx->stdout_pipe.pipe;
    stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[2].data.stream = (uv_stream_t *) &ctx->stderr_pipe.pipe;

    options.stdio = stdio;
    options.stdio_count = 3;

    int result = uv_spawn(&global_loop, &ctx->req, &options);
    if (result) {
        rti_co_throw(co, (char *) uv_strerror(result), false);
    } else {
        // 设置 pid 的值
        ctx->pid = ctx->req.pid;
        assert(ctx->pid);
    }

    co_ready(co);
}

process_context_t *rt_uv_process_spawn(command_t *cmd) {
    assert(cmd->args.data);
    assert(cmd->env.data);
    DEBUGF("[rt_uv_process_spawn] start")


    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    process_context_t *ctx = rti_gc_malloc(sizeof(process_context_t), NULL);
    assert(ctx);

    ctx->cmd = *cmd;
    ctx->p = p;

    // 初始化管道
    ctx->stderr_pipe.name = "stderr";
    ctx->stdout_pipe.name = "stdout";

    int64_t arg_count = cmd->args.length + 2; // name + null
    ctx->args = mallocz(sizeof(char *) * arg_count);

    ctx->args[0] = rt_string_ref(&cmd->name);
    for (int i = 0; i < cmd->args.length; ++i) {
        n_string_t arg = {0};
        rti_vec_access(&cmd->args, i, &arg);
        ctx->args[i + 1] = rt_string_ref(&arg);
    }
    ctx->args[arg_count - 1] = NULL;


    if (cmd->env.length) {
        ctx->envs = mallocz(sizeof(char *) * (cmd->env.length + 1)); // +1 is null

        for (int i = 0; i < cmd->env.length; ++i) {
            n_string_t env = {0};
            rti_vec_access(&cmd->env, i, &env);
            ctx->envs[i] = rt_string_ref(&env);
        }
        ctx->envs[cmd->env.length] = NULL;
    }

    global_waiting_send(uv_async_process_spawn, ctx, co, 0);

    DEBUGF("[rt_uv_process_spawn] end, ctx: %p", ctx)
    return ctx;
}

void uv_async_process_wait(process_context_t *ctx, coroutine_t *co) {
    // check real exited, ready
    if (is_real_exit(ctx)) {
        DEBUGF("[uv_async_process_wait] real exit, co ready")
        co_ready(co);
        return;
    }

    DEBUGF("[uv_async_process_wait] wait exit")
    // waiting real exit
    ctx->req.data = co;
}

/**
 * uv_loop 和当前 coroutine 必须在同一个 processor 中运行, 基于此可以避免 race 问题，判断 exited 也不需要加锁。
 */
void rt_uv_process_wait(process_context_t *ctx) {
    assert(ctx);
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    global_waiting_send(uv_async_process_wait, ctx, co, 0);

    DEBUGF("[rt_uv_process_wait] process %ld exited", ctx->pid);
}

static void uv_async_process_read_stdout(process_context_t *ctx) {
    uv_read_start((uv_stream_t *) &ctx->stdout_pipe.pipe, process_alloc_buffer_cb, on_read_stdout_cb);
}

n_string_t rt_uv_process_read_stdout(process_context_t *ctx) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    if (ctx->stdout_pipe.closed) {
        rti_co_throw(co, "stdout pipe closed", NULL);
        return (n_string_t) {0};
    }

    ctx->stdout_pipe.pipe.data = co;
    global_waiting_send(uv_async_process_read_stdout, ctx, 0, 0);

    if (co->has_error) {
        DEBUGF("[rt_uv_process_read_stdout] co has err, will return NULL")
        return (n_string_t) {0};
    }

    n_string_t buf_string = rt_string_ref_new(ctx->stdout_pipe.buffer, ctx->stdout_pipe.read_buffer_count);
    DEBUGF("[rt_uv_process_read_stdout] read buf len: %d", buf_string.length);
    return buf_string;
}

static void uv_async_process_read_stderr(process_context_t *ctx) {
    uv_read_start((uv_stream_t *) &ctx->stderr_pipe.pipe, process_alloc_buffer_cb, on_read_stderr_cb);
}

n_string_t rt_uv_process_read_stderr(process_context_t *ctx) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    if (ctx->stderr_pipe.closed) {
        rti_co_throw(co, "stderr pipe closed", NULL);
        return (n_string_t) {0};
    }

    ctx->stderr_pipe.pipe.data = co;
    global_waiting_send(uv_async_process_read_stderr, ctx, 0, 0);

    if (co->has_error) {
        DEBUGF("[rt_uv_process_read_stderr] co has err, will return NULL")
        return (n_string_t) {0};
    }

    // 提取 buf 并返回
    n_string_t buf_string = rt_string_ref_new(ctx->stderr_pipe.buffer, ctx->stderr_pipe.read_buffer_count);
    DEBUGF("[rt_uv_process_read_stderr] read buf len: %ld", buf_string.length);
    return buf_string;
}
