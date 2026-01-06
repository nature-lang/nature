#include "fs.h"
#include <unistd.h>  // for read()/write() fallback
#include <errno.h>

static void on_write_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;
    assert(co);

    if (req->result < 0) {
        // vboxsf/NFS/9p 等文件系统不支持 pwritev，fallback 到 pwrite()
        if (req->result == UV_ENOTSUP) {
            ssize_t n = pwrite(ctx->fd, ctx->buf.base, ctx->buf.len, ctx->offset);
            if (n >= 0) {
                DEBUGF("[on_write_cb] pwritev not supported, fallback to pwrite(), bytes: %zd", n);
                ctx->data_len = n;
                co_ready(co);
                uv_fs_req_cleanup(&ctx->req);
                return;
            }
            rti_co_throw(co, strerror(errno), false);
        } else {
            // 文件写入异常，设置错误并返回
            rti_co_throw(co, (char *) uv_strerror(req->result), false);
        }
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // 写入成功，req->result 包含写入的字节数
    DEBUGF("[on_write_cb] write file success, bytes written: %ld", req->result);
    ctx->data_len = req->result;

    co_ready(co);
    uv_fs_req_cleanup(&ctx->req);
}

static inline void on_open_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    if (req->result < 0) {
        DEBUGF("[on_open_cb] open file failed: %s, co: %p", uv_strerror(req->result), req->data);

        rti_co_throw(req->data, (char *) uv_strerror(req->result), false);

        co_ready(req->data);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // 文件打开成功，设置 fd 并返回
    ctx->fd = req->result;
    co_ready(req->data);

    uv_fs_req_cleanup(&ctx->req);
}

static void on_read_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;

    if (req->result < 0) {
        // vboxsf/NFS/9p 等文件系统不支持 preadv，fallback 到 pread()
        if (req->result == UV_ENOTSUP) {
            ssize_t n = pread(ctx->fd, ctx->data, ctx->data_cap, ctx->offset);
            if (n >= 0) {
                DEBUGF("[on_read_cb] preadv not supported, fallback to pread(), bytes: %zd", n);
                ctx->data_len = n;
                co_ready(co);
                uv_fs_req_cleanup(&ctx->req);
                return;
            }
            rti_co_throw(co, strerror(errno), false);
        } else {
            // 文件读取异常，设置错误并返回，不需要关闭 fd, fd 由外部控制
            rti_co_throw(co, (char *) uv_strerror(req->result), false);
        }
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    ctx->data_len += req->result;
    assert(ctx->data_len <= ctx->data_cap);

    DEBUGF("[on_read_cb] read file success, data_len: %ld", ctx->data_len);
    co_ready(co);
    uv_fs_req_cleanup(&ctx->req);
}

/**
 * 主要用于 stdio/stdin/stderr 的创建, name 示例 "/dev/stdin"
 */
fs_context_t *rt_uv_fs_from(n_int_t fd, n_string_t *name) {
    fs_context_t *ctx = rti_gc_malloc(sizeof(fs_context_t), NULL);
    if (fd < 0) {
        coroutine_t *co = coroutine_get();
        rti_co_throw(co, "invalid fd", false);
        return NULL;
    }

    ctx->fd = fd;
    DEBUGF("[fs_from] create file context from fd: %ld, name: %s", fd,
            (char *) rt_string_ref(name));

    return ctx;
}

static void uv_async_fs_open(fs_context_t *ctx, char *path) {
    int result = uv_fs_open(&global_loop, &ctx->req, path, (int) ctx->flags, (int) ctx->mode, on_open_cb);
    if (result) {
        rti_co_throw(ctx->req.data, (char *) uv_strerror(result), false);
        co_ready(ctx->req.data);
    }
}

fs_context_t *rt_uv_fs_open(n_string_t *path, int64_t flags, int64_t mode) {
    // 创建 context, 不需要主动销毁，后续由用户端接手该变量，并由 GC 进行销毁
    fs_context_t *ctx = rti_gc_malloc(sizeof(fs_context_t), NULL);
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    ctx->flags = flags;
    ctx->mode = mode;
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_open, ctx, rt_string_ref(path), 0);
    if (co->has_error) {
        DEBUGF("[fs_open] open file failed: %s", (char *) rt_string_ref(rti_error_msg(co->error)));
        return NULL;
    } else {
        DEBUGF("[fs_open] open file success: %s", (char *) rt_string_ref(path));
    }

    return ctx;
}

n_int_t rt_uv_fs_read(fs_context_t *ctx, n_vec_t *buf) {
    return rt_uv_fs_read_at(ctx, buf, -1);
}

static void uv_async_fs_read_at(fs_context_t *ctx, int offset) {
    uv_fs_read(&global_loop, &ctx->req, ctx->fd, &ctx->buf, 1, offset, on_read_cb);
}

