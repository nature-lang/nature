#ifndef NUTILS_HTTP_H
#define NUTILS_HTTP_H

#include "runtime/runtime.h"
#include "runtime/processor.h"
#include "utils/type.h"
#include "runtime/llhttp/llhttp.h"

#define READ_BUFFER_SIZE 4096      // 4KB
#define WRITE_BUFFER_SIZE  4096      // 4KB
#define HTTP_PARSER_BUF_SIZE 4096
#define HTTP_PARSER_HEADER_LIMIT 100

extern mutex_t uv_thread_locker;
extern uv_loop_t uv_global_loop;

typedef void (*handler_fn)();

/**
* 不能改变顺序
*/
typedef struct {
    handler_fn handler;
    n_string_t *addr;
    n_int_t port;
    void *routers[8];
    uv_handle_t *uv_server_handler;
    coroutine_t *listen_co;
} n_server_t;

/**
* 不能改变顺序
*/
typedef struct {
    n_server_t *n_server;

    void *read_buf; // 默认申请 4096 空间 buf
    const char *url_at;
    const char *path_at;
    const char *query_at;
    const char *body_at;
    const char *host_at;

    struct {
        const char *name_at;
        const char *value_at;
        int64_t name_len;
        int64_t value_len;
    } headers[HTTP_PARSER_HEADER_LIMIT]; // pointer buf

    int64_t read_buf_cap;
    int64_t read_buf_len;
    int64_t body_len;
    int64_t url_len; // content_length
    int64_t path_len;
    int64_t query_len;
    int64_t host_len;
    int64_t headers_len;

    uint8_t method; // llhttp_method
    uint8_t parser_completed;

    llhttp_t parser;
    llhttp_settings_t settings;

    // libuv data
    uv_tcp_t client_handle;
    uv_async_t async_handle;
    uv_write_t write_req;
    uv_buf_t write_buf;
} conn_ctx_t;

void rt_uv_conn_resp(conn_ctx_t *ctx, n_string_t *resp_data);

void rt_uv_http_listen(n_server_t *server_ctx);

void rt_uv_http_close(n_server_t *server_ctx);

#endif //LIBUV_H
