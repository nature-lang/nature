#include "http.h"
#include "runtime/processor.h"
#include "runtime/rt_mutex.h"
#include "runtime/runtime.h"
#include <strings.h>
#include <sys/socket.h>

// SOMAXCONN 在 sys/socket.h 中定义
#ifdef __LINUX
#define DEFAULT_BACKLOG 4096
#else
#define DEFAULT_BACKLOG SOMAXCONN
#endif

#define FREELIST_MAX 10000
#define FREELIST_MIN 1000

static http_conn_t *acquire_conn(inner_http_server_t *inner) {
    if (inner->count > 0) {
        freenode_t *node = inner->freelist;
        inner->freelist = node->next;
        node->next = NULL;
        inner->count--;
        return (http_conn_t *) node;
    }

    //    TDEBUGF("[acquire_conn] malloc count %d", malloc_count++);
    return mallocz(sizeof(http_conn_t));
}

static void release_conn(inner_http_server_t *inner, http_conn_t *conn) {
    if (inner->count > inner->max) {
        free(conn);
        return;
    }

    memset(conn, 0, sizeof(http_conn_t));

    freenode_t *node = (freenode_t *) conn;
    node->next = inner->freelist;
    inner->freelist = node;
    inner->count++;
}

static void init_conn(inner_http_server_t *inner) {
    for (int i = 0; i < inner->min; ++i) {
        freenode_t *node = mallocz(sizeof(http_conn_t));

        node->next = inner->freelist;
        inner->freelist = node;
        inner->count += 1;
    }
}

static void free_conn(inner_http_server_t *inner) {
    while (inner->freelist) {
        freenode_t *node = inner->freelist;
        inner->freelist = node->next;
        inner->count -= 1;
        free(node);
    }

    free(inner);
}

static inline void on_async_conn_close_cb(uv_handle_t *handle) {
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, async_write_handle);
    DEBUGF("[on_async_conn_close_cb] conn: %p", conn);
    assert(conn->n_server);
    inner_http_server_t *inner = conn->n_server->inner;


    conn->read_buf_len = 0;
    conn->read_buf_cap = 0;
    free(conn->read_buf);
    if (conn->write_buf.base != conn->default_buf) {
        free(conn->write_buf.base);
    }

    conn->n_server->inner->closed_count += 1;
    release_conn(inner, conn);
}

static inline void on_conn_close_cb(uv_handle_t *handle) {
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, handle);
    DEBUGF("[on_conn_close_cb] conn, %p, client_handle: %p, async_handle: %p",
           conn, &conn->handle, conn->async_write_handle);

    // 需要先关闭 async 才能释放内存
    if (uv_is_active((uv_handle_t *) &conn->async_write_handle)) {
        uv_close((uv_handle_t *) &conn->async_write_handle, on_async_conn_close_cb);
    } else {
        assert(false);
        DEBUGF("[on_conn_close_cb] close failed, async_write_handle not active")
    }
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

    uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
}

/**
 * @param handle
 * @param suggested_size
 * @param buf
 */
static inline void http_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, handle);

    conn->n_server->inner->read_alloc_buf_count += 1;

    DEBUGF("[uv_alloc_buffer] suggested_size: %ld", suggested_size);
    conn->read_buf_cap += HTTP_BUFFER_SIZE;

    if (conn->read_buf_cap == HTTP_BUFFER_SIZE) {
        buf->base = conn->default_buf;
        buf->len = HTTP_BUFFER_SIZE;
        return;
    }

    // need malloc
    if (!conn->read_buf) {
        conn->read_buf = malloc(conn->read_buf_cap);
        memmove(conn->read_buf, conn->default_buf, HTTP_BUFFER_SIZE);
    } else {
        conn->read_buf = realloc(conn->read_buf, conn->read_buf_cap);
    }

    buf->base = conn->read_buf + conn->read_buf_len;
    buf->len = HTTP_BUFFER_SIZE;
}

static inline void async_conn_write_handle_cb(uv_async_t *handle) {
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, async_write_handle);
    DEBUGF("[async_conn_write_handle_cb] conn: %p, client_handle: %p", conn, handle);
    conn->n_server->inner->resp_count += 1;

    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &conn->write_buf, 1, on_write_end_cb);
    if (result) {
        DEBUGF("[async_conn_write_handle_cb] uv_write have err %s, is_closing %d", uv_strerror(result),
               uv_is_closing((uv_handle_t *) &conn->handle));
        if (!uv_is_closing((uv_handle_t *) &conn->handle)) {
            uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
        }
    }
}

