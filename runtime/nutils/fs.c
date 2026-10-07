#include "fs.h"
#include <errno.h>
#ifdef __WINDOWS
#include "windows_utf8.h"
#include <io.h>
#else
#include <unistd.h> // for read()/write() fallback
#endif

#ifdef __WINDOWS
static ssize_t windows_pwrite(int fd, const void *buf, size_t len, int64_t offset) {
    if (offset < 0) {
        return _write(fd, buf, (unsigned int) len);
    }

    __int64 original_offset = _lseeki64(fd, 0, SEEK_CUR);
    if (original_offset < 0 || _lseeki64(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    int result = _write(fd, buf, (unsigned int) len);
    int saved_errno = errno;
    (void) _lseeki64(fd, original_offset, SEEK_SET);
    errno = saved_errno;
    return result;
}

static ssize_t windows_pread(int fd, void *buf, size_t len, int64_t offset) {
    if (offset < 0) {
        return _read(fd, buf, (unsigned int) len);
    }

    __int64 original_offset = _lseeki64(fd, 0, SEEK_CUR);
    if (original_offset < 0 || _lseeki64(fd, offset, SEEK_SET) < 0) {
        return -1;
    }

    int result = _read(fd, buf, (unsigned int) len);
    int saved_errno = errno;
    (void) _lseeki64(fd, original_offset, SEEK_SET);
    errno = saved_errno;
    return result;
}
#endif

static void fs_complete(fs_context_t *ctx, coroutine_t *co) {
    uv_fs_req_cleanup(&ctx->req);
    ctx->req.data = NULL;
    co_ready(co);
}

static void on_write_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;
    assert(co);

    if (req->result < 0) {
        // vboxsf/NFS/9p 等文件系统不支持 pwritev，fallback 到 pwrite()
        if (req->result == UV_ENOTSUP) {
#ifdef __WINDOWS
            ssize_t n = windows_pwrite(ctx->fd, ctx->buf.base, ctx->buf.len, ctx->offset);
#else
            ssize_t n = pwrite(ctx->fd, ctx->buf.base, ctx->buf.len, ctx->offset);
#endif
            if (n >= 0) {
                DEBUGF("[on_write_cb] pwritev not supported, fallback to pwrite(), bytes: %zd", n);
                ctx->data_len = n;
                fs_complete(ctx, co);
                return;
            }
            ctx->status = errno;
        } else {
            // 文件写入异常，设置错误并返回
            ctx->status = req->result;
        }
        fs_complete(ctx, co);
        return;
    }

    // 写入成功，req->result 包含写入的字节数
    DEBUGF("[on_write_cb] write file success, bytes written: %ld", req->result);
    ctx->data_len = req->result;

    fs_complete(ctx, co);
}

static inline void on_open_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;
    if (req->result < 0) {
        DEBUGF("[on_open_cb] open file failed: %s, co: %p", uv_strerror(req->result), req->data);

        ctx->status = req->result;

    } else {
        ctx->fd = req->result;
    }
    fs_complete(ctx, co);
}

static void on_read_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;

    if (req->result < 0) {
        // vboxsf/NFS/9p 等文件系统不支持 preadv，fallback 到 pread()
        if (req->result == UV_ENOTSUP) {
#ifdef __WINDOWS
            ssize_t n = windows_pread(ctx->fd, ctx->data, ctx->data_cap, ctx->offset);
#else
            ssize_t n = pread(ctx->fd, ctx->data, ctx->data_cap, ctx->offset);
#endif
            if (n >= 0) {
                DEBUGF("[on_read_cb] preadv not supported, fallback to pread(), bytes: %zd", n);
                ctx->data_len = n;
                fs_complete(ctx, co);
                return;
            }
            ctx->status = errno;
        } else {
            // 文件读取异常，设置错误并返回，不需要关闭 fd, fd 由外部控制
            ctx->status = req->result;
        }
        fs_complete(ctx, co);
        return;
    }

    ctx->data_len += req->result;
    assert(ctx->data_len <= ctx->data_cap);

    DEBUGF("[on_read_cb] read file success, data_len: %ld", ctx->data_len);
    fs_complete(ctx, co);
}

/**
 * 主要用于 stdio/stdin/stderr 的创建, name 示例 "/dev/stdin"
 */
n_ptr_result_t rt_uv_fs_from(n_int_t fd, n_string_t name) {
    if (fd < 0) {
        return N_RESULT_ERROR(n_ptr_result_t, native_error(N_ERROR_INVALID_ARGUMENT));
    }

    fs_context_t *ctx = rti_gc_malloc(sizeof(fs_context_t), NULL);
    ctx->fd = fd;
    DEBUGF("[fs_from] create file context from fd: %ld, name: %s", fd,
           (char *) rt_string_ref(&name));

    return N_RESULT_OK(n_ptr_result_t, ctx);
}

static void uv_async_fs_open(fs_context_t *ctx, char *path) {
#ifdef __WINDOWS
    int fd = rt_windows_open_utf8(path, (int) ctx->flags, (int) ctx->mode);
    if (fd < 0) {
        ctx->status = errno;
    } else {
        ctx->fd = fd;
    }
    co_ready(ctx->req.data);
#else
    int result = uv_fs_open(&global_loop, &ctx->req, path, (int) ctx->flags,
                            (int) ctx->mode, on_open_cb);
    if (result) {
        ctx->status = result;
        fs_complete(ctx, ctx->req.data);
    }
#endif
}

