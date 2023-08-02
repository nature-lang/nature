#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "utils/type.h"
#include "string.h"
#include "runtime/memory.h"

/**
 * 主要用于 runtime 调用
 * @param raw_msg
 * @param has
 * @return
 */
static inline n_errort *n_errort_new(char *raw_msg, uint8_t has) {
    n_string_t *msg = string_new(raw_msg, strlen(raw_msg));
    rtype_t *errort_rtype = gc_rtype(TYPE_STRUCT, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    n_errort *errort = runtime_malloc(errort_rtype->size, errort_rtype);
    errort->has = has;
    errort->msg = msg;
    DEBUGF("[runtime.n_errort_new] errort=%p, msg=%p, has=%d", errort, msg, has)
    return errort;
}


#endif //NATURE_ERRORT_H
