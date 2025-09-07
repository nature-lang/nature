#ifndef NATURE_RUNTIME_NUTILS_TLS_H_
#define NATURE_RUNTIME_NUTILS_TLS_H_

#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_cache.h"
#include "mbedtls/x509_crt.h"
#include "runtime/processor.h"
#include <uv.h>

#define TLS_BUFFER_SIZE 4096

// TLS 连接内部结构
typedef struct {
    coroutine_t *co;
    int64_t read_len;

    void *data;

    bool timeout; // 是否触发了 timeout
    bool handshake_done;

    // UV 相关
    uv_tcp_t handle; // 基础 tcp 链接
    uv_write_t write_req;
    uv_timer_t timer;

    // mbedTLS 相关
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;

    // handshake 读写缓冲区
    uv_buf_t buf;
} inner_tls_conn_t;

typedef struct {
    inner_tls_conn_t *conn;
    bool closed;
} n_tls_conn_t;


// conn->data 由 mbedtls 相关 cb 注册
static inline void tls_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    inner_tls_conn_t *conn = handle->data;
    assert(conn);

    *buf = conn->buf;
}


static inline void on_tls_close_handle_cb(uv_handle_t *handle) {
    inner_tls_conn_t *conn = CONTAINER_OF(handle, inner_tls_conn_t, handle);
    free(conn);
}

static inline void on_tls_read_cb(uv_stream_t *client_handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_tls_read_cb] client: %p, nread: %ld", client_handle, nread);

    inner_tls_conn_t *conn = client_handle->data;
    conn->read_len = 0;

    if (nread < 0) {
        DEBUGF("[on_tls_read_cb] uv_read failed: %s", uv_strerror(nread));
    }

    conn->read_len = nread;

    // 停止持续的 uv_read_start 等待用户下次调用
    uv_read_stop(client_handle);

    // 唤醒 connect co
    DEBUGF("[on_tls_read_cb] will ready co %p", conn->co);
    co_ready(conn->co);
}

static inline void on_tls_write_end_cb(uv_write_t *write_req, int status) {
    inner_tls_conn_t *conn = write_req->data;
    if (status < 0) {
        char *msg = tlsprintf("tls uv_write failed: %s", uv_strerror(status));
        DEBUGF("[on_tls_write_end_cb] failed: %s, co=%p", msg, conn->co);
    }

    co_ready(conn->co);
    DEBUGF("[on_tls_write_end_cb] co=%p ready, status=%d", conn->co, conn->co->status);
}

static int mbedtls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    inner_tls_conn_t *conn = (inner_tls_conn_t *) ctx;

    uv_buf_t write_buf = {
            .base = (char *) buf,
            .len = len,
    };
    conn->write_req.data = conn;
    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tls_write_end_cb);

    if (result < 0) {
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }

    // 等待写入完成
    co_yield_waiting(conn->co, NULL, NULL);

    return len;
}

/**
 * buf + len 由 mbedtls init 通过 calloc 进行申请
 */