n_ptr_result_t rt_uv_fs_open(n_string_t path, int64_t flags, int64_t mode) {
    // 创建 context, 不需要主动销毁，后续由用户端接手该变量，并由 GC 进行销毁
    fs_context_t *ctx = rti_gc_malloc(sizeof(fs_context_t), NULL);
    coroutine_t *co = coroutine_get();
    ctx->flags = flags;
    ctx->mode = mode;
    ctx->status = 0;
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_open, ctx, rt_string_ref(&path), 0);
    if (ctx->status != 0) {
        DEBUGF("native filesystem operation failed");
        return N_RESULT_ERROR(n_ptr_result_t, native_fs_error(ctx->status));
    } else {
        DEBUGF("[fs_open] open file success: %s", (char *) rt_string_ref(&path));
    }

    return N_RESULT_OK(n_ptr_result_t, ctx);
}

n_int_result_t rt_uv_fs_read(fs_context_t *ctx, n_vec_t buf) {
    return rt_uv_fs_read_at(ctx, buf, -1);
}

static void uv_async_fs_read_at(fs_context_t *ctx, int offset) {
    int status = uv_fs_read(&global_loop, &ctx->req, ctx->fd, &ctx->buf, 1, offset, on_read_cb);
    if (status < 0) {
        ctx->status = status;
        fs_complete(ctx, ctx->req.data);
    }
}

n_int_result_t rt_uv_fs_read_at(fs_context_t *ctx, n_vec_t buf, int offset) {
    coroutine_t *co = coroutine_get();
    DEBUGF("[rt_uv_fs_read] read file: %ld", ctx->fd);

    if (ctx->closed) {
        return N_RESULT_ERROR(n_int_result_t, native_error(N_ERROR_CLOSED));
    }

    // 配置初始缓冲区，能够读取的最大程度受限于 buf.length
    ctx->data_cap = buf.length;
    ctx->data_len = 0; // 记录实际读取的数量
    ctx->data = (char *) buf.data;
    ctx->buf = uv_buf_init(ctx->data, buf.length);
    ctx->status = 0;
    ctx->req.data = co;
    ctx->offset = offset; // 保存 offset 用于 fallback

    // 基于 fd offset 进行读取
    global_waiting_send(uv_async_fs_read_at, ctx, (void *) (int64_t) offset, 0);

    if (ctx->status != 0) {
        DEBUGF("native filesystem operation failed");
        return N_RESULT_ERROR(n_int_result_t, native_fs_error(ctx->status));
    } else {
        DEBUGF("[rt_uv_fs_read] read file success");
    }

    return N_RESULT_OK(n_int_result_t, ctx->data_len);
}

static void uv_async_fs_write_at(fs_context_t *ctx, n_vec_t *buf, int offset) {
    // 配置写入缓冲区并保存到 ctx 用于 fallback
    ctx->buf = uv_buf_init((char *) buf->data, buf->length);
    ctx->offset = offset; // 保存 offset 用于 fallback

    // 发起异步写入请求，指定偏移量
    int status = uv_fs_write(&global_loop, &ctx->req, ctx->fd, &ctx->buf, 1, offset, on_write_cb);
    if (status < 0) {
        ctx->status = status;
        fs_complete(ctx, ctx->req.data);
    }
}

n_int_result_t rt_uv_fs_write_at(fs_context_t *ctx, n_vec_t buf, int offset) {
    coroutine_t *co = coroutine_get();

    if (ctx->closed) {
        return N_RESULT_ERROR(n_int_result_t, native_error(N_ERROR_CLOSED));
    }

    DEBUGF("[fs_write_at] write file: %ld, offset: %d, data_len: %ld", ctx->fd, offset, buf.length);
    ctx->status = 0;
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_write_at, ctx, &buf, (void *) (int64_t) offset);

    if (ctx->status != 0) {
        DEBUGF("native filesystem operation failed");
    } else {
        DEBUGF("[fs_write_at] write file success");
    }

    if (ctx->status != 0) return N_RESULT_ERROR(n_int_result_t, native_fs_error(ctx->status));
    return N_RESULT_OK(n_int_result_t, ctx->data_len);
}

n_int_result_t rt_uv_fs_write(fs_context_t *ctx, n_vec_t buf) {
    DEBUGF("[rt_uv_fs_write] buf len: %ld", buf.length);
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
        ctx->status = req->result;
        co_ready(co);
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
        ctx->status = result;
        co_ready(co);
    }
}

n_stat_result_t rt_uv_fs_stat(fs_context_t *ctx) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    uv_stat_t stat_result = {0};

    if (ctx->closed) {
        return N_RESULT_ERROR(n_stat_result_t, native_error(N_ERROR_CLOSED));
    }

    DEBUGF("[rt_uv_fs_stat] stat file: %d", ctx->fd);

    // Set up coroutine resume point
    ctx->status = 0;
    ctx->req.data = co;

    global_waiting_send(uv_async_fs_stat, ctx, co, 0);

    if (ctx->status != 0) {
        DEBUGF("native filesystem operation failed");
    } else {
        DEBUGF("[rt_uv_fs_stat] stat file success");
        // Copy stat result from request
        stat_result = ctx->req.statbuf;
    }

    // Clean up request
    uv_fs_req_cleanup(&ctx->req);
    ctx->req.data = NULL;

    if (ctx->status != 0) return N_RESULT_ERROR(n_stat_result_t, native_fs_error(ctx->status));
    return N_RESULT_OK(n_stat_result_t, stat_result);
}
