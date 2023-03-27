#ifndef NATURE_RUNTIME_BUILTIN_H_
#define NATURE_RUNTIME_BUILTIN_H_

#include "utils/type.h"

void print(memory_list_t *args);

void println(memory_list_t *args);

void memory_move(void *dst, void *src, uint64_t size);

#endif //NATURE_SRC_LIR_NATIVE_BUILTIN_H_
