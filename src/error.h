#ifndef NATURE_ERROR_H
#define NATURE_ERROR_H

#include "types.h"

#define ERROR_STR_SIZE 1024

#define SET_LINE_COLUMN(src) { \
    assert(src->line > 0);      \
    m->current_line = src->line;\
    m->current_column = src->column;\
}

#define LINEAR_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_LINEAR, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define CHECKING_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_CHECKING, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define ANALYZER_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_ANALYZER, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define PARSER_ASSERTF(condition, fmt, ...) { \
        ((condition) ?: dump_errorf(m, CT_STAGE_PARSER, parser_peek(m)->line, parser_peek(m)->column, fmt, ##__VA_ARGS__)); \
    }

void push_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...);

void dump_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...);

/**
 *
 * @param m
 * @param stage
 * @param line
 * @param column
 * @param fmt
 * @param ...
 */
void dump_errors_exit(module_t *m);


#endif //NATURE_ERROR_H
