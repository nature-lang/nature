#include "lower.h"
#include "amd64.h"
#include <assert.h>

void lower(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        return amd64_lower(c);
    }

    assert(false && "not support arch");
}
