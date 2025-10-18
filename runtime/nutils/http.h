#ifndef NUTILS_HTTP_H
#define NUTILS_HTTP_H

#include "runtime/llhttp/llhttp.h"
#include "runtime/processor.h"
#include "runtime/runtime.h"
#include "utils/type.h"

#define HTTP_BUFFER_SIZE 4096 // 4KB
#define HTTP_PARSER_BUF_SIZE 4096
#define HTTP_PARSER_HEADER_LIMIT 100

typedef void (*handler_fn)();

typedef struct {
    void *next;
} freenode_t;

typedef struct {
    uv_tcp_t handle;
    freenode_t *freelist; // 空闲的链接列表？
    int count;
    int max; // 500
    int min; // 50
    coroutine_t *listen_co;
    void *server;
    int64_t conn_count;
    int64_t read_cb_count;
    int64_t closed_count;
    int64_t read_alloc_buf_count;
    int64_t read_error_count;
    int64_t coroutine_count;
    int64_t resp_count;
    int64_t read_start_count; // 记录 uv_read_start 成功的次数
} inner_http_server_t;

/**
* 不能改变顺序
*/
typedef struct {
    handler_fn handler;
    n_string_t *addr;
    n_int_t port;
    void *routers[8];
    inner_http_server_t *inner;
} n_http_server_t;

/**
* 不能改变顺序
*/
typedef struct {
    n_http_server_t *n_server;

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
    int64_t read_buf_len; // 如果 cap < HTTP_BUFFER_SIZE 则存储在 default 中
    int64_t body_len;
    int64_t url_len; // content_length
    int64_t path_len;
    int64_t query_len;
    int64_t host_len;
    int64_t headers_len;

    uint8_t method; // llhttp_method
    // ---- other fields

    char default_buf[HTTP_BUFFER_SIZE];

    uint8_t parser_completed;

    llhttp_t parser;
    llhttp_settings_t settings;

    // libuv data
    uv_tcp_t handle;
    uv_async_t async_write_handle;
    uv_write_t write_req;
    uv_buf_t write_buf;
    int64_t create_time;
} http_conn_t;

void rt_uv_conn_resp(http_conn_t *conn, n_string_t *resp_data);

void rt_uv_http_listen(n_http_server_t *server);

void rt_uv_http_close(n_http_server_t *server);

#endif //LIBUV_H
