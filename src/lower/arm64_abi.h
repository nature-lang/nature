#ifndef NATURE_ARM64_ABI_H
#define NATURE_ARM64_ABI_H

#include "src/lir.h"

linked_t *arm64_lower_call(closure_t *c, lir_op_t *op);

linked_t *arm64_lower_fn_begin(closure_t *c, lir_op_t *op);

linked_t *arm64_lower_return(closure_t *c, lir_op_t *op);

#endif//NATURE_ARM64_ABI_H