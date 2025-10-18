#ifndef NATURE_RUNTIME_NUTILS_DNS_H_
#define NATURE_RUNTIME_NUTILS_DNS_H_
#include "runtime/processor.h"
#include <uv.h>

static inline void on_dns_resolved_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
    coroutine_t *co = req->data;

    if (status < 0) {
        rti_co_throw(co, (char *) uv_strerror(status), false);
        co_ready(co);
        return;
    }

    // co->data = res; // addr info need use uv_freeaddrinfo free
    n_vec_t *ips = co->data;
    struct addrinfo *current = res;
    while (current != NULL) {
        if (current->ai_family == AF_INET) {
            char addr[17] = {'\0'};
            struct sockaddr_in *addr_in = (struct sockaddr_in *) current->ai_addr;

            uv_ip4_name(addr_in, addr, 16);

            n_string_t *ip = rt_string_new((n_anyptr_t) addr);
            rt_vec_push(ips, string_rtype.hash, &ip);
        } else if (current->ai_family == AF_INET6) {
            char addr6[INET6_ADDRSTRLEN] = {'\0'};

            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *) current->ai_addr;
            uv_ip6_name(addr_in6, addr6, sizeof(addr6));

            n_string_t *ip = rt_string_new((n_anyptr_t) addr6);
            rt_vec_push(ips, string_rtype.hash, &ip);
        }
        current = current->ai_next;
    }

    uv_freeaddrinfo(res);
    co_ready(co);
}

void uv_async_getaddrinfo_register(uv_getaddrinfo_t *req, n_string_t *host) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    int result = uv_getaddrinfo(&global_loop, req, on_dns_resolved_cb, rt_string_ref(host), NULL, &hints);
    if (result) {
        rti_co_throw(req->data, tlsprintf("resolve %s failed: %s", rt_string_ref(host), uv_strerror(result)), false);
        co_ready(req->data);
        return;
    }
}

void rt_uv_dns_lookup(n_string_t *host, n_vec_t *ips) {
    n_processor_t *p = processor_get();
    coroutine_t *co = coroutine_get();

    uv_getaddrinfo_t *req = malloc(sizeof(uv_getaddrinfo_t));
    req->data = co;
    co->data = ips;

    global_async_send(uv_async_getaddrinfo_register, req, host, 0);

    co_yield_waiting(co, NULL, NULL);

    free(req);
}

#endif //NATURE_RUNTIME_NUTILS_DNS_H_
