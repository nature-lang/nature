#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "error.h"

//void error_exit(int code, char *message) {
//  printf("exception, message: %s\n", message);
//  exit(code);
//
//  va_list args;
//  char *buf = malloc(sizeof(char) * ERROR_STR_COUNT);
//  va_start(args, format);
//  vsprintf(buf, format, args);
//  va_end(args);
//
//  printf("line: %d, %s", line, buf);
//  exit(1);
//}

void error_exit(char *format, ...) {
    va_list args;
    char *buf = malloc(sizeof(char) * ERROR_STR_COUNT);
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);

    printf("%s", buf);
    exit(1);
}

void error_ident_not_found(int line, char *ident) {
    printf("[error_ident_not_found] line: %d, identifier '%s' undeclared \n", line, ident);
    exit(1);
}

void error_message(int line, char *message) {
    printf("line: %d, %s\n", line, message);
    exit(1);
}

void error_type_not_found(int line, char *ident) {
    printf("line: %d, code '%s' undeclared \n", line, ident);
    exit(1);
}

void error_redeclare_ident(int line, char *ident) {
    printf("line: %d,  redeclare ident '%s'\n", line, ident);
    exit(1);
}

void error_type_not_match(int line) {
    printf("line: %d, cannot assigned variables, because code inconsistency", line);
    exit(1);
}

void error_printf(int line, char *format, ...) {
    va_list args;
    char *buf = malloc(sizeof(char) * ERROR_STR_COUNT);
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);

    printf("line: %d, %s", line, buf);
    exit(1);
}