static inline void on_read_cb(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_read_cb] client: %p, nread: %ld", handle, nread);
    http_conn_t *conn = CONTAINER_OF(handle, http_conn_t, handle);
    conn->n_server->inner->read_cb_count += 1;

    if (nread < 0) {
        conn->n_server->inner->read_error_count += 1;

        if (nread == UV_EOF) {
            // do nothing
            if (!conn->parser_completed) {
                DEBUGF("[on_read_cb] uv_read EOF before complete");
            }
        } else {
            DEBUGF("[on_read_cb] uv_read failed: %s", uv_strerror(nread));
        }

        uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
        return;
    }

    enum llhttp_errno err = llhttp_execute(&conn->parser, buf->base, nread);
    if (err != HPE_OK) {
        DEBUGF("[on_read_cb] llhttp_execute failed: %s", llhttp_errno_name(err));
        // 解析错误
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
        return;
    }
    conn->read_buf_len += nread;
    if (!conn->parser_completed) {
        // 未完整读取
        DEBUGF("[on_read_cb] parser not completed yet");
        return;
    }

    conn->n_server->inner->coroutine_count += 1;
    coroutine_t *conn_co = rt_coroutine_new(conn->n_server->handler, 0, NULL, conn);
    rt_coroutine_dispatch(conn_co);
}

void rt_uv_conn_resp(http_conn_t *conn, n_string_t resp_data) {
    // 进行数据 copy 避免 resp_data 后续被清理掉
    if ((resp_data.length + 1) < HTTP_BUFFER_SIZE) {
        conn->write_buf.base = conn->default_buf;
        conn->write_buf.len = resp_data.length;

        memmove(conn->write_buf.base, resp_data.data, resp_data.length);
    } else {
        // TODO write buf base 没有进行释放
        conn->write_buf.base = malloc(resp_data.length + 1);
        conn->write_buf.len = resp_data.length;
        memmove(conn->write_buf.base, resp_data.data, resp_data.length);
    }

    // 通知主线程触发 write, 当前 coroutine 直接返回，不 wait
    uv_async_send(&conn->async_write_handle);
}

static int http_parser_on_url_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *conn = CONTAINER_OF(parser, http_conn_t, parser);
    conn->url_at = at;
    conn->url_len = length;

    conn->path_at = at;
    conn->path_len = length; // 如果不存在 ? 和 #， path 长度就是整个 url 长度

    const char *question_mark = memchr(at, '?', length);
    if (question_mark) {
        conn->path_len = question_mark - at;
        conn->query_at = question_mark + 1;
        conn->query_len = length - conn->path_len - 1;
    }

    return 0;
}

static int http_parser_on_header_field_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *conn = CONTAINER_OF(parser, http_conn_t, parser);
    conn->headers[conn->headers_len].name_at = at;
    conn->headers[conn->headers_len].name_len = length;

    if (length == 4 && (*(uint32_t *) at == 0x74736F48)) {
        parser->flags |= F_HOST; // 标记发现 Host 字段
    }

    return 0;
}

static int http_parser_on_header_value_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *conn = CONTAINER_OF(parser, http_conn_t, parser);
    conn->headers[conn->headers_len].value_at = at;
    conn->headers[conn->headers_len].value_len = length;
    conn->headers_len++;
    if (parser->flags & F_HOST) {
        conn->host_at = at;
        conn->host_len = length;
        parser->flags &= ~F_HOST;
    }

    return 0;
}

static int http_parser_on_body_cb(llhttp_t *parser, const char *at, size_t length) {
    http_conn_t *conn = CONTAINER_OF(parser, http_conn_t, parser);
    conn->body_at = at;
    conn->body_len = length;
    return 0;
}