static int mbedtls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    inner_tls_conn_t *conn = (inner_tls_conn_t *) ctx;
    conn->buf.base = (char *) buf;
    conn->buf.len = len;

    int result = uv_read_start((uv_stream_t *) &conn->handle, tls_alloc_buffer_cb, on_tls_read_cb);
    if (result < 0) {
        DEBUGF("[mbedtls_recv_cb] uv_read_start failed: %s", uv_strerror(result));
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    // wait read
    co_yield_waiting(conn->co, NULL, NULL);
    if (conn->read_len < 0) {
        DEBUGF("[mbedtls_recv_cb] uv_read_start read failed: %s", uv_strerror(conn->read_len));
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    // may be error
    return (int) conn->read_len;
}

static inline int tls_init_ssl_context(inner_tls_conn_t *conn, n_string_t *addr) {
    int ret;

    // 初始化 mbedTLS 上下文
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->conf);
    mbedtls_entropy_init(&conn->entropy);
    mbedtls_ctr_drbg_init(&conn->ctr_drbg);
    mbedtls_x509_crt_init(&conn->cacert);

    // 初始化随机数生成器
    const char *pers = "nature_tls_client";
    ret = mbedtls_ctr_drbg_seed(&conn->ctr_drbg, mbedtls_entropy_func, &conn->entropy,
                                (const unsigned char *) pers, strlen(pers));
    if (ret != 0) {
        DEBUGF("[tls_init_ssl_context] mbedtls_ctr_drbg_seed failed: -0x%x", -ret);
        return ret;
    }

    // 设置 SSL 配置
    ret = mbedtls_ssl_config_defaults(&conn->conf, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        DEBUGF("[tls_init_ssl_context] mbedtls_ssl_config_defaults failed: -0x%x", -ret);
        return ret;
    }

    // 设置证书验证模式（可选）
    mbedtls_ssl_conf_authmode(&conn->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    // 设置随机数生成器
    mbedtls_ssl_conf_rng(&conn->conf, mbedtls_ctr_drbg_random, &conn->ctr_drbg);


    // 设置 SSL 上下文
    ret = mbedtls_ssl_setup(&conn->ssl, &conn->conf);
    if (ret != 0) {
        DEBUGF("[tls_init_ssl_context] mbedtls_ssl_setup failed: -0x%x", -ret);
        return ret;
    }

    // 设置服务器名称（SNI）
    ret = mbedtls_ssl_set_hostname(&conn->ssl, rt_string_ref(addr));
    if (ret != 0) {
        DEBUGF("[rt_uv_tls_connect] mbedtls_ssl_set_hostname failed: -0x%x", -ret);
    }

    // 设置 BIO 回调函数, 用于数据加密与解密
    mbedtls_ssl_set_bio(&conn->ssl, conn, mbedtls_send_cb, mbedtls_recv_cb, NULL);

    return 0;
}

static void tls_cleanup_ssl_context(inner_tls_conn_t *conn) {
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_entropy_free(&conn->entropy);
    mbedtls_ctr_drbg_free(&conn->ctr_drbg);
    mbedtls_x509_crt_free(&conn->cacert);
}

static inline void on_tls_connect_cb(uv_connect_t *conn_req, int status) {
    DEBUGF("[on_tls_connect_cb] start")
    inner_tls_conn_t *conn = conn_req->data;
    if (conn->timeout) {
        DEBUGF("[on_tls_connect_cb] connection timeout, not need handle anything")
        return;
    }

    if (uv_is_active((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
    }

    if (status < 0) {
        DEBUGF("[on_tls_connect_cb] connection failed: %s", uv_strerror(status));
        rti_co_throw(conn->co, tlsprintf("TLS connection failed: %s", uv_strerror(status)), false);
    }

    co_ready(conn->co);
}

static inline void on_tls_timeout_cb(uv_timer_t *handle) {
    DEBUGF("[on_tls_timeout_cb] timeout set")

    inner_tls_conn_t *conn = handle->data;
    conn->timeout = true;

    uv_timer_stop(handle);
    rti_co_throw(conn->co, "TLS connection timeout", 0);
    co_ready(conn->co);
}

void rt_uv_tls_connect(n_tls_conn_t *n_conn, n_string_t *addr, n_int64_t port, n_int64_t timeout_ms) {
    DEBUGF("[rt_uv_tls_connect] start, addr %s, port %ld, timeout_ms %ld", (char *) rt_string_ref(addr), port, timeout_ms)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    inner_tls_conn_t *conn = mallocz(sizeof(inner_tls_conn_t));
    conn->timeout = false;
    conn->data = NULL;
    conn->handshake_done = false;
    conn->read_len = 0;

    n_conn->conn = conn;
    conn->co = co;

    DEBUGF("[rt_uv_tls_connect] malloc new conn=%p, co=%p, p_index=%d", conn, conn->co, p->index);

    // 初始化 TLS 上下文
    int ret = tls_init_ssl_context(conn, addr);
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        rti_co_throw(co, tlsprintf("TLS init failed: %s", error_buf), false);

        free(conn);
        return;
    }

    uv_tcp_init(&p->uv_loop, &conn->handle);
    conn->handle.data = conn;

    struct sockaddr_in dest;
    uv_ip4_addr(rt_string_ref(addr), port, &dest);

    uv_connect_t *connect_req = malloc(sizeof(uv_connect_t));
    connect_req->data = conn;
    uv_tcp_connect(connect_req, &conn->handle, (const struct sockaddr *) &dest, on_tls_connect_cb);

    if (timeout_ms > 0) {
        conn->timer.data = conn;
        uv_timer_init(&p->uv_loop, &conn->timer);
        uv_timer_start(&conn->timer, on_tls_timeout_cb, timeout_ms, 0);
    }

    // yield wait conn and handshake
    co_yield_waiting(co, NULL, NULL);
    free(connect_req);

    if (co->has_error) {
        if (uv_is_active((uv_handle_t *) &conn->handle)) {
            uv_close((uv_handle_t *) &conn->handle, NULL);
        }
        DEBUGF("[rt_uv_tls_connect] have error");
        return;
    }

    // handshake
    while (true) {
        ret = mbedtls_ssl_handshake(&conn->ssl);
        if (ret == 0) {
            conn->handshake_done = true;
            DEBUGF("[on_tls_async_cb] TLS handshake completed successfully");
            break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            // handshake more data
            continue;
        } else {
            char error_buf[256];
            mbedtls_strerror(ret, error_buf, sizeof(error_buf));
            rti_co_throw(conn->co, tlsprintf("tls handshake failed: %s", error_buf), false);
        }
    }

    DEBUGF("[rt_uv_tls_connect] resume, TLS connect and handshake success, will return conn=%p", conn)
}

int64_t rt_uv_tls_read(n_tls_conn_t *n_conn, n_vec_t *buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "tls conn closed", false);
    }
    inner_tls_conn_t *conn = n_conn->conn;
    conn->co = co;

    if (!conn->handshake_done) {
        rti_co_throw(co, "tls conn handshake failed, cannot read", false);
        return 0;
    }

    conn->handle.data = conn;
    conn->data = buf;

    // 使用 mbedTls 读取数据, buf->data + buf->length 存储解密后的数据
    int ret = mbedtls_ssl_read(&conn->ssl, (unsigned char *) buf->data, buf->length);
    if (ret < 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        rti_co_throw(co, tlsprintf("tls read failed: %s", error_buf), false);
        return 0;
    }

    return ret;
}

