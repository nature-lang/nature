#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "helper.h"

char *itoa(int n) {
  // 计算长度
  int length = snprintf(NULL, 0, "%d", n);

  // 初始化 buf
  char *str = malloc(length + 1);

  // 转换
  snprintf(str, length + 1, "%d", n);

  return str;
}

bool strequal(char *a, char *b) {
  return strcmp(a, b) == 0;
}

uint8_t int8_complement(int8_t i) {

}

uint32_t int32_complement(int32_t i) {

}

