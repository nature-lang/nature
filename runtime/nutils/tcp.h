#ifndef NATURE_RUNTIME_NUTILS_TCP_H_
#define NATURE_RUNTIME_NUTILS_TCP_H_

#include "runtime/processor.h"
#include <uv.h>

#ifdef __LINUX
#define DEFAULT_BACKLOG 4096
#else
#define DEFAULT_BACKLOG SOMAXCONN
#endif

#define FREELIST_MAX 10000
#define FREELIST_MIN 1000

typedef struct {
    void *next;
} freenode_t;

typedef struct {
    coroutine_t *co;
    int64_t read_len;
    void *data;
    void *server;
    bool timeout; // 是否触发了 timeout
    uv_tcp_t handle;
    uv_write_t write_req;
    uv_connect_t conn_req;
    uv_timer_t timer;
    int ref_count;
} inner_conn_t;

typedef struct {
    uv_tcp_t handle;
    coroutine_t *listen_co;
    rt_linkco_list_t waiters; // wait accept

    coroutine_t *waiters_head;
    coroutine_t *waiters_tail;
    int64_t waiters_count;
    inner_conn_t *accept_head; // use data

    pthread_mutex_t accept_locker;

    freenode_t *freelist;
    int count;
} inner_server_t;

typedef struct {
    n_string_t *ip;
    n_int_t port;
    bool closed;

    inner_server_t *inner;
} n_tcp_server_t;

typedef struct {
    inner_conn_t *conn;
    bool closed;
} n_tcp_conn_t;


#define FREELIST_MAX 10000
#define FREELIST_MIN 1000

static inner_conn_t *acquire_conn(inner_server_t *inner) {
    if (inner->count > 0) {
        freenode_t *node = inner->freelist;
        inner->freelist = node->next;
        node->next = NULL;
        inner->count--;
        return (inner_conn_t *) node;
    }

    return mallocz(sizeof(inner_conn_t));
}

static void release_conn(inner_server_t *inner, inner_conn_t *conn) {
    if (inner->count > FREELIST_MAX) {
        free(conn);
        return;
    }

    memset(conn, 0, sizeof(inner_conn_t));

    freenode_t *node = (freenode_t *) conn;
    node->next = inner->freelist;
    inner->freelist = node;
    inner->count++;
}

static void init_conn(inner_server_t *inner) {
    for (int i = 0; i < FREELIST_MIN; ++i) {
        freenode_t *node = mallocz(sizeof(inner_conn_t));

        node->next = inner->freelist;
        inner->freelist = node;
        inner->count += 1;
    }
}

static void free_conn(inner_server_t *inner) {
    while (inner->freelist) {
        freenode_t *node = inner->freelist;
        inner->freelist = node->next;
        inner->count -= 1;
        free(node);
    }

    free(inner);
}


static inline void on_conn_close_handle_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, handle);
    conn->ref_count -= 1;

    if (conn->ref_count == 0) {
        if (conn->co) {
            co_ready(conn->co);
        }

        if (conn->server) {
            release_conn(conn->server, conn);
        } else {
            free(conn);
        }

        // close async
        DEBUGF("[on_conn_close_handle_cb] ref count = 0, closed and free")
    } else {
        DEBUGF("[on_conn_close_handle_cb] ref count = %d, skip", conn->ref_count);
    }
}

static inline void on_conn_close_timer_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, timer);
    conn->ref_count -= 1;
    if (conn->ref_count == 0) {
        if (conn->co) {
            co_ready(conn->co);
        }

        if (conn->server) {
            release_conn(conn->server, conn);
        } else {
            free(conn);
        }

        DEBUGF("[on_conn_close_timer_cb] ref count = 0, will close async")
    } else {
        DEBUGF("[on_conn_close_timer_cb] ref count = %d, skip", conn->ref_count);
    }
}