int64_t rt_uv_tls_write(n_tls_conn_t *n_conn, n_vec_t *buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "tls conn closed", false);
    }

    inner_tls_conn_t *conn = n_conn->conn;
    conn->co = co;

    if (!conn->handshake_done) {
        rti_co_throw(co, "tls handshake not completed, cannot write", false);
        return 0;
    }

    if (conn->handle.loop != &co->p->uv_loop) {
        rti_throw("processor threads are not safe", false);
        return 0;
    }

    conn->handle.data = conn;

    // 使用 mbedTLS 写入数据
    int ret = mbedtls_ssl_write(&conn->ssl, (const unsigned char *) buf->data, buf->length);
    if (ret < 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        rti_co_throw(co, tlsprintf("TLS write failed: %s", error_buf), false);
        return 0;
    }

    return ret;
}

void rt_uv_tls_conn_close(n_tls_conn_t *n_conn) {
    if (n_conn->closed) {
        return;
    }

    inner_tls_conn_t *conn = n_conn->conn;
    n_conn->closed = true;

    // 发送 TLS close notify
    if (conn->handshake_done) {
        mbedtls_ssl_close_notify(&conn->ssl);
    }

    uv_close((uv_handle_t *) &conn->handle, on_tls_close_handle_cb);
}

#endif //NATURE_RUNTIME_NUTILS_TLS_H_
