#include <stdlib.h>
#include <stdio.h>
#include "error.h"

void exit_error(int code, char *message) {
  printf("exception, message: %s\n", message);
  exit(code);
}
