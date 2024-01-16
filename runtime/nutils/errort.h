#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "utils/type.h"
#include "vec.h"
#include "string.h"
#include "runtime/memory.h"
#include "nutils.h"

/**
 * 主要用于 runtime 调用
 * @param raw_msg
 * @param has
 * @return
 */
static inline n_errort *n_errort_new(n_string_t *msg, uint8_t has) {
    // 构造一个 trace_t
    rtype_t *list_rtype = gc_rtype(TYPE_VEC, 4, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    rtype_t *element_rtype = gc_rtype(TYPE_STRUCT, 4, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN, TYPE_GC_NOSCAN);
    n_vec_t *traces = vec_new(list_rtype->hash, element_rtype->hash, 0, 0);

    rtype_t *errort_rtype = gc_rtype(TYPE_STRUCT, 3, TYPE_GC_SCAN, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_errort *errort = runtime_zero_malloc(errort_rtype->size, errort_rtype);
    errort->msg = msg;
    errort->traces = traces;
    errort->has = has;
    DEBUGF("[runtime.n_errort_new] errort=%p, msg=%p, has=%d", errort, msg, has);
    return errort;
}


#endif //NATURE_ERRORT_H
