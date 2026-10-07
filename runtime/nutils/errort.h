#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "nutils.h"
#include "runtime/memory.h"
#include "runtime/rtype.h"
#include "utils/type.h"

static inline n_error_t native_error(int32_t code) {
    n_error_t error = {.rtype_hash = RUNTIME_ERROR_RTYPE_HASH};
    memcpy(error.payload, &code, sizeof(code));
    return error;
}

static inline n_error_t native_system_error(int32_t code) {
    n_error_t error = {.rtype_hash = SYSTEM_ERROR_RTYPE_HASH};
    memcpy(error.payload, &code, sizeof(code));
    return error;
}

static inline n_error_t native_uv_error(int32_t status) {
    switch (status) {
        case UV_EOF:
            return native_error(N_ERROR_EOF);
        case UV_ETIMEDOUT:
            return native_error(N_ERROR_TIMEOUT);
        case UV_ECANCELED:
            return native_error(N_ERROR_CANCELLED);
        case UV_EBADF:
            return native_error(N_ERROR_CLOSED);
        default:
            return native_system_error(status);
    }
}

static inline n_error_t native_fs_error(int32_t status) {
    return status > 0 ? native_system_error(status) : native_uv_error(status);
}

#endif
