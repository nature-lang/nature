#ifndef NATURE_RUNTIME_NUTILS_DNS_H_
#define NATURE_RUNTIME_NUTILS_DNS_H_
#include "runtime/processor.h"
#include "runtime/rtype.h"
#include "runtime/uv_compat.h"

typedef struct {
    n_string_t host;
    n_vec_t ips;
    uv_getaddrinfo_t req;
    coroutine_t *co;
    int32_t status;
} dns_ctx_t;

static inline void on_dns_resolved_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
    dns_ctx_t *ctx = req->data;
    coroutine_t *co = ctx->co;
    DEBUGF("[on_dns_resolved_cb] co: %p, status: %d", co, status);

    if (status < 0) {
        ctx->status = status;
        co_ready(co);
        return;
    }

    n_vec_t *ips = &ctx->ips;
    struct addrinfo *current = res;
    while (current != NULL) {
        if (current->ai_family == AF_INET) {
            char addr[17] = {'\0'};
            struct sockaddr_in *addr_in = (struct sockaddr_in *) current->ai_addr;

            uv_ip4_name(addr_in, addr, 16);

            n_string_t ip = rt_string_new((n_anyptr_t) addr);
            rt_vec_push(ips, string_rtype.hash, &ip);
        } else if (current->ai_family == AF_INET6) {
            char addr6[INET6_ADDRSTRLEN] = {'\0'};

            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *) current->ai_addr;
            uv_ip6_name(addr_in6, addr6, sizeof(addr6));

            n_string_t ip = rt_string_new((n_anyptr_t) addr6);
            rt_vec_push(ips, string_rtype.hash, &ip);
        }
        current = current->ai_next;
    }

    uv_freeaddrinfo(res);
    co_ready(co);
}

void uv_async_getaddrinfo_register(uv_getaddrinfo_t *req, dns_ctx_t *ctx) {
    DEBUGF("[uv_async_getaddrinfo_register] host is %s, co=%p", (char *) rt_string_ref(&ctx->host), req->data);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    int result = uv_getaddrinfo(&global_loop, req, on_dns_resolved_cb, rt_string_ref(&ctx->host), NULL, &hints);
    if (result) {
        DEBUGF("[uv_async_getaddrinfo_register] uv_getaddrinfo failed: %s, co=%p", uv_strerror(result), req->data);
        ctx->status = result;
        co_ready(ctx->co);
        return;
    }
}

n_vec_result_t rt_uv_dns_lookup(n_string_t host) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    DEBUGF("[rt_uv_dns_lookup] start, host is %s, co=%p", (char *) rt_string_ref(&host), co);

    dns_ctx_t *ctx = mallocz(sizeof(dns_ctx_t));
    ctx->host = host;
    ctx->ips = rt_vec_cap(vec_rtype.hash, string_rtype.hash, 0).value;

    ctx->co = co;
    ctx->req.data = ctx;

    global_waiting_send(uv_async_getaddrinfo_register, &ctx->req, ctx, 0);

    n_vec_result_t result = ctx->status ? N_RESULT_ERROR(n_vec_result_t, native_uv_error(ctx->status)) : N_RESULT_OK(n_vec_result_t, ctx->ips);
    free(ctx);
    return result;
}

#endif //NATURE_RUNTIME_NUTILS_DNS_H_
