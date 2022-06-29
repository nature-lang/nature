#ifndef NATURE_SRC_RUNTIME_STRING_H_
#define NATURE_SRC_RUNTIME_STRING_H_

#include <stdlib.h>
#include <stdint.h>

typedef struct {
  int length; // 实际长度
//  int capacity; // 可存储长度
  uint8_t *content; // 字符串数据
} string_t;

// 可能是数据段，也嫩是是其他地方
void *string_new(uint8_t *raw, int count);

void *string_addr(void *point);

int string_length(void *point);

#endif //NATURE_SRC_RUNTIME_STRING_H_
