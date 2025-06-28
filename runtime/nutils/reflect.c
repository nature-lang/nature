#include "reflect.h"

rtype_t rt_reflect_type(int64_t hash) {
    rtype_t *r = rt_find_rtype(hash);
    DEBUGF("sizeof reflect %ld", sizeof(rtype_t))
    return *r;
}