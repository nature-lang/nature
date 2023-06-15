#ifndef NATURE_ERRORT_H
#define NATURE_ERRORT_H

#include "utils/type.h"
#include "type/string.h"
#include "memory.h"

/**
 * 主要用于 runtime 调用
 * @param raw_msg
 * @param has
 * @return
 */
static inline memory_errort *memory_errort_new(char *raw_msg, uint8_t has) {
    memory_string_t *msg = string_new(raw_msg, strlen(raw_msg));
    rtype_t errort_rtype = gc_rtype(TYPE_STRUCT, 2, TYPE_GC_SCAN, TYPE_GC_NOSCAN);
    memory_errort *errort = runtime_malloc(errort_rtype.size, &errort_rtype);
    errort->has = has;
    errort->msg = msg;
    DEBUGF("[runtime.memory_errort_new] errort=%p, msg=%p, has=%d", errort, msg, has)
    return errort;
}


#endif //NATURE_ERRORT_H
