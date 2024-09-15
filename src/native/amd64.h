#ifndef NATURE_SRC_LIR_NATIVE_AMD64_H_
#define NATURE_SRC_LIR_NATIVE_AMD64_H_

#include "src/binary/encoding/amd64/asm.h"
#include "src/lir.h"
#include "src/register/register.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef slice_t *(*amd64_native_fn)(closure_t *c, lir_op_t *op);

/**
 * 分发入口, 基于 op->code 做选择(包含 label code)
 * @param c
 * @param op
 * @return
 */
slice_t *amd64_native_op(closure_t *c, lir_op_t *op);

slice_t *amd64_native_block(closure_t *c, basic_block_t *block);

void amd64_native(closure_t *c);

#endif //NATURE_SRC_LIR_NATIVE_AMD64_H_
