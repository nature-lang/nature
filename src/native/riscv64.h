#ifndef NATURE_RISCV64_H
#define NATURE_RISCV64_H

#include "src/binary/encoding/riscv64/asm.h"
#include "src/lir.h"
#include "src/register/register.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/**
 * 生成RISC-V 64位汇编代码
 * @param c
 */
void riscv64_native(closure_t *c);

#endif //NATURE_RISCV64_H
