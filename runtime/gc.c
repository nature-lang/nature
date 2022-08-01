#include "gc.h"
#include <stdlib.h>

void *gc_new(int size) {
    void *addr = malloc(size);
    // TODO gc 标记等
    return addr;
}
