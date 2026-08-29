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
#include "runtime/nutils/tls_write.h"
#include "runtime/processor.h"
#include "runtime/uv_compat.h"

#define TLS_BUFFER_SIZE 4096

// TLS 连接内部结构
typedef struct {
    coroutine_t *co;
    coroutine_t *write_co;
    int64_t read_len;
    void *data;
    bool timeout; // 是否触发了 timeout
    bool handshake_done;
    bool read_timeout;
    bool read_waiting;
    bool read_started;
    int64_t read_timeout_ms;

    uv_tcp_t handle; // 基础 tcp 链接
    uv_write_t write_req;
    uv_connect_t conn_req;
    uv_timer_t timer;
    int ref_count;

    // mbedTLS 相关
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;

    // handshake 读写缓冲区
    uv_buf_t buf;
    n_vec_t user_buf;
} inner_tls_conn_t;

typedef struct {
    inner_tls_conn_t *conn;
    bool closed;
    int64_t read_timeout_ms;
} n_tls_conn_t;

static void tls_cleanup_ssl_context(inner_tls_conn_t *conn);
static void uv_async_conn_close(inner_tls_conn_t *conn);

static inline void tls_release_conn(inner_tls_conn_t *conn) {
    conn->ref_count -= 1;
    assert(conn->ref_count >= 0);

    if (conn->ref_count == 0) {
        tls_cleanup_ssl_context(conn);
        free(conn);
        DEBUGF("[tls_release_conn] ref count = 0, cleaned and freed")
    } else {
        DEBUGF("[tls_release_conn] ref count = %d, skip", conn->ref_count);
    }
}


// conn->data 由 mbedtls 相关 cb 注册
static inline void tls_alloc_buffer_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    inner_tls_conn_t *conn = CONTAINER_OF(handle, inner_tls_conn_t, handle);
    assert(conn);

    *buf = conn->buf;
}

static inline void on_tls_timer_close_cb(uv_handle_t *handle) {
    inner_tls_conn_t *conn = CONTAINER_OF(handle, inner_tls_conn_t, timer);
    DEBUGF("[on_tls_timer_close_cb] start, ref_count = %d", conn->ref_count);
    tls_release_conn(conn);
}

static inline void on_tls_close_cb(uv_handle_t *handle) {
    inner_tls_conn_t *conn = CONTAINER_OF(handle, inner_tls_conn_t, handle);
    DEBUGF("[on_tls_close_cb] start, ref_count = %d", conn->ref_count);
    tls_release_conn(conn);
}

static inline void on_tls_read_cb(uv_stream_t *client_handle, ssize_t nread, const uv_buf_t *buf) {
    DEBUGF("[on_tls_read_cb] client: %p, nread: %ld", client_handle, nread);

    inner_tls_conn_t *conn = CONTAINER_OF(client_handle, inner_tls_conn_t, handle);
    if (!conn->read_waiting) {
        return;
    }
    conn->read_len = 0;

    if (nread < 0) {
        DEBUGF("[on_tls_read_cb] uv_read failed: %s", uv_strerror(nread));
    }
    conn->read_len = nread;

    if (uv_is_active((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
    }

    // 停止持续的 uv_read_start 等待用户下次调用
    uv_read_stop(client_handle);
    conn->read_waiting = false;
    conn->read_started = false;

    // 唤醒 read co
    DEBUGF("[on_tls_read_cb] will ready co %p", conn->co);
    co_ready(conn->co);
}

static inline void on_tls_read_timeout_cb(uv_timer_t *timer) {
    inner_tls_conn_t *conn = CONTAINER_OF(timer, inner_tls_conn_t, timer);
    if (!conn->read_waiting) {
        return;
    }
    uv_timer_stop(timer);
    uv_read_stop((uv_stream_t *) &conn->handle);
    conn->read_timeout = true;
    conn->read_len = UV_ETIMEDOUT;
    conn->read_waiting = false;
    conn->read_started = false;
    co_ready(conn->co);
}

static inline void on_tls_write_end_cb(uv_write_t *write_req, int status) {
    inner_tls_conn_t *conn = CONTAINER_OF(write_req, inner_tls_conn_t, write_req);
    coroutine_t *write_co = conn->write_co;
    assert(write_co);
    conn->write_co = NULL;

    if (status < 0) {
        char *msg = tlsprintf("tls uv_write failed: %s", uv_strerror(status));
        DEBUGF("[on_tls_write_end_cb] failed: %s, co=%p", msg, write_co);
    }

    co_ready(write_co);
    DEBUGF("[on_tls_write_end_cb] co=%p ready, status=%d", write_co, write_co->status);
}


static void uv_async_tls_write(inner_tls_conn_t *conn, char *buf, size_t len) {
    uv_buf_t write_buf = {
            .base = (char *) buf,
            .len = len,
    };

    conn->write_req.data = conn;
    int result = uv_write(&conn->write_req, (uv_stream_t *) &conn->handle, &write_buf, 1, on_tls_write_end_cb);
    if (result < 0) {
        coroutine_t *write_co = conn->write_co;
        assert(write_co);
        conn->write_co = NULL;
        rti_co_throw(write_co, tlsprintf("TLS write failed: %s", uv_strerror(result)), false);
        co_ready(write_co);
    }
}

static int mbedtls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    inner_tls_conn_t *conn = (inner_tls_conn_t *) ctx;
    assert(conn->write_co == NULL);
    conn->write_co = coroutine_get();

    global_waiting_send(uv_async_tls_write, conn, (void *) buf, (void *) len);

    return len;
}


static void uv_async_tls_read(inner_tls_conn_t *conn) {
    if (!conn->read_waiting) {
        return;
    }
    if (uv_is_closing((uv_handle_t *) &conn->handle)) {
        conn->read_len = UV_ECANCELED;
        conn->read_waiting = false;
        co_ready(conn->co);
        return;
    }
    conn->read_started = true;
    int result = uv_read_start((uv_stream_t *) &conn->handle, tls_alloc_buffer_cb, on_tls_read_cb);
    if (result < 0) {
        conn->read_len = result;
        conn->read_waiting = false;
        conn->read_started = false;
        co_ready(conn->co);
        return;
    }
    if (conn->read_timeout_ms > 0 && !uv_is_closing((uv_handle_t *) &conn->timer)) {
        uv_timer_start(&conn->timer, on_tls_read_timeout_cb, conn->read_timeout_ms, 0);
    }
}
/**
 * buf + len 由 mbedtls init 通过 calloc 进行申请
 */
static int mbedtls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    inner_tls_conn_t *conn = (inner_tls_conn_t *) ctx;
    conn->buf.base = (char *) buf;
    conn->buf.len = len;
    conn->co = coroutine_get();
    conn->read_waiting = true;
    conn->read_started = false;

    global_waiting_send(uv_async_tls_read, conn, 0, 0);
    // may be error
    if (conn->read_timeout) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
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
    inner_tls_conn_t *conn = CONTAINER_OF(conn_req, inner_tls_conn_t, conn_req);

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
        if (!uv_is_closing((uv_handle_t *) &conn->handle)) {
            uv_close((uv_handle_t *) &conn->handle, on_tls_close_cb);
        }
    }

    co_ready(conn->co);
}

