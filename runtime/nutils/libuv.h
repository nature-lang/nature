#ifndef NUTILS_LIBUV_H
#define NUTILS_LIBUV_H

#include "runtime/runtime.h"
#include "runtime/processor.h"
#include "utils/type.h"

extern mutex_t uv_thread_locker;
extern uv_loop_t uv_global_loop;

typedef void (*handler_fn)();

typedef struct {
    void *routes;

    handler_fn handler;

    n_string_t *addr;
    n_int_t port;
} n_server_t;

/**
* 不能改变顺序
*/
typedef struct {
    n_server_t *n_server;
    void *read_buf_base; // must '\0' end
    uv_tcp_t client_handle;
    uv_async_t async_handle;
    uv_write_t write_req;
    uv_buf_t write_buf; // malloc c
} conn_ctx_t;

void rt_uv_conn_resp(conn_ctx_t *ctx, n_string_t *resp_data);

void rt_uv_http_listen(n_server_t *n_server);

#endif //LIBUV_H
