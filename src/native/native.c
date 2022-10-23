#include "native.h"
#include "amd64.h"

void native(closure_t *c) {
    if (BUILD_ARCH == ARCH_AMD64) {
        amd64_native(c);
    }
}

void native_init() {
    native_var_decls = slice_new();
    native_decl_unique_count = 0;
}
