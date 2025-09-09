#include "http.h"
#include "runtime/processor.h"
#include "runtime/rt_mutex.h"
#include "runtime/runtime.h"

#define DEFAULT_BACKLOG 128


mutex_t uv_thread_locker = {0};
uv_loop_t uv_global_loop = {0};

static inline void on_async_close_cb(uv_handle_t *handle) {
    http_conn_t *ctx = CONTAINER_OF(handle, http_conn_t, async_handle);
    DEBUGF("[on_async_close_cb] ctx: %p", ctx);

    ctx->read_buf_len = 0;
    ctx->read_buf_cap = 0;
    free(ctx->read_buf);
    free(ctx->write_buf.base);
    free(ctx);
}

static inline void on_close_cb(uv_handle_t *handle) {
    http_conn_t *ctx = CONTAINER_OF(handle, http_conn_t, handle);
    DEBUGF("[on_close_cb] ctx, %p, client_handle: %p, async_handle: %p",
           ctx, &ctx->handle, ctx->async_handle);


    // 需要先关闭 async 才能释放内存
    if (uv_is_active((uv_handle_t *) &ctx->async_handle)) {
        uv_close((uv_handle_t *) &ctx->async_handle, on_async_close_cb);
    } else {
        DEBUGF("[on_close_cb] ctx async_handle not active, not need close")
    }

    // free(ctx->read_buf_base);
    // free(ctx->write_buf.base);
    // free(ctx);
}

/**
 * @param write_req
 * @param status
 */
static inline void on_write_end_cb(uv_write_t *write_req, int status) {
    if (status < 0) {
        // 对端可能已经关闭了连接,导致写入失败等情况
        char *msg = tlsprintf("uv_write failed: %s", uv_strerror(status));
        DEBUGF("[on_write_end_cb] %s", msg);
    }

    http_conn_t *conn = CONTAINER_OF(write_req, http_conn_t, write_req);
    uv_close((uv_handle_t *) &conn->handle, on_close_cb);
}

/**
 * @param handle
 * @param suggested_size
 * @param buf
 */
static inline void http_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    http_conn_t *ctx = CONTAINER_OF(handle, http_conn_t, handle);
    DEBUGF("[uv_alloc_buffer] suggested_size: %ld", suggested_size);
    ctx->read_buf_cap += HTTP_PARSER_BUF_SIZE;

    if (!ctx->read_buf) {
        ctx->read_buf = malloc(ctx->read_buf_cap);
    } else {
        ctx->read_buf = realloc(ctx->read_buf, ctx->read_buf_cap);
    }
    assert(ctx->read_buf);

    buf->base = ctx->read_buf + ctx->read_buf_len;
    buf->len = HTTP_PARSER_BUF_SIZE;
}

static inline void async_conn_handle_cb(uv_async_t *handle) {
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, async_handle);
    DEBUGF("[async_conn_handle_cb] ctx: %p, client_handle: %p", conn, handle);

    // assert(uv_is_active((uv_handle_t*)&ctx->client_handle));
    // assert(ctx->write_buf.len > 0);

    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &conn->write_buf, 1, on_write_end_cb);
    if (result) {
        uv_close((uv_handle_t *) &conn->handle, on_close_cb);
    }
}

static inline void on_read_cb(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_read_cb] client: %p, nread: %ld", handle, nread);

    http_conn_t *ctx = CONTAINER_OF(handle, http_conn_t, handle);

    if (nread < 0) {
        if (nread == UV_EOF) {
            // do nothing
        } else {
            DEBUGF("[on_read_cb] uv_read failed: %s", uv_strerror(nread));
            uv_close((uv_handle_t *) &ctx->handle, on_close_cb);
        }
        return;
    }

    // last_len 用于增量解析
    llhttp_init(&ctx->parser, HTTP_REQUEST, &ctx->settings);
    enum llhttp_errno err = llhttp_execute(&ctx->parser, buf->base, nread);
    if (err != HPE_OK) {
        DEBUGF("[on_read_cb] llhttp_execute failed: %s", llhttp_errno_name(err));
        // 直接关闭链接不做处理
        uv_close((uv_handle_t *) &ctx->handle, on_close_cb);
        return;
    }
    ctx->read_buf_len += nread;
    if (!ctx->parser_completed) {
        return;
    }

    // 创建协程，准备处理请求数据, 这里都太麻烦了。不如 data 复杂化一点，用一个结构体处理，就不用担心被 gc 杀掉
    coroutine_t *listen_co = handle->data;
    // 传递 coroutine 参数给到用户空间, 该参数使用
    uv_async_init(&listen_co->p->uv_loop, &ctx->async_handle, async_conn_handle_cb);

    // n_server 中包含路由，通用回调等信息
    coroutine_t *sub_conn_handler_co = rt_coroutine_new(ctx->n_server->handler, 0, NULL, ctx);
    // coroutine_t *sub_conn_handler_co = rt_coroutine_new(test_send_async_fn, 0, NULL, ctx);
    rt_coroutine_dispatch(sub_conn_handler_co);
}

void rt_uv_conn_resp(http_conn_t *conn, n_string_t *resp_data) {
    DEBUGF("[rt_uv_conn_resp] ctx: %p, client_handle: %p", conn, &conn->handle);

    // 进行数据 copy 避免 resp_data 后续被清理掉
    conn->write_buf.base = malloc(resp_data->length + 1);
    conn->write_buf.len = resp_data->length;
    memmove(conn->write_buf.base, resp_data->data, resp_data->length);
    conn->write_buf.base[resp_data->length] = '\0';

    uv_async_send(&conn->async_handle);
}

