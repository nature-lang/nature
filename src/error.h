#ifndef NATURE_ERROR_H
#define NATURE_ERROR_H

#include "types.h"
#include <setjmp.h>

#define ERROR_STR_SIZE 1024

extern jmp_buf test_compiler_jmp_buf;
extern int test_has_compiler_error;
extern char test_error_msg[2048];

#define COMPILER_TRY if (!setjmp(test_compiler_jmp_buf))

#define COMPILER_THROW(_fmt, ...) do { \
    snprintf(test_error_msg, sizeof(test_error_msg) - 1, _fmt, ##__VA_ARGS__);\
    longjmp(test_compiler_jmp_buf, 1);\
} while(0)

#define SET_LINE_COLUMN(src) { \
    m->current_line = src->line;\
    m->current_column = src->column;\
}

#define LINEAR_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_LINEAR, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define INFER_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_INFER, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define ANALYZER_ASSERTF(cond, fmt, ...) { \
    if (!(cond)) { \
        dump_errorf(m, CT_STAGE_ANALYZER, m->current_line, m->current_column, fmt, ##__VA_ARGS__); \
    } \
}

#define PARSER_ASSERTF(condition, fmt, ...) { \
        ((condition) ?"": dump_errorf(m, CT_STAGE_PARSER, parser_peek(m)->line, parser_peek(m)->column, fmt, ##__VA_ARGS__)); \
    }

#define SCANNER_ASSERTF(condition, fmt, ...) { \
        ((condition) ?"": dump_errorf(m, CT_STAGE_SCANNER, m->s_cursor.line, m->s_cursor.column, fmt, ##__VA_ARGS__)); \
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