static int http_parser_on_message_complete_cb(llhttp_t *parser) {
    http_conn_t *conn = CONTAINER_OF(parser, http_conn_t, parser);
    conn->parser_completed = 1;
    conn->method = llhttp_get_method(parser);
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
    inner_http_server_t *inner = CONTAINER_OF(server, inner_http_server_t, handle);
    if (status < 0) {
        DEBUGF("[on_http_conn_cb] new connection error: %s", uv_strerror(status));
        return;
    }

    coroutine_t *listen_co = inner->listen_co;

    // 初始化 client 数据 accept loop 和 listen loop 必须使用同一个 loop
    http_conn_t *conn = acquire_conn(inner);

    conn->create_time = uv_hrtime();
    conn->n_server = inner->server;
    llhttp_settings_init(&conn->settings);
    conn->settings.on_url = http_parser_on_url_cb;
    conn->settings.on_header_field = http_parser_on_header_field_cb;
    conn->settings.on_header_value = http_parser_on_header_value_cb;
    conn->settings.on_body = http_parser_on_body_cb;
    conn->settings.on_message_complete = http_parser_on_message_complete_cb;
    uv_async_init(&global_loop, &conn->async_write_handle, async_conn_write_handle_cb);
    llhttp_init(&conn->parser, HTTP_REQUEST, &conn->settings);

    uv_tcp_init(&global_loop, &conn->handle);
    int result = uv_accept(server, (uv_stream_t *) &conn->handle);
    if (result) {
        DEBUGF("[on_http_conn_cb] uv_accept failed: %s", uv_strerror(result));
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
        return;
    }

    inner->conn_count += 1;
    // 直接进行 conn 读取
    conn->handle.data = listen_co;
    result = uv_read_start((uv_stream_t *) &conn->handle, http_alloc_buffer_cb, on_read_cb);
    if (result) {
        DEBUGF("[on_http_conn_cb] uv_read_start failed: %s", uv_strerror(result));
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_cb);
        return;
    }
    inner->read_start_count += 1; // 记录成功启动读取的连接
    DEBUGF("[accept_new_conn] accept new conn and create client co success, conn: %p, client_handle: %p", conn,
           &conn->handle);

    uv_async_send(&global.async_weak);
}

static inline void on_server_close_cb(uv_handle_t *handle) {
    n_http_server_t *conn = handle->data;
    coroutine_t *listen_co = conn->inner->listen_co;

    free_conn(conn->inner);
    co_ready(listen_co);
}

static void uv_async_http_close(n_http_server_t *server) {
    uv_close((uv_handle_t *) &server->inner->handle, on_server_close_cb);
}

void test_timer_dump_count_cb(uv_timer_t *timer) {
    inner_http_server_t *inner = timer->data;

    int64_t pending_read = inner->read_start_count - inner->read_cb_count;
    int64_t pending_close = inner->read_cb_count - inner->closed_count;
    int64_t leaked = inner->conn_count - inner->closed_count;

    DEBUGF("[app_metrics] conn=%ld, read_start=%ld, alloc_cb=%ld, read_cb=%ld, read_error=%ld, "
           "resp=%ld, closed=%ld, "
           "pending_read=%ld, pending_close=%ld, leaked=%ld",
           inner->conn_count, inner->read_start_count, inner->read_alloc_buf_count, inner->read_cb_count,
           inner->read_error_count,
           inner->resp_count, inner->closed_count,
           pending_read, pending_close, leaked);
}


// 由 libuv 通用 async 触发的回调器
static void uv_async_http_listen(inner_http_server_t *inner) {
    uv_tcp_init(&global_loop, &inner->handle);
    inner->handle.data = inner->listen_co;
    struct sockaddr_in addr;

    n_http_server_t *server = inner->server;

    // TODO 测试逻辑
    //    uv_timer_t *timer = mallocz(sizeof(uv_timer_t));
    //    uv_timer_init(&global_loop, timer);
    //    timer->data = inner;
    //    uv_timer_start(timer, test_timer_dump_count_cb, 1000, 1000);

    uv_ip4_addr(rt_string_ref(&server->addr), server->port, &addr);
    uv_tcp_bind(&inner->handle, (const struct sockaddr *) &addr, 0);

    int result = uv_listen((uv_stream_t *) &inner->handle, DEFAULT_BACKLOG, on_http_conn_cb);
    if (result) {
        rti_co_throw(inner->listen_co, tlsprintf("listen failed: %s", uv_strerror(result)), false);
        co_ready(inner->listen_co);
    }
}


void rt_uv_http_close(n_http_server_t *server) {
    global_async_send(uv_async_http_close, server, 0, 0);
}

/**
 * @param server
 */
void rt_uv_http_listen(n_http_server_t *server) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    inner_http_server_t *inner = malloc(sizeof(inner_http_server_t));
    inner->count = 0;
    inner->freelist = NULL;
    inner->max = FREELIST_MAX;
    inner->min = FREELIST_MIN;
    init_conn(inner);
    inner->listen_co = co;
    inner->server = server;
    server->inner = inner;

    global_waiting_send(uv_async_http_listen, inner, 0, 0);

    DEBUGF("[rt_uv_http_listen] listen resume, port=%ld, and return, p_index=%d", server->port, p->index);
}