static inline void on_tcp_close_cb(uv_handle_t *handle) {
    inner_server_t *inner = handle->data;
    free(inner);
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

void uv_async_tcp_read(inner_conn_t *conn, n_vec_t *buf) {
    int result = uv_read_start((uv_stream_t *) &conn->handle, tcp_alloc_buffer_cb, on_tcp_read_cb);
    if (result) {
        rti_co_throw(conn->co, tlsprintf("tcp read failed: %s", uv_strerror(result)), false);
        co_ready(conn->co);
        return;
    }
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

    global_waiting_send(uv_async_tcp_read, conn, buf, 0);
    DEBUGF("[rt_uv_tcp_read] co=%p resume completed, read len: %ld", co, conn->read_len);

    if (conn->read_len < 0) {
        rti_co_throw(co, uv_strerror(conn->read_len), false);
        return 0;
    }

    return conn->read_len;
}

static void uv_async_tcp_write(inner_conn_t *conn, n_vec_t *buf) {
    uv_buf_t write_buf = {
            .base = (void *) buf->data,
            .len = buf->length,
    };
    conn->write_req.data = conn;

    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tcp_write_end_cb);
    if (result) {
        rti_co_throw(conn->co, tlsprintf("tcp write failed: %s", uv_strerror(result)), false);
        DEBUGF("[rt_uv_tcp_write] co=%p, tcp write failed: %s", conn->co, uv_strerror(result));
        co_ready(conn->co);
    }
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

    global_waiting_send(uv_async_tcp_write, conn, buf, 0);

    DEBUGF("[rt_uv_tcp_write] co=%p, waiting resume", co)

    // not need yield, return directly
    return buf->length;
}

static inline void on_tcp_connect_cb(uv_connect_t *conn_req, int status) {
    DEBUGF("[on_tcp_connect_cb] start")
    inner_conn_t *conn = CONTAINER_OF(conn_req, inner_conn_t, conn_req);

    if (conn->timeout) {
        DEBUGF("[on_tcp_connect_cb] connection timeout, not need handle anything")
        return;
    }

    if (uv_is_active((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
        uv_close((uv_handle_t *) &conn->timer, on_conn_close_timer_cb);
    }

    if (status < 0) {
        DEBUGF("[on_tcp_connect_cb] connection failed: %s", uv_strerror(status));
        rti_co_throw(conn->co, tlsprintf("connection failed: %s", uv_strerror(status)), false);
    }

    co_ready(conn->co);
}

static inline void on_tcp_timeout_cb(uv_timer_t *handle) {
    DEBUGF("[on_tcp_timeout_cb] timeout set")

    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, timer);
    conn->timeout = true;

    uv_timer_stop(handle);
    uv_close((uv_handle_t *) handle, on_conn_close_timer_cb);

    rti_co_throw(conn->co, "connection timeout", 0);
    co_ready(conn->co);
}

static void uv_async_tcp_connect(inner_conn_t *conn, struct sockaddr_in *dest, n_int64_t timeout_ms) {
    DEBUGF("[uv_async_tcp_connect] start, timeout_ms=%ld, dest=%p", timeout_ms, dest)
    uv_tcp_init(&global_loop, &conn->handle);

    uv_tcp_connect(&conn->conn_req, &conn->handle, (const struct sockaddr *) dest, on_tcp_connect_cb);

    free(dest);

    if (timeout_ms > 0) {
        conn->ref_count += 1;
        uv_timer_init(&global_loop, &conn->timer);
        uv_timer_start(&conn->timer, on_tcp_timeout_cb, timeout_ms, 0); // repeat == 0
    }
}

void rt_uv_tcp_connect(n_tcp_conn_t *n_conn, n_string_t *addr, n_int64_t port, n_int64_t timeout_ms) {
    DEBUGF("[rt_uv_tcp_connect] start, addr %s, port %ld", (char *) rt_string_ref(addr), port)

    coroutine_t *co = coroutine_get();

    struct sockaddr_in *dest = malloc(sizeof(struct sockaddr_in));
    uv_ip4_addr(rt_string_ref(addr), (int) port, dest);

    inner_conn_t *conn = mallocz(sizeof(inner_conn_t));
    conn->timeout = false;
    conn->data = NULL;
    conn->ref_count = 1;
    n_conn->conn = conn;
    conn->co = co;

    global_waiting_send(uv_async_tcp_connect, conn, dest, (void *) timeout_ms);

    DEBUGF("[rt_uv_tcp_connect] resume, connect success, will return conn=%p", conn)
}

void uv_async_tcp_accept(inner_server_t *inner_server, coroutine_t *co) {
    co->next = NULL;

    // push to tail
    if (inner_server->waiters_head == NULL) {
        inner_server->waiters_head = co;
        inner_server->waiters_tail = co;
    } else {
        inner_server->waiters_tail->next = co;
        inner_server->waiters_tail = co;
    }

    inner_server->waiters_count += 1;
}

void rt_uv_tcp_accept(n_tcp_server_t *server, n_tcp_conn_t *n_conn) {
    DEBUGF("[rt_uv_tcp_accept] accept start")
    coroutine_t *co = coroutine_get();
    inner_server_t *inner_server = server->inner;
    inner_conn_t *conn = NULL;

    while (true) {
        pthread_mutex_lock(&inner_server->accept_locker);
        if (inner_server->accept_head == NULL) {
            pthread_mutex_unlock(&inner_server->accept_locker);

            DEBUGF("[rt_uv_tcp_accept] eagain to wait");
            global_waiting_send(uv_async_tcp_accept, inner_server, co, NULL);
            DEBUGF("[rt_uv_tcp_accept] eagain resume, will retry accept");
            continue;
        } else {
            conn = inner_server->accept_head;
            inner_server->accept_head = inner_server->accept_head->data; // maybe null
            pthread_mutex_unlock(&inner_server->accept_locker);

            conn->handle.data = conn;
            break;
        }
    }

    conn->ref_count = 1;
    n_conn->conn = conn;
    // accept success, can read
    DEBUGF("[rt_uv_tcp_accept] accept success, inner_conn=%p, co=%p", conn, co);
}

void on_tcp_conn_cb(uv_stream_t *handle, int status) {
    DEBUGF("[on_new_conn_cb] status: %d", status);
    if (status < 0) {
        DEBUGF("[on_new_conn_cb] new connection error: %s", uv_strerror(status));
        return;
    }
    inner_server_t *inner_server = handle->data;
    inner_conn_t *conn = acquire_conn(inner_server);
    conn->server = inner_server;

    uv_tcp_init(handle->loop, &conn->handle);

    int result = uv_accept((uv_stream_t *) &inner_server->handle, (uv_stream_t *) &conn->handle);
    if (result) {
        uv_close((uv_handle_t *) &conn->handle, NULL);
    } else {
        pthread_mutex_lock(&inner_server->accept_locker);
        // push to head
        if (inner_server->accept_head == NULL) {
            inner_server->accept_head = conn;
        } else {
            conn->data = inner_server->accept_head;
            inner_server->accept_head = conn;
        }
        pthread_mutex_unlock(&inner_server->accept_locker);
    }

    // 唤醒一个 pop and coroutine
    if (inner_server->waiters_count > 0) {
        DEBUGF("[on_new_conn_cb] waiters count = %ld", inner_server->waiters.count)
        assert(inner_server->waiters_head);

        // head pop
        coroutine_t *co = inner_server->waiters_head;
        inner_server->waiters_head = inner_server->waiters_head->next;
        inner_server->waiters_count--;
        if (inner_server->waiters_head == NULL) {
            inner_server->waiters_tail = NULL;
        }

        co_ready(co);
    }

    DEBUGF("[on_new_conn_cb] handle completed");
}

static void uv_async_tcp_listen(n_tcp_server_t *server) {
    uv_tcp_init(&global_loop, &server->inner->handle);
    server->inner->handle.data = server->inner;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(server->ip), (int) server->port, &addr);
    uv_tcp_bind(&server->inner->handle, (const struct sockaddr *) &addr, 0);

    int result = uv_listen((uv_stream_t *) &server->inner->handle, DEFAULT_BACKLOG, on_tcp_conn_cb);
    if (result) {
        // 端口占用等错误
        rti_co_throw(server->inner->listen_co, tlsprintf("listen failed: %s", uv_strerror(result)), false);
        return;
    }

    co_ready(server->inner->listen_co);
}

