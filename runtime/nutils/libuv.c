#include "libuv.h"
#include "runtime/runtime.h"
#include "runtime/processor.h"
#include <runtime/rt_mutex.h>

#define DEFAULT_BACKLOG 128

mutex_t uv_thread_locker;
uv_loop_t uv_global_loop;

static inline void on_async_close_cb(uv_handle_t *handle) {
    conn_ctx_t *ctx = CONTAINER_OF(handle, conn_ctx_t, async_handle);
    DEBUGF("[on_async_close_cb] ctx: %p", ctx);

    free(ctx->read_buf_base);
    free(ctx->write_buf.base);
    free(ctx);
}

static inline void on_close_cb(uv_handle_t *handle) {
    conn_ctx_t *ctx = CONTAINER_OF(handle, conn_ctx_t, client_handle);
    DEBUGF("[on_close_cb] ctx, %p, client_handle: %p", ctx, &ctx->client_handle);

    // 需要先关闭 async 才能释放内存 TODO 检查 async_handle 是否创建
    uv_close((uv_handle_t *) &ctx->async_handle, on_async_close_cb);

    // free(ctx->read_buf_base);
    // free(ctx->write_buf.base);
    // free(ctx);
}

/**
 * tcp 流中的数据发送完毕后会触发 shutdown req
 * @param shutdown_req
 * @param status
 * @return
 */
static inline void on_shutdown_cb(uv_shutdown_t *shutdown_req, int status) {
    DEBUGF("[on_shutdown_cb] status: %d, handle: %p", status, shutdown_req->handle);
    uv_close((uv_handle_t *) shutdown_req->handle, on_close_cb);
    free(shutdown_req);
}

/**
 * @param write_req
 * @param status
 */
static inline void on_write_end_cb(uv_write_t *write_req, int status) {
    if (status < 0) {
        // 对端可能已经关闭了连接,导致写入失败等情况
        char *msg = dsprintf("uv_write failed: %s", uv_strerror(status));
        DEBUGF("[on_write_end_cb] %s", msg);
    }

    conn_ctx_t *ctx = CONTAINER_OF(write_req, conn_ctx_t, write_req);
    uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb);
}

void rt_uv_write(void *client, n_string_t *data) {
    coroutine_t *co = coroutine_get();
    DEBUGF("[rt_uv_write] co:%p, client: %p, data: %s", co, client, (char*)rt_string_ref(data));
    uv_buf_t buf = uv_buf_init((char *) data->data, data->length);
    uv_write_t *write_req = malloc(sizeof(uv_write_t));
    write_req->data = co;
    int result = uv_write(write_req, (uv_stream_t *) client, &buf, 1, on_write_end_cb);
    if (result) {
        DEBUGF("[rt_uv_write] uv_write failed: %s", uv_strerror(result));
        // rt_co_error(co, dsprintf("uv_write failed: %s", uv_strerror(result)), false);
        return;
    }

    co_yield_waiting(co->p, co);

    DEBUGF("[rt_uv_write] write resume, co=%p, will shutdown client=%p", co, client);
    uv_shutdown_t *shutdown_req = malloc(sizeof(uv_shutdown_t));
    uv_shutdown(shutdown_req, (uv_stream_t *) client, on_shutdown_cb);
}

/**
 * TODO 如果一次无法完整读取，多申请 8byte 内存，生成 free list 结构
 * @param handle
 * @param suggested_size
 * @param buf
 */
void alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    DEBUGF("[uv_alloc_buffer] suggested_size: %ld", suggested_size);
    buf->base = (char *) malloc(suggested_size);
    buf->len = suggested_size;
}

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

void rt_on_write(uv_write_t *req, int status) {
    write_req_t *wr = (write_req_t *) req;
    uv_stream_t *client = req->handle;

    free(wr->buf.base);
    free(wr);

    // Close the client connection after response
    uv_close((uv_handle_t *) client, on_close_cb);
}

static inline void async_conn_handle_cb(uv_async_t *handle) {
    conn_ctx_t *ctx = CONTAINER_OF(handle, conn_ctx_t, async_handle);
    DEBUGF("[async_conn_handle_cb] ctx: %p, client_handle: %p", ctx, handle);

    // assert(uv_is_active((uv_handle_t*)&ctx->client_handle));
    // assert(ctx->write_buf.len > 0);

    int result = uv_write(&ctx->write_req, (uv_stream_t *) &ctx->client_handle, &ctx->write_buf, 1, on_write_end_cb);
    if (result) {
        uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb);
    }
}

static inline void test_send_async_fn() {
    conn_ctx_t *ctx = rt_coroutine_arg();

    char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 12\r\n"
            "Connection: close\r\n"
            "\r\n"
            "hello world\n";

    ctx->write_buf.base = response;
    ctx->write_buf.len = strlen(response);

    int result = uv_async_send(&ctx->async_handle);
    if (result) {
        DEBUGF("[test_send_async_fn] uv_async_send failed: %s", uv_strerror(result));
    }
    DEBUGF("[test_send_async_fn] success");
}

