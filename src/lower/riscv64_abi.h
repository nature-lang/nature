#ifndef NATURE_RISCV64_ABI_H
#define NATURE_RISCV64_ABI_H

#include "src/lir.h"

linked_t *riscv64_lower_call(closure_t *c, lir_op_t *op);

linked_t *riscv64_lower_fn_begin(closure_t *c, lir_op_t *op);

linked_t *riscv64_lower_fn_end(closure_t *c, lir_op_t *op);

#endif//NATURE_RISCV64_ABI_H