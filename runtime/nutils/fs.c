#include "fs.h"

static void on_write_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;

    if (req->result < 0) {
        // 文件写入异常，设置错误并返回
        rt_co_error(co, (char *) uv_strerror(req->result), false);
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // 写入成功，req->result 包含写入的字节数
    DEBUGF("[on_write_cb] write file success, bytes written: %ld", req->result);
    co_ready(co);
    uv_fs_req_cleanup(&ctx->req);
}

static inline void on_open_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    if (req->result < 0) {
        DEBUGF("[on_open_cb] open file failed: %s, co: %p", uv_strerror(req->result), req->data);

        rt_co_error(req->data, (char *) uv_strerror(req->result), false);

        co_ready(req->data);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // 文件打开成功，设置 fd 并返回
    ctx->fd = req->result;
    co_ready(req->data);

    uv_fs_req_cleanup(&ctx->req);
}

static void on_read_at_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;

    if (req->result < 0) {
        // 文件读取异常，设置错误并返回
        rt_co_error(co, (char *) uv_strerror(req->result), false);
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    // result >= 0, 表示读取的数据长度
    ctx->data_len = req->result;

    DEBUGF("[on_read_at_cb] read file success, data_len: %ld", ctx->data_len);
    co_ready(co);
    uv_fs_req_cleanup(&ctx->req);
}

static void on_read_cb(uv_fs_t *req) {
    fs_context_t *ctx = CONTAINER_OF(req, fs_context_t, req);
    coroutine_t *co = req->data;

    if (req->result < 0) {
        // 文件读取异常， 设置错误并返回，不需要关闭 fd, fd 由外部控制
        rt_co_error(co, (char *) uv_strerror(req->result), false);
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }

    if (req->result == 0) {// 文件已经读取完成, 可以正常返回
        DEBUGF("[on_read_cb] read file success, data_len: %ld", ctx->data_len);
        co_ready(co);
        uv_fs_req_cleanup(&ctx->req);
        return;
    }


    // 成功读取数据, 但是没有读取完整, req result 中保存了读取的数据的长度，通常是 buf_size
    // 读取的相关数据保存在 data 中。data 作为连续缓冲区，其空闲长度总是大于等于 buf_size
    ctx->data_len += req->result;
    assert(ctx->data_len <= ctx->data_cap);

    // 检查缓冲区是否满载
    if (ctx->data_len >= ctx->data_cap) {
        ctx->data_cap = ctx->data_cap * 2;
        ctx->data = realloc(ctx->data, ctx->data_cap);
        if (!ctx->data) {
            rt_co_error(co, "out of memory", false);
            co_ready(co);
            uv_fs_req_cleanup(&ctx->req);
            return;
        }
    }

    int64_t buf_len = ctx->data_cap - ctx->data_len;
    if (buf_len > (BUFFER_SIZE * 10)) {// 最多一次读取 40KB 的内容
        buf_len = BUFFER_SIZE * 10;
    }
    ctx->buf = uv_buf_init(ctx->data + ctx->data_len, buf_len);
    uv_fs_req_cleanup(&ctx->req);

    n_processor_t *p = processor_get();
    uv_fs_read(&p->uv_loop, &ctx->req, ctx->fd, &ctx->buf, 1, -1, on_read_cb);
}

fs_context_t *rt_uv_fs_open(n_string_t *path, int64_t flags, int64_t mode) {
    // 创建 context, 不需要主动销毁，后续由用户端接手该变量，并由 GC 进行销毁
    fs_context_t *ctx = rti_gc_malloc(sizeof(fs_context_t), NULL);
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    int result = uv_fs_open(&p->uv_loop, &ctx->req, rt_string_ref(path), (int) flags, (int) mode, on_open_cb);
    if (result) {
        rt_co_error(co, (char *) uv_strerror(result), false);
        return NULL;
    }

    // 可以 co 的恢复点
    ctx->req.data = co;

    // yield wait file open
    co_yield_waiting(co, NULL, NULL);

    if (co->error && co->error->has) {
        DEBUGF("[fs_open] open file failed: %s", rt_string_ref(co->error->msg));
        return NULL;
    } else {
        DEBUGF("[fs_open] open file success: %s", rt_string_ref(path));
    }

    return ctx;
}

n_string_t *rt_uv_fs_read(fs_context_t *ctx) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    DEBUGF("[fs_read] read file: %d", ctx->fd);

    if (ctx->closed) {
        rt_co_error(co, "fd already closed", false);
        return NULL;
    }

    // 配置初始缓冲区
    ctx->data_cap = BUFFER_SIZE;
    ctx->data_len = 0;
    ctx->data = malloc(BUFFER_SIZE);
    // libuv 回调会直接将数据写入到 data 中，注意调整 data 的起始位置即可
    ctx->buf = uv_buf_init(ctx->data, BUFFER_SIZE);

    lseek(ctx->fd, 0, SEEK_SET);

    // 总是从 0 开始读取
    ctx->req.data = co;
    uv_fs_read(&p->uv_loop, &ctx->req, ctx->fd, &ctx->buf, 1, -1, on_read_cb);

    co_yield_waiting(co, NULL, NULL);

    if (co->error && co->error->has) {
        DEBUGF("[fs_read] read file failed: %s", rt_string_ref(co->error->msg));
        return NULL;
    } else {
        DEBUGF("[fs_read] read file success");
    }

    n_string_t *result = string_new(ctx->data, ctx->data_len);

    // 读取完成, 清理 data
    free(ctx->data);
    ctx->data = NULL;
    ctx->data_len = 0;
    ctx->data_cap = 0;

    return result;
}

