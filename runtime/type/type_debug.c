#include "type_debug.h"

#include <stdlib.h>
#include <string.h>

void *gen_hello_world() {
  char *str = "hello world";
  size_t len = strlen(str);
  void *point = malloc(len);
  strcpy(point, str);
  return point;
}

