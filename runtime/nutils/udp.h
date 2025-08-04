#ifndef NATURE_RUNTIME_NUTILS_UDP_H_
#define NATURE_RUNTIME_NUTILS_UDP_H_

#include "runtime/processor.h"
#include <uv.h>

#define UDP_BUFFER_SIZE 65536

typedef struct {
    n_string_t *ip;
    n_int_t port;
} n_udp_addr_t;

typedef struct udp_packet_t {
    char *data; // read data
    int64_t len; // read len
    struct sockaddr_storage addr;
    struct udp_packet_t *next;
} udp_packet_t;

// default cap 65536
typedef struct udp_buf_t {
    char *data;
    int64_t len; // used len
    void *next;
} udp_buf_t;

typedef struct {
    n_udp_addr_t addr;

    uv_udp_t *handle;
    coroutine_t *bind_co;
    void *data;

    udp_packet_t *head;
    udp_packet_t *tail;
    int64_t packet_count;

    char *buf; // 按照 suggested_size 固定大小
    uint64_t suggested_size; // default = 65536

    udp_buf_t *write_buf;
    udp_buf_t *read_buf;

    bool recv_start;
    bool closed; // close flag
} n_udp_socket_t;


typedef struct {
    n_udp_socket_t *socket;
    n_udp_addr_t remote_addr;
} n_udp_conn_t;

static inline void on_udp_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    n_udp_socket_t *s = handle->data;
    buf->base = s->buf;
    buf->len = UDP_BUFFER_SIZE;
}

/**
 * 从 buf pool 获取 nread 数据
 */
static inline char *get_buffer_start(n_udp_socket_t *s, ssize_t nread) {
    assert(s->write_buf); // init 时必定初始化一个 buf
    assert(nread <= UDP_BUFFER_SIZE);

    if (s->write_buf->len + nread > UDP_BUFFER_SIZE) {
        udp_buf_t *new_buf = malloc(sizeof(udp_buf_t));
        new_buf->len = 0;
        new_buf->next = NULL;
        new_buf->data = malloc(UDP_BUFFER_SIZE);

        s->write_buf->next = new_buf;
        s->write_buf = new_buf;
    }

    assert(s->write_buf->len + nread <= UDP_BUFFER_SIZE);
    return s->write_buf->data + s->write_buf->len;
}

static inline void enqueue_packet(n_udp_socket_t *s, udp_packet_t *packet) {
    if (s->tail) {
        s->tail->next = packet;
    } else {
        s->head = packet;
    }

    s->tail = packet;
    s->packet_count += 1;
}

static inline udp_packet_t *dequeue_packet(n_udp_socket_t *s) {
    // 队列为空
    if (s->head == NULL) {
        return NULL;
    }

    udp_packet_t *packet = s->head;
    s->head = s->head->next;

    if (s->head == NULL) {
        s->tail = NULL;
    }

    s->packet_count--;
    packet->next = NULL;
    return packet;
}

static inline void on_udp_read_cb(uv_udp_t *handle, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    if (nread == 0 || addr == NULL) {
        return;
    }

    n_udp_socket_t *s = handle->data;
    udp_packet_t *packet = mallocz(sizeof(udp_packet_t));

    // get from buf
    char *buf_start = get_buffer_start(s, nread);
    memmove(buf_start, buf->base, nread);

    packet->data = buf_start;
    packet->len = nread;
    memmove(&packet->addr, addr, sizeof(struct sockaddr_storage));

    // push to queue 中
    enqueue_packet(s, packet);

    // 判断是否需要唤醒 coroutine
    if (s->bind_co->status == CO_STATUS_WAITING) {
        co_ready(s->bind_co);
    }

    DEBUGF("[on_udp_read_cb] nread=%ld, packet=%p, buf_start=%p", nread, packet, buf_start);
}

static inline void on_udp_send_cb(uv_udp_send_t *req, int status) {
    uv_udp_t *handle = req->handle;
    n_udp_socket_t *s = handle->data;
    if (status) {
        DEBUGF("[on_udp_send_cb] failed: %s", uv_strerror(status));
    }
}

int64_t rt_uv_udp_sendto(n_udp_socket_t *s, n_vec_t *buf, n_udp_addr_t udp_addr) {
    DEBUGF("[rt_uv_udp_sendto] start")
    if (s->closed) {
        rti_throw("socket closed", false);
        return 0;
    }


    coroutine_t *co = coroutine_get();
    s->data = buf;

    if (s->handle->loop != &co->p->uv_loop) {
        rti_throw("processor threads are not safe", false);
        return 0;
    }

    uv_buf_t write_buf = uv_buf_init((void *) buf->data, buf->length);
    uv_udp_send_t *send_req = malloc(sizeof(uv_udp_send_t));

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(udp_addr.ip), (int) udp_addr.port, &addr);

    uv_udp_send(send_req, s->handle, &write_buf, 1, (const struct sockaddr *) &addr, on_udp_send_cb);

    return buf->length;
}