n_int_t rt_uv_fs_read_at(fs_context_t *ctx, n_vec_t *buf, int offset) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    DEBUGF("[rt_uv_fs_read] read file: %ld", ctx->fd);

    if (ctx->closed) {
        rti_co_throw(co, "fd already closed", false);
        return 0;
    }

    // 配置初始缓冲区，能够读取的最大程度受限于 buf.length
    ctx->data_cap = buf->length;
    ctx->data_len = 0; // 记录实际读取的数量
    ctx->data = (char *) buf->data;
    ctx->buf = uv_buf_init(ctx->data, buf->length);
    ctx->req.data = co;
    ctx->offset = offset; // 保存 offset 用于 fallback

    // 基于 fd offset 进行读取
    global_waiting_send(uv_async_fs_read_at, ctx, (void *) offset, 0);

    if (co->has_error) {
        DEBUGF("[rt_uv_fs_read] read file failed: %s", (char *) rt_string_ref(rti_error_msg(co->error)));
        return 0;
    } else {
        DEBUGF("[rt_uv_fs_read] read file success");
    }

    ctx->req.data = NULL;
    return ctx->data_len;
}

static void uv_async_fs_write_at(fs_context_t *ctx, n_vec_t *buf, int offset) {
    // 配置写入缓冲区并保存到 ctx 用于 fallback
    ctx->buf = uv_buf_init((char *) buf->data, buf->length);
    ctx->offset = offset; // 保存 offset 用于 fallback

    // 发起异步写入请求，指定偏移量
    uv_fs_write(&global_loop, &ctx->req, ctx->fd, &ctx->buf, 1, offset, on_write_cb);
}

n_int_t rt_uv_fs_write_at(fs_context_t *ctx, n_vec_t *buf, int offset) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();

    if (ctx->closed) {
        rti_co_throw(co, "fd already closed", false);
        return 0;
    }

    DEBUGF("[fs_write_at] write file: %ld, offset: %d, data_len: %ld", ctx->fd, offset, buf->length);
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_write_at, ctx, buf, (void *) (int64_t) offset);

    if (co->has_error) {
        DEBUGF("[fs_write_at] write file failed: %s", (char *) rt_string_ref(rti_error_msg(co->error)));
    } else {
        DEBUGF("[fs_write_at] write file success");
    }

    return ctx->data_len;
}

n_int_t rt_uv_fs_write(fs_context_t *ctx, n_vec_t *buf) {
    assert(buf);

    DEBUGF("[rt_uv_fs_write] buf len: %ld", buf->length);
    return rt_uv_fs_write_at(ctx, buf, -1);
}

static void uv_async_fs_close(fs_context_t *ctx, coroutine_t *co) {
    // 同步方式关闭文件
    int result = uv_fs_close(&global_loop, &ctx->req, ctx->fd, NULL);
    if (result < 0) {
        DEBUGF("[fs_close] close file failed: %s", uv_strerror(result));
    } else {
        DEBUGF("[fs_close] close file success");
    }
    uv_fs_req_cleanup(&ctx->req);

    co_ready(co);
}

void rt_uv_fs_close(fs_context_t *ctx) {
    n_processor_t *p = processor_get();
    DEBUGF("[fs_close] close file: %ld", ctx->fd);

    // 重复关闭直接返回
    if (ctx->closed) {
        return;
    }
    ctx->closed = true;

    global_waiting_send(uv_async_fs_close, ctx, coroutine_get(), 0);
}


static void on_stat_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;
    assert(co);

    if (req->result < 0) {
        // File stat operation failed, set error and return
        rti_co_throw(co, (char *) uv_strerror(req->result), false);
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // Stat operation successful
    DEBUGF("[on_stat_cb] stat file success, fd: %d", ctx->fd);

    co_ready(co);
    // Note: req cleanup is handled in the main function after coroutine resumes
}

static void uv_async_fs_stat(fs_context_t *ctx, coroutine_t *co) {
    // Initiate async stat request
    int result = uv_fs_fstat(&global_loop, &ctx->req, ctx->fd, on_stat_cb);
    if (result < 0) {
        rti_co_throw(co, (char *) uv_strerror(result), false);
        co_ready(co);
    }
}

uv_stat_t rt_uv_fs_stat(fs_context_t *ctx) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    uv_stat_t stat_result = {0};

    if (ctx->closed) {
        rti_co_throw(co, "fd already closed", false);
        return stat_result;
    }

    DEBUGF("[rt_uv_fs_stat] stat file: %d", ctx->fd);

    // Set up coroutine resume point
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_stat, ctx, co, 0);

    if (co->has_error) {
        DEBUGF("[rt_uv_fs_stat] stat file failed: %s", (char *) rt_string_ref(rti_error_msg(co->error)));
    } else {
        DEBUGF("[rt_uv_fs_stat] stat file success");
        // Copy stat result from request
        stat_result = ctx->req.statbuf;
    }

    // Clean up request
    uv_fs_req_cleanup(&ctx->req);

    return stat_result;
}