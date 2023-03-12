#ifndef NATURE_SRC_RUNTIME_STRING_H_
#define NATURE_SRC_RUNTIME_STRING_H_

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "utils/type.h"

// 可能是数据段，也嫩是是其他地方
void *string_new(uint8_t *raw, int count);

void *string_addr(void *point);

int string_length(void *point);

#endif //NATURE_SRC_RUNTIME_STRING_H_
