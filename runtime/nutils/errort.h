#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "nutils.h"
#include "runtime/memory.h"
#include "runtime/rtype.h"
#include "string.h"
#include "utils/type.h"
#include "vec.h"

typedef n_string_t (*error_msg_fn)(void *self);

static inline n_string_t errort_msg(n_errort *self) {
    return self->msg;
}

static inline n_string_t rti_error_msg(n_interface_t *error) {
    assert(error->method_count == 1);
    error_msg_fn error_msg = (error_msg_fn) error->methods[0];
    return error_msg(error->value.ptr_value);
}

/**
 * 主要用于 runtime 调用
 * @param raw_msg
 * @param has
 * @return
 */
static inline n_interface_t *n_error_new(n_string_t msg, uint8_t panic) {
    n_errort *errort = rti_gc_malloc(errort_rtype.gc_heap_size, &errort_rtype);
    errort->msg = msg;
    errort->panic = panic;
    DEBUGF("[runtime.n_error_new] errort=%p, msg=%p, panic=%d", errort, msg, panic);

    // interface casting, error to interface union
    int64_t methods[1] = {(int64_t) errort_msg};
    n_interface_t *result = interface_casting(errort_rtype.hash, errort, 1, methods);

    return result;
}

#endif // NATURE_ERRORT_H
