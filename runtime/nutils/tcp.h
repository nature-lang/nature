#ifndef NATURE_RUNTIME_NUTILS_TCP_H_
#define NATURE_RUNTIME_NUTILS_TCP_H_

#include "runtime/processor.h"
#include "runtime/uv_compat.h"

#define DEFAULT_BACKLOG 4096

#define FREELIST_MAX 10000
#define FREELIST_MIN 1000

typedef struct {
    void *next;
} freenode_t;

typedef struct {
    coroutine_t *co;
    coroutine_t *read_co;
    int64_t read_len;
    void *data;
    void *server;
    bool timeout; // 是否触发了 timeout
    bool read_waiting;
    bool read_started;
    int64_t read_timeout_ms;
    uv_tcp_t handle;
    uv_write_t write_req;
    uv_connect_t conn_req;
    uv_timer_t timer;
    int ref_count;
    n_vec_t buf;
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
    int accept_waiters;
} inner_server_t;

typedef struct {
    n_string_t ip;
    n_int_t port;
    bool closed;

    inner_server_t *inner;
} n_tcp_server_t;

typedef struct {
    inner_conn_t *conn;
    bool closed;
    int64_t read_timeout_ms;
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


static inline void conn_release(inner_conn_t *conn) {
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

        DEBUGF("[conn_release] ref count = 0, freed")
    } else {
        DEBUGF("[conn_release] ref count = %d, skip", conn->ref_count);
    }
}

static inline void on_conn_close_handle_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, handle);
    conn_release(conn);
}

static inline void on_conn_close_timer_cb(uv_handle_t *handle) {
    inner_conn_t *conn = CONTAINER_OF(handle, inner_conn_t, timer);
    conn_release(conn);
}

static inline void on_tcp_close_cb(uv_handle_t *handle) {
    inner_server_t *inner = handle->data;
    // Keep the listener state alive until every blocked accept has observed
    // the close callback and released its waiter reference.
    handle->data = NULL;
    if (inner->accept_waiters == 0) {
        free_conn(inner);
    }
}

static inline void on_tcp_read_cb(uv_stream_t *client_handle, ssize_t nread, const uv_buf_t *buf) {
    inner_conn_t *conn = client_handle->data;
    DEBUGF("[on_tcp_read_cb] client: %p, nread: %ld, co: %p", client_handle, nread, conn->co);

    if (!conn->read_waiting) {
        return;
    }

    conn->read_len = 0;

    if (nread < 0) {
        DEBUGF("[on_tcp_read_cb] uv_read failed: %s, co: %p", uv_strerror(nread), conn->co);
    }

    // read data to buf? and set len, 数据已经在 buf 里面了， nread 是读取的数量。
    conn->read_len = nread;

    if (uv_is_active((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
    }

    // 停止持续的 uv_read_start 等待用户下次调用
    uv_read_stop(client_handle);
    conn->read_waiting = false;
    conn->read_started = false;

    co_ready(conn->read_co);
}

static inline void on_tcp_read_timeout_cb(uv_timer_t *timer) {
    inner_conn_t *conn = CONTAINER_OF(timer, inner_conn_t, timer);
    if (!conn->read_waiting) {
        return;
    }

    uv_timer_stop(timer);
    uv_read_stop((uv_stream_t *) &conn->handle);
    conn->read_len = UV_ETIMEDOUT;
    conn->read_waiting = false;
    conn->read_started = false;
    co_ready(conn->read_co);
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
    buf->base = (void *) conn->buf.data;
    buf->len = conn->buf.length;
}

void uv_async_tcp_read(inner_conn_t *conn) {
    if (!conn->read_waiting) {
        return;
    }
    if (uv_is_closing((uv_handle_t *) &conn->handle)) {
        conn->read_len = UV_ECANCELED;
        conn->read_waiting = false;
        conn->read_started = false;
        co_ready(conn->read_co);
        return;
    }
    conn->read_started = true;
    int result = uv_read_start((uv_stream_t *) &conn->handle, tcp_alloc_buffer_cb, on_tcp_read_cb);
    if (result < 0) {
        conn->read_len = result;
        conn->read_waiting = false;
        conn->read_started = false;
        co_ready(conn->read_co);
        return;
    }
    if (conn->read_timeout_ms > 0 && !uv_is_closing((uv_handle_t *) &conn->timer)) {
        uv_timer_start(&conn->timer, on_tcp_read_timeout_cb, conn->read_timeout_ms, 0);
    }
}

// read once
int64_t rt_uv_tcp_read(n_tcp_conn_t *n_conn, n_vec_t buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "conn closed", false);
        return 0;
    }

    inner_conn_t *conn = n_conn->conn;
    conn->read_co = co;
    conn->read_waiting = true;
    conn->read_started = false;
    conn->read_timeout_ms = n_conn->read_timeout_ms;
    conn->ref_count += 1;

    conn->handle.data = conn;
    conn->buf = buf;

    global_waiting_send(uv_async_tcp_read, conn, 0, 0);
    DEBUGF("[rt_uv_tcp_read] co=%p resume completed, read len: %ld", co, conn->read_len);

    int64_t read_len = conn->read_len;
    bool closed = n_conn->closed;
    conn->read_co = NULL;
    conn_release(conn);

    if (closed) {
        rti_co_throw(co, "conn closed", false);
        return 0;
    }
    if (read_len == UV_ETIMEDOUT) {
        rti_co_throw(co, "tcp read timeout", false);
        return 0;
    }
    if (read_len < 0) {
        rti_co_throw(co, uv_strerror(read_len), false);
        return 0;
    }

    return read_len;
}