static inline void on_tls_timeout_cb(uv_timer_t *handle) {
    DEBUGF("[on_tls_timeout_cb] timeout set")
    inner_tls_conn_t *conn = CONTAINER_OF(handle, inner_tls_conn_t, timer);
    conn->timeout = true;

    uv_timer_stop(handle);
    uv_close((uv_handle_t *) handle, on_tls_timer_close_cb);
    if (!uv_is_closing((uv_handle_t *) &conn->handle)) {
        uv_close((uv_handle_t *) &conn->handle, on_tls_close_cb);
    }

    rti_co_throw(conn->co, "TLS connection timeout", 0);
    co_ready(conn->co);
}

static void uv_async_tls_connect(inner_tls_conn_t *conn, struct sockaddr_in *dest, n_int64_t timeout_ms) {
    uv_tcp_init(&global_loop, &conn->handle);
    uv_timer_init(&global_loop, &conn->timer);

    uv_tcp_connect(&conn->conn_req, &conn->handle, (const struct sockaddr *) dest, on_tls_connect_cb);

    free(dest);
    if (timeout_ms > 0) {
        uv_timer_start(&conn->timer, on_tls_timeout_cb, timeout_ms, 0);
    }
}

void rt_uv_tls_connect(n_tls_conn_t *n_conn, n_string_t addr, n_int64_t port, n_int64_t timeout_ms) {
    DEBUGF("[rt_uv_tls_connect] start, addr %s, port %ld, timeout_ms %ld", (char *) rt_string_ref(&addr), port,
           timeout_ms)

    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    inner_tls_conn_t *conn = mallocz(sizeof(inner_tls_conn_t));
    conn->timeout = false;
    conn->data = NULL;
    conn->handshake_done = false;
    conn->read_len = 0;
    // One reference belongs to the TCP handle, one belongs to the shared
    // timer, and one keeps the connection alive while this coroutine is
    // suspended in connect/handshake. Close callbacks may run before the
    // coroutine is scheduled again.
    conn->ref_count = 3;
    n_conn->conn = conn;
    conn->co = co;
    conn->read_timeout_ms = timeout_ms;

    DEBUGF("[rt_uv_tls_connect] malloc new conn=%p, co=%p, p_index=%d", conn, conn->co, p->index);

    // 初始化 TLS 上下文
    int ret = tls_init_ssl_context(conn, &addr);
    if (ret != 0) {
        char error_buf[256];
        mbedtls_strerror(ret, error_buf, sizeof(error_buf));
        rti_co_throw(co, tlsprintf("TLS init failed: %s", error_buf), false);

        tls_cleanup_ssl_context(conn);
        free(conn);
        n_conn->conn = NULL;
        n_conn->closed = true;
        return;
    }

    struct sockaddr_in *dest = malloc(sizeof(struct sockaddr_in));
    ret = uv_ip4_addr(rt_string_ref(&addr), (int) port, dest);
    if (ret != 0) {
        rti_co_throw(co, tlsprintf("invalid TLS IPv4 address: %s", rt_string_ref(&addr)), false);
        free(dest);
        tls_cleanup_ssl_context(conn);
        free(conn);
        n_conn->conn = NULL;
        n_conn->closed = true;
        return;
    }

    global_waiting_send(uv_async_tls_connect, conn, dest, (void *) timeout_ms);

    if (conn->timeout || uv_is_closing((uv_handle_t *) &conn->handle)) {
        n_conn->closed = true;
        tls_release_conn(conn);
        return;
    }

    DEBUGF("[rt_uv_tls_connect] tcp connect success, will handshake handle");

    // handshake handle
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
            n_conn->closed = true;
            global_async_send(uv_async_conn_close, conn, 0, 0);
            tls_release_conn(conn);
            return;
        }
    }

    DEBUGF("[rt_uv_tls_connect] resume, TLS connect and handshake success, will return conn=%p", conn)
    tls_release_conn(conn);
}

