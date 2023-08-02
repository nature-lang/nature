#ifndef NATURE_CODING_H
#define NATURE_CODING_H

#include "utils/type.h"
#include "utils/autobuf.h"

static uint64_t struct_spread_to_buf(n_struct_t *s, uint64_t struct_rtype_hash, autobuf_t *buf);

static uint64_t struct_decode(uint8_t *cptr, n_struct_t *s, rtype_t *struct_rtype);

/**
 * 目前仅支持 struct 和 list encode
 * @param v
 * @return
 */
void *libc_encode(n_union_t *v);

void libc_decode(void *data, n_union_t *v);

#endif //NATURE_CODING_H
