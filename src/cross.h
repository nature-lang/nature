#ifndef NATURE_CROSS_H
#define NATURE_CROSS_H

#include "build/config.h"

void amd64_reg_init();

void riscv64_reg_init();

typedef void (*fn_reg_init)();

#define CROSS_REG_INIT cross_reg_init()

static fn_reg_init cross_reg_init() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_reg_init;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
    return NULL;
}


#define AMD64_ALLOC_REG_COUNT 14+16;

static uint8_t cross_alloc_reg_count() {
    if (BUILD_ARCH == ARCH_AMD64) {
        return AMD64_ALLOC_REG_COUNT;
    }

    assertf(false, "not support arch %d", BUILD_ARCH);
    exit(1);
}

#endif //NATURE_CROSS_H
