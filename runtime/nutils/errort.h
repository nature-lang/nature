#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "nutils.h"
#include "runtime/memory.h"
#include "string.h"
#include "utils/type.h"
#include "vec.h"

/**
 * 主要用于 runtime 调用
 * @param raw_msg
 * @param has
 * @return
 */
static inline n_error_t *n_error_new(n_string_t *msg, uint8_t panic) {
    // 构造一个 trace_t
    rtype_t *element_rtype = gc_rtype(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    n_vec_t *traces = rti_vec_new(element_rtype, 0, 0);

    rtype_t *errort_rtype = gc_rtype(TYPE_STRUCT, 3, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_error_t *errort = rti_gc_malloc(errort_rtype->size, errort_rtype);
    errort->msg = msg;
    errort->traces = traces;
    errort->has = true;
    errort->panic = panic;
    DEBUGF("[runtime.n_error_new] errort=%p, msg=%p, panic=%d", errort, msg, panic);
    return errort;
}

#endif // NATURE_ERRORT_H
