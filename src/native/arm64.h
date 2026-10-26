#ifndef NAX_SRC_LIR_NATIVE_ARM64_H
#define NAX_SRC_LIR_NATIVE_ARM64_H

#include "src/binary/encoding/arm64/asm.h"
#include "src/lir.h"
#include "src/register/register.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void arm64_native(closure_t *c);

#endif //NAX_ARM64_H
