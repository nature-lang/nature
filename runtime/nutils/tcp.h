#ifndef NATURE_RUNTIME_NUTILS_TCP_H_
#define NATURE_RUNTIME_NUTILS_TCP_H_

#include "runtime/processor.h"
#include <uv.h>

#define DEFAULT_BACKLOG 128

// 都不需要 gc 吧，直接换？
typedef struct {
    coroutine_t *co;
    int64_t read_len;
    void *data;
    uint8_t async_type; // 0 read/ 1 write/ 2 close
    bool timeout; // 是否触发了 timeout
    uv_tcp_t handle;
    uv_async_t async;
    uv_write_t write_req;
} inner_conn_t;

typedef struct {
    n_string_t *ip;
    n_int_t port;
    uv_handle_t *server_handle;
    coroutine_t *listen_co;
    rt_linkco_list_t waiters; // wait accept
    inner_conn_t *accept_list; // use data
    bool closed;
} n_tcp_server_t;

typedef struct {
    inner_conn_t *conn;
    bool closed;
} n_tcp_conn_t;

static inline void on_conn_close_async_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, async);
    free(conn);
}

static inline void on_conn_close_handle_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, handle);
    // close async
    uv_close((uv_handle_t *) &conn->async, on_conn_close_async_cb);
}

static inline void on_tcp_close_cb(uv_handle_t *handle) {
    free(handle);
}

static inline void on_tcp_read_cb(uv_stream_t *client_handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_tcp_read_cb] client: %p, nread: %ld", client_handle, nread);

    inner_conn_t *conn = client_handle->data;
    conn->read_len = 0;

    if (nread < 0) {
        DEBUGF("[on_tcp_read_cb] uv_read failed: %s", uv_strerror(nread));
    }

    // read data to buf? and set len, 数据已经在 buf 里面了， nread 是读取的数量。
    conn->read_len = nread;

    // 停止持续的 uv_read_start 等待用户下次调用
    uv_read_stop(client_handle);

    // 唤醒 connect co
    DEBUGF("[on_tcp_read_cb] will ready co %p", conn->co);
    co_ready(conn->co);
}

static inline void on_tcp_write_end_cb(uv_write_t *write_req, int status) {
    inner_conn_t *conn = write_req->data;
    if (status < 0) {
        // 对端可能已经关闭了连接,导致写入失败等情况
        char *msg = tlsprintf("uv_write failed: %s", uv_strerror(status));
        DEBUGF("[on_tcp_write_end_cb] failed: %s, co=%p", msg, conn->co);
    }

    co_ready(conn->co);
    DEBUGF("[on_tcp_write_end_cb] co=%p ready, status=%d", conn->co, conn->co->status);
}

static inline void tcp_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    inner_conn_t *conn = handle->data;
    DEBUGF("[tcp_alloc_buffer_cb] suggested_size: %ld", suggested_size);
    buf->base = (void *) ((n_vec_t *) conn->data)->data;
    buf->len = ((n_vec_t *) conn->data)->length;
}

static inline void on_tcp_async_cb(uv_async_t *handle) {
    inner_conn_t *conn = handle->data;
    DEBUGF("[on_tcp_async_cb] start, type=%s, co=%p", conn->async_type ? "write" : "read", conn->co)

    if (conn->async_type == 0) {
        int result = uv_read_start((uv_stream_t *) &conn->handle, tcp_alloc_buffer_cb, on_tcp_read_cb);
        if (result) {
            rti_co_throw(conn->co, tlsprintf("tcp read failed: %s", uv_strerror(result)), false);
            co_ready(conn->co);
            DEBUGF("[on_tcp_async_cb] uv_read_start failed: %s, co=%p", uv_strerror(result), conn->co);
            return;
        }
    } else if (conn->async_type == 1) {
        uv_buf_t write_buf = {
                .base = (void *) ((n_vec_t *) conn->data)->data,
                .len = ((n_vec_t *) conn->data)->length,
        };
        conn->write_req.data = conn;

        int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tcp_write_end_cb);
        if (result) {
            DEBUGF("[on_tcp_async_cb] tcp write failed: %s, co=%p", uv_strerror(result), conn->co);
            rti_co_throw(conn->co, tlsprintf("tcp write failed: %s", uv_strerror(result)), false);
            co_ready(conn->co);
            return;
        }
    } else if (conn->async_type == 2) {
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_handle_cb);
    } else {
        assert(false);
    }
}

static inline bool yield_async_udp_send(coroutine_t *co, void *data) {
    inner_conn_t *conn = data;

    uv_async_send(&conn->async);
    DEBUGF("[yield_async_udp_send] send success, co=%p", co)
    return true;
}

