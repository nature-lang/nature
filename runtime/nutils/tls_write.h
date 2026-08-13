#ifndef NATURE_RUNTIME_NUTILS_TLS_WRITE_H_
#define NATURE_RUNTIME_NUTILS_TLS_WRITE_H_

#include <stddef.h>
#include <stdint.h>

typedef int (*tls_record_write_fn)(void *ctx, const unsigned char *buf, size_t len);

static inline int64_t tls_write_all_records(void *ctx, tls_record_write_fn write_record,
                                            const unsigned char *buf, size_t len) {
    int64_t written = 0;
    while ((size_t) written < len) {
        int result = write_record(ctx, buf + written, len - (size_t) written);
        if (result <= 0) {
            return result;
        }
        written += result;
    }
    return written;
}

#endif // NATURE_RUNTIME_NUTILS_TLS_WRITE_H_