n_string_t *rt_uv_fs_read_at(fs_context_t *ctx, int offset, int len) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    DEBUGF("[fs_read_at] read file: %d, offset: %d, len: %d", ctx->fd, offset, len);
    if (ctx->closed) {
        rt_co_error(co, "fd already closed", false);
        return NULL;
    }

    ctx->req.data = co;

    if (len == -1) {
        off_t file_size = lseek(ctx->fd, 0, SEEK_END);// 获取文件大小
        int remaining_size = file_size - offset;
        if (remaining_size <= 0) {
            // 偏移量超过文件大小
            rt_co_error(co, "offset exceeds file size", false);
            co_ready(co);
            return NULL;
        }

        len = remaining_size;
    }

    // 配置初始缓冲区，使用指定的长度, 保证能够进行一次读取完成
    ctx->data_cap = len;
    ctx->data_len = 0;
    ctx->data = malloc(len);
    ctx->buf = uv_buf_init(ctx->data, len);

    // 直接使用 offset 参数，不需要 lseek
    uv_fs_read(&p->uv_loop, &ctx->req, ctx->fd, &ctx->buf, 1, offset, on_read_at_cb);

    co_yield_waiting(co, NULL, NULL);

    if (co->error && co->error->has) {
        DEBUGF("[fs_read_at] read file failed: %s", rt_string_ref(co->error->msg));
        if (ctx->data) {
            free(ctx->data);
            ctx->data = NULL;
        }
        return NULL;
    }

    n_string_t *result = string_new(ctx->data, ctx->data_len);

    // 读取完成, 清理 data
    free(ctx->data);
    ctx->data = NULL;
    ctx->data_len = 0;
    ctx->data_cap = 0;

    return result;
}

void rt_uv_fs_write_at(fs_context_t *ctx, n_string_t *data, int offset, int len) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();

    if (ctx->closed) {
        rt_co_error(co, "fd already closed", false);
        return;
    }

    // 如果 len 为 -1，则使用整个数据长度
    int write_len = (len == -1) ? data->length : len;
    // 确保不超过实际数据长度
    write_len = (write_len > data->length) ? data->length : write_len;

    DEBUGF("[fs_write_at] write file: %d, offset: %d, data_len: %d", ctx->fd, offset, write_len);

    // 配置写入缓冲区
    uv_buf_t buf = uv_buf_init(rt_string_ref(data), write_len);

    // 设置协程恢复点
    ctx->req.data = co;

    // 发起异步写入请求，指定偏移量
    uv_fs_write(&p->uv_loop, &ctx->req, ctx->fd, &buf, 1, offset, on_write_cb);

    // 挂起协程等待写入完成
    co_yield_waiting(co, NULL, NULL);

    if (co->error && co->error->has) {
        DEBUGF("[fs_write_at] write file failed: %s", rt_string_ref(co->error->msg));
    } else {
        DEBUGF("[fs_write_at] write file success");
    }
}

void rt_uv_fs_write(fs_context_t *ctx, n_string_t *data) {
    coroutine_t *co = coroutine_get();
    n_processor_t *p = processor_get();
    DEBUGF("[fs_write] write file: %d, data_len: %ld", ctx->fd, data->length);

    if (ctx->closed) {
        rt_co_error(co, "fd already closed", false);
        return;
    }

    // 配置写入缓冲区, 进行一次写入
    uv_buf_t buf = uv_buf_init(rt_string_ref(data), data->length);

    // 发起异步写入请求
    uv_fs_write(&p->uv_loop, &ctx->req, ctx->fd, &buf, 1, -1, on_write_cb);

    // 设置协程恢复点
    ctx->req.data = co;

    // 挂起协程等待写入完成
    co_yield_waiting(co, NULL, NULL);

    if (co->error && co->error->has) {
        DEBUGF("[fs_write] write file failed: %s", rt_string_ref(co->error->msg));
    } else {
        DEBUGF("[fs_write] write file success");
    }
}

void rt_uv_fs_close(fs_context_t *ctx) {
    n_processor_t *p = processor_get();
    DEBUGF("[fs_close] close file: %d", ctx->fd);

    // 重复关闭直接返回
    if (ctx->closed) {
        return;
    }

    // 同步方式关闭文件
    int result = uv_fs_close(&p->uv_loop, &ctx->req, ctx->fd, NULL);
    if (result < 0) {
        DEBUGF("[fs_close] close file failed: %s", uv_strerror(result));
    } else {
        DEBUGF("[fs_close] close file success");
    }

    // 清理请求和释放内存
    uv_fs_req_cleanup(&ctx->req);
    if (ctx->data) {
        free(ctx->data);
    }

    ctx->closed = true;

    // ctx 的内存应该由 GC 释放，而不是此处主动释放。
    // 否则 用户端 再次读取 ctx 从而出现奇怪的行为！
//    free(ctx);
}