void rt_uv_tcp_listen(n_tcp_server_t *server) {
    DEBUGF("[rt_uv_tcp_listen] start, addr %s, port %ld", (char *) rt_string_ref(server->ip), server->port)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    server->inner = mallocz(sizeof(inner_server_t));

    co->data = server;
    server->inner->listen_co = co;

    global_waiting_send(uv_async_tcp_listen, server, 0, 0);

    init_conn(server->inner);
    DEBUGF("[rt_uv_tcp_listen] listen success, will return")
}

void uv_async_server_close(n_tcp_server_t *server) {
    uv_close((uv_handle_t *) &server->inner->handle, on_tcp_close_cb);
}

void rt_uv_tcp_server_close(n_tcp_server_t *server) {
    if (server->closed) {
        return;
    }

    server->closed = true;
    free_conn(server->inner);

    global_async_send(uv_async_server_close, server, NULL, NULL);
}

void uv_async_conn_close(inner_conn_t *conn) {
    uv_close((uv_handle_t *) &conn->handle, on_conn_close_handle_cb);
}

void rt_uv_tcp_conn_close(n_tcp_conn_t *n_conn) {
    DEBUGF("[rt_uv_tcp_conn_close] start")
    if (n_conn->closed) {
        return;
    }

    n_conn->closed = true;
    inner_conn_t *conn = n_conn->conn;
    coroutine_t *co = coroutine_get();
    conn->co = co;
    global_waiting_send(uv_async_conn_close, conn, 0, 0);
}

#endif //NATURE_RUNTIME_NUTILS_TCP_H_
