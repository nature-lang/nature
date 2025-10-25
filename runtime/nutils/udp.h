#ifndef NATURE_RUNTIME_NUTILS_UDP_H_
#define NATURE_RUNTIME_NUTILS_UDP_H_

#include "runtime/processor.h"
#include <uv.h>

#define UDP_BUFFER_SIZE 65536 * 2

typedef struct {
    char data[16];
    n_int_t port; // is raw port?
    bool v4;
} n_udp_addr_t;

typedef struct {
    n_udp_addr_t addr;

    uv_udp_t *handle;
    coroutine_t *co;

    bool closed; // close flag
    int fd;
} n_udp_socket_t;


typedef struct {
    n_udp_socket_t *socket;
    n_udp_addr_t remote_addr;
} n_udp_conn_t;

int64_t rt_uv_udp_recvfrom(n_udp_socket_t *s, n_vec_t *buf, n_udp_addr_t *addr) {
    DEBUGF("[rt_uv_udp_recvfrom] start")
    coroutine_t *co = coroutine_get();

    struct sockaddr_storage src_addr;
    socklen_t addr_len = sizeof(src_addr);

    while (true) {
        // 直接调用 recvfrom（非阻塞）
        ssize_t nread = recvfrom(s->fd, buf->data, buf->length, 0,
                                 (struct sockaddr *) &src_addr, &addr_len);

        if (nread >= 0) {
            struct sockaddr *caddr = (struct sockaddr *) &src_addr;
            //            addr->family = caddr->sa_family;
            if (caddr->sa_family == AF_INET) {
                addr->v4 = true;

                struct sockaddr_in *addr_in = (struct sockaddr_in *) caddr;
                memmove(addr->data, &addr_in->sin_addr, INET_ADDRSTRLEN);

                //                char ip_buf[INET6_ADDRSTRLEN] = {0};
                //
                //                inet_ntop(AF_INET, &addr_in->sin_addr, ip_buf, INET_ADDRSTRLEN);
                addr->port = ntohs(addr_in->sin_port);
            } else if (caddr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *) caddr;

                memmove(addr->data, &addr_in6->sin6_addr, INET6_ADDRSTRLEN);
                //                inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_buf, INET6_ADDRSTRLEN);
                addr->port = ntohs(addr_in6->sin6_port);
                //                addr->ip = rt_string_new((n_anyptr_t) ip_buf);
            }

            DEBUGF("[rt_uv_udp_recvfrom] received %ld bytes from %s:%ld",
                   nread, ip_buf, addr->port);
            return nread;
        }

        // 处理错误
        if (errno == EINTR) {
            continue; // 被信号中断，重试
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 没有数据，让出 CPU 等待（类似 Go 的 waitRead）
            DEBUGF("[rt_uv_udp_recvfrom] EAGAIN, coroutine yield");

            // 注册到 libuv poll 等待可读
            // 或者简单地 yield 一段时间
            // rt_coroutine_sleep(1); // 可以优化为 poll 等待
            continue;
        }

        // 其他错误
        rti_co_throw(co, tlsprintf("udp recv failed: %s", strerror(errno)), false);
        return 0;
    }
}


int64_t rt_uv_udp_sendto(n_udp_socket_t *s, n_vec_t *buf, n_udp_addr_t udp_addr) {
    coroutine_t *co = coroutine_get();
    DEBUGF("[rt_uv_udp_sendto] start")
    if (s->closed) {
        rti_co_throw(co, "socket closed", false);
        return 0;
    }

    struct sockaddr_in addr = {0};
    if (s->addr.v4) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(udp_addr.port);
        memmove(&addr.sin_addr, &udp_addr.data, 4);
    } else {
        // TODO
        assert(false);
    };

    while (true) {
        uv_buf_t write_buf = uv_buf_init((void *) buf->data, buf->length);
        int length = uv_udp_try_send(s->handle, &write_buf, 1, (const struct sockaddr *) &addr);
        if (length < 0) {
            DEBUGF("uv udp try send failed: %s", uv_strerror(length));
            if (length == UV_EAGAIN) {
                rt_coroutine_sleep(1);
                continue;
            }

            rti_throw(tlsprintf("udp send failed: %s", uv_strerror(length)), false);
            return 0;
        }

        return length;
    }
}

static inline void on_udp_close_cb(uv_handle_t *handle) {
    DEBUGF("[on_udp_close_cb] udp close cb, will free handle")
    n_udp_socket_t *s = handle->data;
    free(handle);

    co_ready(s->co);
}

static void uv_async_udp_close(n_udp_socket_t *s) {
    uv_close((uv_handle_t *) s->handle, on_udp_close_cb);
}

void rt_uv_udp_close(n_udp_socket_t *s) {
    if (s->closed) {
        return;
    }

    global_waiting_send(uv_async_udp_close, s, 0, 0);

    s->closed = true;
    DEBUGF("[rt_uv_udp_close] close success, handle_count=%ld", global.async_handle_count)
}

static void uv_async_udp_bind(n_udp_socket_t *s) {
    DEBUGF("[uv_async_udp_bind] start, co %p", s->co)
    uv_udp_init_ex(&global_loop, s->handle, AF_UNSPEC | UV_UDP_RECVMMSG);
    s->handle->data = s;

    struct sockaddr_in addr = {0};
    if (s->addr.v4) {
        addr.sin_family = AF_INET;
        addr.sin_port = htons(s->addr.port);
        memmove(&addr.sin_addr, &s->addr.data, 4);
    } else {
        // TODO
        assert(false);
    };

    int result = uv_udp_bind(s->handle, (const struct sockaddr *) &addr, 0);
    if (result) {
        DEBUGF("[uv_async_udp_bind] bind failed: %s", uv_strerror(result));
        rti_co_throw(s->co, tlsprintf("udp bind failed: %s", uv_strerror(result)), false);
    }

    uv_fileno((uv_handle_t *) s->handle, &s->fd);
    int flags = fcntl(s->fd, F_GETFL, 0);
    fcntl(s->fd, F_SETFL, flags | O_NONBLOCK);

    co_ready(s->co);
}

void rt_uv_udp_bind(n_udp_socket_t *s) {
    DEBUGF("[rt_uv_udp_bind] start, port %ld", s->addr.port)

    coroutine_t *co = coroutine_get();

    s->co = co;
    s->handle = mallocz(sizeof(uv_udp_t));

    global_waiting_send(uv_async_udp_bind, s, 0, 0);

    DEBUGF("[rt_uv_udp_bind] bind success, will return")
}

#endif //NATURE_RUNTIME_NUTILS_UDP_H_
