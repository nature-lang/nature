#ifndef NATURE_AMD64_ABI_H
#define NATURE_AMD64_ABI_H

#include "src/lir.h"

typedef enum {
    AMD64_CLASS_NO = 0,
    AMD64_CLASS_MEMORY,
    AMD64_CLASS_INTEGER,
    AMD64_CLASS_SSE,
    //    AMD64_MODE_X87 // 不认识，暂时忽略
} amd64_class_t;

int64_t amd64_type_classify(type_t t, amd64_class_t *lo, amd64_class_t *hi, uint64_t offset);

linked_t *amd64_lower_call(closure_t *c, lir_op_t *op);

linked_t *amd64_lower_fn_begin(closure_t *c, lir_op_t *op);

linked_t *amd64_lower_return(closure_t *c, lir_op_t *op);

#endif//NATURE_AMD64_ABI_H