int64_t rt_uv_tls_read(n_tls_conn_t *n_conn, n_vec_t buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "tls conn closed", false);
        return 0;
    }
    inner_tls_conn_t *conn = n_conn->conn;
    conn->co = co;

    if (!conn->handshake_done) {
        rti_co_throw(co, "tls conn handshake failed, cannot read", false);
        return 0;
    }
    conn->handle.data = conn;
    conn->user_buf = buf;
    conn->read_timeout = false;
    conn->read_timeout_ms = n_conn->read_timeout_ms;
    conn->ref_count += 1;

    // 使用 mbedTls 读取数据, buf.data + buf.length 存储解密后的数据
    int ret = mbedtls_ssl_read(&conn->ssl, (unsigned char *) buf.data, buf.length);
    bool read_timeout = conn->read_timeout;
    bool closed = n_conn->closed;
    tls_release_conn(conn);
    if (closed) {
        rti_co_throw(co, "tls conn closed", false);
        return 0;
    }
    if (read_timeout) {
        rti_co_throw(co, "tls read timeout", false);
        return 0;
    }
    if (ret < 0) {
        rti_co_throw(co, tlsprintf("TLS read failed: %s", uv_strerror(ret)), false);
        return 0;
    }

    return ret;
}

static int tls_mbedtls_write_record(void *ctx, const unsigned char *buf, size_t len) {
    return mbedtls_ssl_write((mbedtls_ssl_context *) ctx, buf, len);
}

int64_t rt_uv_tls_write(n_tls_conn_t *n_conn, n_vec_t buf) {
    coroutine_t *co = coroutine_get();
    if (n_conn->closed) {
        rti_co_throw(co, "tls conn closed", false);
        return 0;
    }

    inner_tls_conn_t *conn = n_conn->conn;
    conn->handle.data = conn;
    conn->user_buf = buf;

    if (!conn->handshake_done) {
        rti_co_throw(co, "tls handshake not completed, cannot write", false);
        return 0;
    }

    // mbedtls_ssl_write() writes at most one TLS record and may therefore
    // return a positive short count for buffers larger than the negotiated
    // record payload (normally about 16 KiB). Keep the connable write
    // contract complete instead of silently truncating callers.
    int64_t written = tls_write_all_records(&conn->ssl, tls_mbedtls_write_record,
                                            (const unsigned char *) buf.data, (size_t) buf.length);
    if (written < 0) {
        char error_buf[256];
        mbedtls_strerror((int) written, error_buf, sizeof(error_buf));
        rti_co_throw(co, tlsprintf("TLS write failed: %s", error_buf), false);
        return 0;
    }
    if (written == 0 && buf.length > 0) {
        rti_co_throw(co, "TLS write made no progress", false);
        return 0;
    }

    return written;
}

static void uv_async_conn_close(inner_tls_conn_t *conn) {
    if (conn->read_waiting) {
        conn->read_len = UV_ECANCELED;
        if (conn->read_started) {
            uv_read_stop((uv_stream_t *) &conn->handle);
            conn->read_waiting = false;
            conn->read_started = false;
            co_ready(conn->co);
        }
    }
    if (!uv_is_closing((uv_handle_t *) &conn->handle)) {
        uv_close((uv_handle_t *) &conn->handle, on_tls_close_cb);
    }
    if (!uv_is_closing((uv_handle_t *) &conn->timer)) {
        uv_timer_stop(&conn->timer);
        uv_close((uv_handle_t *) &conn->timer, on_tls_timer_close_cb);
    }
}

void rt_uv_tls_conn_close(n_tls_conn_t *n_conn) {
    if (n_conn->closed) {
        return;
    }

    inner_tls_conn_t *conn = n_conn->conn;
    n_conn->closed = true;

    // Send TLS close notify before closing the transport. mbedtls_send_cb()
    // records the coroutine performing this write separately from conn->co,
    // which may still be a different coroutine blocked in TLS read.
    if (conn->handshake_done) {
        mbedtls_ssl_close_notify(&conn->ssl);
    }

    global_async_send(uv_async_conn_close, conn, 0, 0);
}

#endif //NATURE_RUNTIME_NUTILS_TLS_H_
