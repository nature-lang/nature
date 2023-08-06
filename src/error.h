#ifndef NATURE_ERROR_H
#define NATURE_ERROR_H

#include "types.h"

#define ERROR_STR_SIZE 1024

void push_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...);

/**
 *
 * @param m
 * @param stage
 * @param line
 * @param column
 * @param fmt
 * @param ...
 */
void dump_errors(module_t *m);


#endif //NATURE_ERROR_H
