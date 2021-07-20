#include <stdlib.h>
#include <stdio.h>
#include "error.h"

void error_exit(int code, char *message) {
  printf("exception, message: %s\n", message);
  exit(code);
}

void error_ident_not_found(int line, char *ident) {
  printf("line: %d, identifier '%s' undeclared \n", line, ident);
  exit(0);
}

void error_message(int line, char *message) {
  printf("line: %d, %s\n", line, message);
  exit(0);
}

void error_type_not_found(int line, char *ident) {
  printf("line: %d, type '%s' undeclared \n", line, ident);
  exit(0);
}
