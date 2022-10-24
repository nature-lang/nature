#ifndef NATURE_OPCODE_H
#define NATURE_OPCODE_H

#include "src/build/config.h"
#include "amd64/opcode.h"

#include <assert.h>

static inline void opcode_init() {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_opcode_init();
        return;
    }

    assert(false && "not support this arch");
}

#endif //NATURE_OPCODE_H