int64_t rt_uv_udp_recvfrom(n_udp_socket_t *s, n_vec_t *buf, n_udp_addr_t *addr) {
    DEBUGF("[rt_uv_udp_recvfrom] start")
    coroutine_t *co = coroutine_get();
    if (s->handle->loop != &co->p->uv_loop) {
        rti_throw("processor threads are not safe", false);
        return 0;
    }

    // read start
    if (!s->recv_start) {
        int result = uv_udp_recv_start(s->handle, on_udp_alloc_buffer_cb, on_udp_read_cb);
        if (result) {
            rti_co_throw(co, tlsprintf("udp recv failed: %s", uv_strerror(result)), false);
            return 0;
        }
        s->recv_start = true;
    }

    while (true) {
        // check have packet
        if (s->packet_count > 0) {
            udp_packet_t *packet = dequeue_packet(s);

            struct sockaddr *caddr = (struct sockaddr *) &packet->addr;
            char ip_buf[INET6_ADDRSTRLEN] = {0};

            // parser addr
            if (caddr->sa_family == AF_INET) {
                struct sockaddr_in *addr_in = (struct sockaddr_in *) caddr;
                uv_ip4_name(addr_in, ip_buf, INET_ADDRSTRLEN);
                addr->port = ntohs(addr_in->sin_port);
                addr->ip = rt_string_new((n_anyptr_t) ip_buf);
            } else if (caddr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *) caddr;
                uv_ip6_name(addr_in6, ip_buf, INET6_ADDRSTRLEN);
                addr->port = ntohs(addr_in6->sin6_port);
                addr->ip = rt_string_new((n_anyptr_t) ip_buf);
            }

            // copy buf
            int copy_len = min(buf->length, packet->len);
            memmove(buf->data, packet->data, copy_len);

            // check need clear read buf
            if (packet->data > (s->read_buf->data + UDP_BUFFER_SIZE)) {
                DEBUGF("[rt_uv_udp_recvfrom] can clear read buf, packet %p, current read buf max %p", packet->data, s->read_buf->data + UDP_BUFFER_SIZE);
                udp_buf_t *temp = s->read_buf;
                s->read_buf = s->read_buf->next;
                free(temp->data);
                free(temp);
            }

            free(packet);

            return copy_len;
        } else {
            co_yield_waiting(co, NULL, NULL);
            DEBUGF("[rt_uv_udp_recvfrom] coroutine %p resume, packet count = %ld", co, s->packet_count);
        }
    }
}

static inline void on_udp_close_cb(uv_handle_t *handle) {
    n_udp_socket_t *socket = handle->data;
    free(handle);
    assert(socket);

    // 清理所有的 malloc
    udp_buf_t *current = socket->read_buf;
    while (current) {
        udp_buf_t *temp = current;
        current = current->next;
        free(temp->data);
        free(temp);
    }

    free(socket->buf);

    while (true) {
        udp_packet_t *packet = dequeue_packet(socket);
        if (packet == NULL) {
            break;
        }

        free(packet);
    }
}

void rt_uv_udp_close(n_udp_socket_t *s) {
    if (s->closed) {
        return;
    }

    uv_udp_recv_stop(s->handle);
    uv_close((uv_handle_t *) s->handle, on_udp_close_cb);

    s->closed = true;
}

void rt_uv_udp_bind(n_udp_socket_t *s) {
    DEBUGF("[rt_uv_udp_bind] start, addr %s, port %ld", (char *) rt_string_ref(s->addr.ip), s->addr.port)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    s->buf = malloc(UDP_BUFFER_SIZE);
    s->suggested_size = UDP_BUFFER_SIZE;

    udp_buf_t *first_buf = malloc(sizeof(udp_buf_t));
    first_buf->len = 0;
    first_buf->next = NULL;
    first_buf->data = malloc(UDP_BUFFER_SIZE);
    s->write_buf = first_buf;
    s->read_buf = first_buf;

    s->handle = malloc(sizeof(uv_tcp_t));
    uv_udp_init_ex(&p->uv_loop, s->handle, AF_UNSPEC | UV_UDP_RECVMMSG);
    s->handle->data = s;
    s->bind_co = co;

    struct sockaddr_in addr;
    uv_ip4_addr(rt_string_ref(s->addr.ip), (int) s->addr.port, &addr);
    uv_udp_bind(s->handle, (const struct sockaddr *) &addr, 0);

    DEBUGF("[rt_uv_udp_bind] bind success, will return")
}

#endif //NATURE_RUNTIME_NUTILS_UDP_H_
