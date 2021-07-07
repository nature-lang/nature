#include <stdlib.h>
#include <stdio.h>
#include "error.h"

void error_exit(int code, char *message) {
  printf("exception, message: %s\n", message);
  exit(code);
}