// read once
int64_t rt_uv_tcp_read(n_tcp_conn_t *n_conn, n_vec_t *buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "conn closed", false);
        return 0;
    }

    inner_conn_t *conn = n_conn->conn;
    conn->co = co;

    conn->handle.data = conn;
    conn->data = buf;

    if (conn->handle.loop != &co->p->uv_loop) {
        DEBUGF("[rt_uv_tcp_read] diff loop, conn loop = %p, current loop = %p, co=%p, conn=%p", conn->handle.loop, &co->p->uv_loop, co, conn);

        conn->async_type = 0;
        conn->async.data = conn;

        // 在 co_yield_waiting 的第二个参数中调用 uv_async_send, typedef bool (*unlock_fn)(coroutine_t *co, void *lock_of); 避免线程冲突
        co_yield_waiting(co, yield_async_udp_send, conn);
    } else {
        int result = uv_read_start((uv_stream_t *) &conn->handle, tcp_alloc_buffer_cb, on_tcp_read_cb);
        if (result) {
            rti_co_throw(co, tlsprintf("tcp read failed: %s", uv_strerror(result)), false);
            return 0;
        }
        co_yield_waiting(co, NULL, NULL);
    }

    DEBUGF("[rt_uv_tcp_read] co=%p resume completed, read len: %ld", co, conn->read_len);

    if (conn->read_len < 0) {
        rti_co_throw(co, tlsprintf("tcp read failed: %s", uv_strerror(conn->read_len)), false);
        return 0;
    }

    return conn->read_len;
}

int64_t rt_uv_tcp_write(n_tcp_conn_t *n_conn, n_vec_t *buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "conn closed", false);
        return 0;
    }

    inner_conn_t *conn = n_conn->conn;
    conn->co = co;

    conn->handle.data = conn;
    if (conn->handle.loop != &co->p->uv_loop) {
        DEBUGF("[rt_uv_tcp_write] diff loop, conn=%p, current_co=%p|%p, conn_co=%p|%p", conn, co, &co->p->uv_loop, conn->co, conn->handle.loop);

        conn->data = buf;
        conn->async_type = 1;
        conn->async.data = conn;

        co_yield_waiting(co, yield_async_udp_send, conn);
    } else {
        // TODO buf safe?
        uv_buf_t write_buf = {
                .base = (void *) buf->data,
                .len = buf->length,
        };
        conn->write_req.data = conn;

        int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tcp_write_end_cb);
        if (result) {
            rti_co_throw(co, tlsprintf("tcp write failed: %s", uv_strerror(result)), false);
            DEBUGF("[rt_uv_tcp_write] co=%p, tcp write failed: %s", co, uv_strerror(result));
            return 0;
        }

        co_yield_waiting(co, NULL, NULL);
    }

    DEBUGF("[rt_uv_tcp_write] co=%p, waiting resume", co)

    // not need yield, return directly
    return buf->length;
}

static inline void on_tcp_connect_cb(uv_connect_t *conn_req, int status) {
    DEBUGF("[on_tcp_connect_cb] start")
    inner_conn_t *conn = conn_req->data;
    if (conn->timeout) {
        DEBUGF("[on_tcp_connect_cb] connection timeout, not need handle anything")
        return;
    }

    if (status < 0) {
        DEBUGF("[on_tcp_connect_cb] connection failed: %s", uv_strerror(status));
        rti_co_throw(conn->co, tlsprintf("connection failed: %s", uv_strerror(status)), false);
    } else {
        DEBUGF("[on_tcp_connect_cb] connected. will resume");
    }

    co_ready(conn->co);
}

static inline void on_conn_timeout_cb(uv_timer_t *handle) {
    DEBUGF("[on_conn_timeout_cb] timeout set")

    inner_conn_t *conn = handle->data;
    conn->timeout = true;

    uv_timer_stop(handle);
    rti_co_throw(conn->co, "connection timeout", 0);
    co_ready(conn->co);
}

void rt_uv_tcp_connect(n_tcp_conn_t *n_conn, n_string_t *addr, n_int64_t port, n_int64_t timeout_ms) {
    DEBUGF("[rt_uv_tcp_connect] start, addr %s, port %ld", (char *) rt_string_ref(addr), port)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    inner_conn_t *conn = mallocz(sizeof(inner_conn_t));
    conn->timeout = false;
    conn->data = NULL;
    n_conn->conn = conn;

    conn->co = co;
    DEBUGF("[rt_uv_tcp_connect] malloc new conn=%p, co=%p, p_index=%d", conn, conn->co, p->index);

    uv_tcp_init(&p->uv_loop, &conn->handle);
    uv_async_init(&p->uv_loop, &conn->async, on_tcp_async_cb);


    struct sockaddr_in dest;
    uv_ip4_addr(rt_string_ref(addr), port, &dest);

    uv_connect_t *connect_req = malloc(sizeof(uv_connect_t));
    connect_req->data = conn;
    uv_tcp_connect(connect_req, &conn->handle, (const struct sockaddr *) &dest, on_tcp_connect_cb);

    uv_timer_t *timer = NULL;
    if (timeout_ms > 0) {
        timer = malloc(sizeof(uv_timer_t));
        timer->data = conn;
        uv_timer_init(&p->uv_loop, timer);
        uv_timer_start(timer, on_conn_timeout_cb, timeout_ms, 0); // repeat == 0
    }


    // yield wait conn
    co_yield_waiting(co, NULL, NULL);

    // 释放上面用不到的各种资源
    timer ? free(timer) : 0;
    free(connect_req);

    if (co->has_error) {
        uv_close((uv_handle_t *) &conn->handle, NULL);
        DEBUGF("[rt_uv_tcp_connect] have error");
        return;
    }

    DEBUGF("[rt_uv_tcp_connect] resume, connect success, will return conn=%p", conn)
}