static int http_parser_on_url_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *ctx = CONTAINER_OF(parser, http_conn_t, parser);
    ctx->url_at = at;
    ctx->url_len = length;

    ctx->path_at = at;
    ctx->path_len = length; // 如果不存在 ? 和 #， path 长度就是整个 url 长度

    const char *question_mark = memchr(at, '?', length);
    if (question_mark) {
        ctx->path_len = question_mark - at;
        ctx->query_at = question_mark + 1;
        ctx->query_len = length - ctx->path_len - 1;
    }

    return 0;
}

static int http_parser_on_header_field_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *ctx = CONTAINER_OF(parser, http_conn_t, parser);
    ctx->headers[ctx->headers_len].name_at = at;
    ctx->headers[ctx->headers_len].name_len = length;

    if (length == 4 && (*(uint32_t *) at == 0x74736F48)) {
        parser->flags |= F_HOST; // 标记发现 Host 字段
    }

    return 0;
}

static int http_parser_on_header_value_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *ctx = CONTAINER_OF(parser, http_conn_t, parser);
    ctx->headers[ctx->headers_len].value_at = at;
    ctx->headers[ctx->headers_len].value_len = length;
    ctx->headers_len++;
    if (parser->flags & F_HOST) {
        ctx->host_at = at;
        ctx->host_len = length;
        parser->flags &= ~F_HOST;
    }

    return 0;
}

static int http_parser_on_body_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *ctx = CONTAINER_OF(parser, http_conn_t, parser);
    ctx->body_at = at;
    ctx->body_len = length;
    return 0;
}

static int http_parser_on_message_complete_cb(llhttp_t *parser) {
    http_conn_t *ctx = CONTAINER_OF(parser, http_conn_t, parser);
    ctx->parser_completed = 1;
    ctx->method = llhttp_get_method(parser);
    return 0;
}


/**
 * libuv processor_run -> uv_run 会在有新的请求时负责触发该回调, 所以当前回调不运行在任何 coroutine 上，所以不要试图进行 coroutine_get
 * 如果存在异常，直接打日志并返回，表示无法处理当前请求即可。暂时不选择崩溃退出 listen coroutine 方案，后续调研后进行整改
 *
 * @param server
 * @param status
 */
static void on_http_conn_cb(uv_stream_t *server, int status) {
    DEBUGF("[on_http_conn_cb] status: %d", status);

    if (status < 0) {
        DEBUGF("[on_http_conn_cb] new connection error: %s", uv_strerror(status));
        return;
    }

    coroutine_t *listen_co = server->data;
    assert(listen_co->p);
    assert(listen_co->data);
    n_processor_t *p = listen_co->p;

    // 初始化 client 数据 accept loop 和 listen loop 必须使用同一个 loop
    http_conn_t *ctx = mallocz(sizeof(http_conn_t));
    assert(ctx);
    ctx->n_server = listen_co->data;
    ctx->read_buf_len = 0;
    ctx->read_buf_cap = 0;
    llhttp_settings_init(&ctx->settings);
    ctx->settings.on_url = http_parser_on_url_cb;
    ctx->settings.on_header_field = http_parser_on_header_field_cb;
    ctx->settings.on_header_value = http_parser_on_header_value_cb;
    ctx->settings.on_body = http_parser_on_body_cb;
    ctx->settings.on_message_complete = http_parser_on_message_complete_cb;

    uv_tcp_init(server->loop, &ctx->handle);
    int result = uv_accept(server, (uv_stream_t *) &ctx->handle);
    if (result) {
        DEBUGF("[on_http_conn_cb] uv_accept failed: %s", uv_strerror(result));
        uv_close((uv_handle_t *) &ctx->handle, on_close_cb);
        return;
    }

    ctx->handle.data = listen_co;
    result = uv_read_start((uv_stream_t *) &ctx->handle, http_alloc_buffer_cb, on_read_cb);
    if (result) {
        DEBUGF("[on_http_conn_cb] uv_read_start failed: %s", uv_strerror(result));
        uv_close((uv_handle_t *) &ctx->handle, on_close_cb);
        return;
    }

    DEBUGF("[accept_new_conn] accept new conn and create client co success, ctx: %p, client_handle: %p", ctx,
           &ctx->handle);
}

static inline void on_server_close_cb(uv_handle_t *handle) {
    n_http_server_t *ctx = handle->data;
    free(ctx->uv_server_handler);

    co_ready(ctx->listen_co);
}

void rt_uv_http_close(n_http_server_t *server_ctx) {
    server_ctx->uv_server_handler->data = server_ctx;
    uv_close(server_ctx->uv_server_handler, on_server_close_cb);
}

/**
 * @param server_ctx
 */
void rt_uv_http_listen(n_http_server_t *server_ctx) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    co->data = server_ctx;

    uv_tcp_t *uv_server = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(&p->uv_loop, uv_server);
    uv_server->data = co;

    server_ctx->listen_co = co;
    server_ctx->uv_server_handler = (uv_handle_t *) uv_server;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(server_ctx->addr), server_ctx->port, &addr);
    uv_tcp_bind(uv_server, (const struct sockaddr *) &addr, 0);


    // 新的请求进来将会触发 new connection 回调
    int result = uv_listen((uv_stream_t *) uv_server, DEFAULT_BACKLOG, on_http_conn_cb);
    if (result) {
        // 端口占用等错误
        rti_co_throw(co, tlsprintf("uv listen failed: %s", uv_strerror(result)), false);
        return;
    }

    DEBUGF("[rt_uv_http_listen] listen success, port=%ld, p_index=%d", server_ctx->port, p->index);
    co_yield_waiting(co, NULL, NULL);
    DEBUGF("[rt_uv_http_listen] listen resume, port=%ld, and return, p_index=%d", server_ctx->port, p->index);
}