static inline void on_read_cb(uv_stream_t *client_handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_read_cb] client: %p, nread: %ld", client_handle, nread);

    conn_ctx_t *ctx = CONTAINER_OF(client_handle, conn_ctx_t, client_handle);

    if (nread < 0) {
        if (nread == UV_EOF) {
            // 什么都不用做, 直接返回, on_read_cb 会被重复调用
        } else {
            DEBUGF("[on_read_cb] uv_read failed: %s", uv_strerror(nread));
            uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb); // TODO 可能会重复调用
        }
        free(buf->base); // 交给 read catch 进行 close 或者是其他操作
        return;
    }

    // http 请求需要一次读取完整
    if (!strstr(buf->base, "\r\n\r\n")) {
        free(buf->base);
        DEBUGF("[on_read_cb] unable to read full http body, current package max read size <= 65535");
        uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb);
        return;
    }

    ctx->read_buf_base = buf->base;

    // 创建协程，准备处理请求数据, 这里都太麻烦了。不如 data 复杂化一点，用一个结构体处理，就不用担心被 gc 杀掉
    coroutine_t *listen_co = client_handle->data;
    // 传递 coroutine 参数给到用户空间, 该参数使用
    uv_async_init(&listen_co->p->uv_loop, &ctx->async_handle, async_conn_handle_cb);

    // n_server 中包含路由，通用回调等信息
    coroutine_t *sub_conn_handler_co = rt_coroutine_new(ctx->n_server->handler, 0, NULL, ctx);
    // coroutine_t *sub_conn_handler_co = rt_coroutine_new(test_send_async_fn, 0, NULL, ctx);
    rt_coroutine_dispatch(sub_conn_handler_co);
}

void rt_uv_conn_resp(conn_ctx_t *ctx, n_string_t *resp_data) {
    DEBUGF("[rt_uv_conn_resp] ctx: %p, client_handle: %p", ctx, &ctx->client_handle);
    // 进行数据 copy 避免 resp_data 后续被清理掉
    ctx->write_buf.base = malloc(resp_data->length + 1);
    ctx->write_buf.len = resp_data->length;
    memmove(ctx->write_buf.base, resp_data->data, resp_data->length);
    ctx->write_buf.base[resp_data->length] = '\0';

    uv_async_send(&ctx->async_handle);
}

/**
 * libuv processor_run -> uv_run 会在有新的请求时负责触发该回调, 所以当前回调不运行在任何 coroutine 上，所以不要试图进行 coroutine_get
 * 如果存在异常，直接打日志并返回，表示无法处理当前请求即可。暂时不选择崩溃退出 listen coroutine 方案，后续调研后进行整改
 *
 * @param server
 * @param status
 */
static void on_new_conn_cb(uv_stream_t *server, int status) {
    DEBUGF("[accept_new_conn] status: %d", status);

    if (status < 0) {
        DEBUGF("[accept_new_conn] new connection error: %s", uv_strerror(status));
        return;
    }

    coroutine_t *listen_co = server->data;
    assert(listen_co->p);
    assert(listen_co->data);
    n_processor_t *p = listen_co->p;

    // 初始化 client 数据 accept loop 和 listen loop 必须使用同一个 loop
    conn_ctx_t *ctx = malloc(sizeof(conn_ctx_t));
    ctx->n_server = listen_co->data;

    uv_tcp_init(server->loop, &ctx->client_handle);
    int result = uv_accept(server, (uv_stream_t *) &ctx->client_handle);
    if (result) {
        uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb);
        return;
    }

    ctx->client_handle.data = listen_co;
    result = uv_read_start((uv_stream_t *) &ctx->client_handle, alloc_buffer_cb, on_read_cb);
    if (result) {
        DEBUGF("[rt_uv_read] uv_read_start failed: %s", uv_strerror(result));
        uv_close((uv_handle_t *) &ctx->client_handle, on_close_cb);
        return;
    }

    DEBUGF("[accept_new_conn] accept new conn and create client co success, ctx: %p, client_handle: %p", ctx,
            &ctx->client_handle);
}

/**
 * @param n_server
 */
void rt_uv_http_listen(n_server_t *n_server) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    co->data = n_server;

    uv_tcp_t *uv_server = NEW(uv_tcp_t);
    uv_tcp_init(&p->uv_loop, uv_server);
    uv_server->data = co;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(n_server->addr), n_server->port, &addr);
    uv_tcp_bind(uv_server, (const struct sockaddr *) &addr, 0);


    // 新的请求进来将会触发 new connection 回调
    int result = uv_listen((uv_stream_t *) uv_server, DEFAULT_BACKLOG, on_new_conn_cb);
    if (result) {
        // 端口占用等错误
        rt_co_error(co, dsprintf("uv listen failed: %s", uv_strerror(result)), false);
        return;
    }

    TDEBUGF("[rt_uv_http_listen] listen success, port=%ld, p_index=%d", n_server->port, p->index);
    co_yield_waiting(p, co);
    TDEBUGF("[rt_uv_http_listen] listen resume, port=%ld, and return, p_index=%d", n_server->port, p->index);
}