void rt_uv_tcp_accept(n_tcp_server_t *server, n_tcp_conn_t *n_conn) {
    DEBUGF("[rt_uv_tcp_accept] accept start")
    coroutine_t *co = coroutine_get();

    if (&co->p->uv_loop != server->server_handle->loop) {
        rti_co_throw(co, "`accept` must be called in the coroutine where `listen` is located", false);
        return;
    }

    inner_conn_t *conn = NULL;
    while (true) {
        if (server->accept_list == NULL) {
            DEBUGF("[rt_uv_tcp_accept] eagain to wait");

            linkco_list_push_head(&server->waiters, co);

            // yield wait
            co_yield_waiting(co, NULL, NULL);

            DEBUGF("[rt_uv_tcp_accept] eagain resume, will retry accept");
            continue;
        } else {
            // pop from head.
            conn = server->accept_list;
            server->accept_list = server->accept_list->data; // maybe null
            conn->handle.data = conn;
            break;
        }
    }

    n_conn->conn = conn;

    // init async
    uv_async_init(conn->handle.loop, &conn->async, on_tcp_async_cb);

    // accept success, can read
    DEBUGF("[rt_uv_tcp_accept] accept success, inner_conn=%p, co=%p", conn, co);
}

void on_tcp_conn_cb(uv_stream_t *handle, int status) {
    DEBUGF("[on_new_conn_cb] status: %d", status);
    if (status < 0) {
        DEBUGF("[on_new_conn_cb] new connection error: %s", uv_strerror(status));
        return;
    }
    n_tcp_server_t *server = handle->data;

    inner_conn_t *conn = mallocz(sizeof(inner_conn_t));
    //    conn->timeout = 0;
    uv_tcp_init(handle->loop, &conn->handle);

    int result = uv_accept((uv_stream_t *) server->server_handle, (uv_stream_t *) &conn->handle);
    if (result) {
        // close and free client
        uv_close((uv_handle_t *) &conn->handle, NULL);
    } else {
        // push to head
        if (server->accept_list == NULL) {
            server->accept_list = conn;
        } else {
            conn->data = server->accept_list;
            server->accept_list = conn;
        }
    }

    if (server->waiters.count > 0) {
        DEBUGF("[on_new_conn_cb] waiters count = %ld", server->waiters.count)
        // accept 和 new conn 存在竞争关系，所以需要注意加锁
        coroutine_t *co = linkco_list_pop(&server->waiters);
        co_ready(co);
    }

    DEBUGF("[on_new_conn_cb] handle completed");
}

void rt_uv_tcp_listen(n_tcp_server_t *server) {
    DEBUGF("[rt_uv_tcp_listen] start, addr %s, port %ld", (char *) rt_string_ref(server->ip), server->port)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();
    co->data = server;

    uv_tcp_t *uv_server = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(&p->uv_loop, uv_server);

    uv_server->data = server;

    server->listen_co = co;
    server->server_handle = (uv_handle_t *) uv_server;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(server->ip), (int) server->port, &addr);
    uv_tcp_bind(uv_server, (const struct sockaddr *) &addr, 0);

    int result = uv_listen((uv_stream_t *) uv_server, DEFAULT_BACKLOG, on_tcp_conn_cb);
    if (result) {
        // 端口占用等错误
        rti_co_throw(co, tlsprintf("uv listen failed: %s", uv_strerror(result)), false);
        return;
    }

    DEBUGF("[rt_uv_tcp_listen] listen success, will return")
}

void rt_uv_tcp_server_close(n_tcp_server_t *server) {
    if (server->closed) {
        return;
    }

    uv_close(server->server_handle, on_tcp_close_cb);
    server->closed = true;
}

void rt_uv_tcp_conn_close(n_tcp_conn_t *n_conn) {
    if (n_conn->closed) {
        return;
    }
    n_conn->closed = true;

    inner_conn_t *conn = n_conn->conn;
    coroutine_t *co = coroutine_get();

    if (conn->handle.loop != &co->p->uv_loop) {
        DEBUGF("conn(co(%p)) handle loop %p != current co(%p) loop %p", conn->co, conn->handle.loop, co, &co->p->uv_loop);

        conn->async_type = 2;
        conn->async.data = conn;
        uv_async_send(&conn->async);
    } else {
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_handle_cb);
    }
}

#endif //NATURE_RUNTIME_NUTILS_TCP_H_