static void uv_async_tcp_write(inner_conn_t *conn) {
    uv_buf_t write_buf = {
            .base = (void *) conn->buf.data,
            .len = conn->buf.length,
    };
    conn->write_req.data = conn;

    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tcp_write_end_cb);
    if (result < 0) {
        rti_co_throw(conn->co, tlsprintf("tcp write failed: %s", uv_strerror(result)), false);
        DEBUGF("[rt_uv_tcp_write] co=%p, tcp write failed: %s", conn->co, uv_strerror(result));
        co_ready(conn->co);
    }
}

int64_t rt_uv_tcp_write(n_tcp_conn_t *n_conn, n_vec_t buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "conn closed", false);
        return 0;
    }

    inner_conn_t *conn = n_conn->conn;
    conn->co = co;
    conn->handle.data = conn;
    conn->buf = buf;

    global_waiting_send(uv_async_tcp_write, conn, 0, 0);

    DEBUGF("[rt_uv_tcp_write] co=%p, waiting resume", co)

    // not need yield, return directly
    return buf.length;
}

static inline void on_tcp_connect_cb(uv_connect_t *conn_req, int status) {
    DEBUGF("[on_tcp_connect_cb] start, status %d", status)
    inner_conn_t *conn = CONTAINER_OF(conn_req, inner_conn_t, conn_req);

    if (conn->timeout) {
        DEBUGF("[on_tcp_connect_cb] connection timeout, not need handle anything")
        return;
    }

    if (uv_is_active((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
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
    uv_timer_init(&global_loop, &conn->timer);

    uv_tcp_connect(&conn->conn_req, &conn->handle, (const struct sockaddr *) dest, on_tcp_connect_cb);

    free(dest);

    if (timeout_ms > 0) {
        uv_timer_start(&conn->timer, on_tcp_timeout_cb, timeout_ms, 0); // repeat == 0
    }
}

void rt_uv_tcp_connect(n_tcp_conn_t *n_conn, n_string_t ip, n_int64_t port, n_int64_t timeout_ms) {
    DEBUGF("[rt_uv_tcp_connect] start, addr %s, port %ld, co=%p", (char *) rt_string_ref(&ip), port, coroutine_get())

    coroutine_t *co = coroutine_get();

    struct sockaddr_in *dest = malloc(sizeof(struct sockaddr_in));
    uv_ip4_addr(rt_string_ref(&ip), (int) port, dest);

    inner_conn_t *conn = mallocz(sizeof(inner_conn_t));
    conn->timeout = false;
    conn->data = NULL;
    // One reference belongs to the TCP handle and one keeps the shared timer
    // alive for both connect and read timeouts. Close callbacks release them.
    conn->ref_count = 2;
    n_conn->conn = conn;
    conn->co = co;

    global_waiting_send(uv_async_tcp_connect, conn, dest, (void *) timeout_ms);

    DEBUGF("[rt_uv_tcp_connect] resume, connect success, will return conn=%p, co=%p", conn, co)
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
    coroutine_t *co = coroutine_get();
    DEBUGF("[rt_uv_tcp_accept] accept start, co=%p", co)

    if (server->closed) {
        rti_co_throw(co, "server closed", false);
        return;
    }
    inner_server_t *inner_server = server->inner;
    inner_server->accept_waiters += 1;
    inner_conn_t *conn = NULL;

    while (true) {
        if (server->closed) {
            if (inner_server->handle.data != NULL) {
                rt_coroutine_sleep(1);
                continue;
            }
            inner_server->accept_waiters -= 1;
            if (inner_server->accept_waiters == 0) {
                free_conn(inner_server);
            }
            rti_co_throw(co, "server closed", false);
            return;
        }
        if (inner_server->accept_head == NULL) {
            rt_coroutine_sleep(1);
            continue;
        }

        pthread_mutex_lock(&inner_server->accept_locker);
        if (inner_server->accept_head == NULL) {
            pthread_mutex_unlock(&inner_server->accept_locker);
            continue;
        }

        conn = inner_server->accept_head;
        inner_server->accept_head = inner_server->accept_head->data; // maybe null
        pthread_mutex_unlock(&inner_server->accept_locker);

        conn->handle.data = conn;
        inner_server->accept_waiters -= 1;
        break;
    }

    conn->ref_count = 2;
    n_conn->conn = conn;
    // accept success, can read
    DEBUGF("[rt_uv_tcp_accept] accept success, inner_conn=%p, co=%p", conn, co);
}

void on_tcp_conn_cb(uv_stream_t *handle, int status) {
    inner_server_t *inner_server = handle->data;
    inner_conn_t *conn = acquire_conn(inner_server);

    DEBUGF("[on_new_conn_cb] status: %d, co=%p", status, conn->co);
    if (status < 0) {
        DEBUGF("[on_new_conn_cb] new connection error: %s", uv_strerror(status));
        return;
    }

    conn->server = inner_server;

    uv_tcp_init(handle->loop, &conn->handle);
    uv_timer_init(handle->loop, &conn->timer);

    int result = uv_accept((uv_stream_t *) &inner_server->handle, (uv_stream_t *) &conn->handle);
    if (result < 0) {
        uv_close((uv_handle_t *) &conn->handle, NULL);
        release_conn(inner_server, conn);
        return;
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
    //    if (inner_server->waiters_count > 0) {
    //        DEBUGF("[on_new_conn_cb] waiters count = %ld", inner_server->waiters.count)
    //        assert(inner_server->waiters_head);
    //
    //        // head pop
    //        coroutine_t *co = inner_server->waiters_head;
    //        inner_server->waiters_head = inner_server->waiters_head->next;
    //        inner_server->waiters_count--;
    //        if (inner_server->waiters_head == NULL) {
    //            inner_server->waiters_tail = NULL;
    //        }
    //
    //        co_ready(co);
    //    }

    DEBUGF("[on_new_conn_cb] handle completed, co=%p", conn->co);
}

static void uv_async_tcp_listen(n_tcp_server_t *server) {
    uv_tcp_init(&global_loop, &server->inner->handle);
    server->inner->handle.data = server->inner;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(&server->ip), (int) server->port, &addr);
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
    DEBUGF("[rt_uv_tcp_listen] start, addr %s, port %ld", (char *) rt_string_ref(&server->ip), server->port)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    server->inner = mallocz(sizeof(inner_server_t));

    pthread_mutex_init(&server->inner->accept_locker, NULL);
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
    global_async_send(uv_async_server_close, server, NULL, NULL);
}

void uv_async_conn_close(inner_conn_t *conn) {
    if (conn->read_waiting) {
        conn->read_len = UV_ECANCELED;
        if (conn->read_started) {
            uv_read_stop((uv_stream_t *) &conn->handle);
            conn->read_waiting = false;
            conn->read_started = false;
            co_ready(conn->read_co);
        }
    }
    if (!uv_is_closing((uv_handle_t *) &conn->handle)) {
        uv_close((uv_handle_t *) &conn->handle, on_conn_close_handle_cb);
    }
    if (!uv_is_closing((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
        uv_close((uv_handle_t *) &conn->timer, on_conn_close_timer_cb);
    }
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
