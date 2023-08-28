#include "error.h"
#include <stdarg.h>

void push_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...) {
    ct_error_t *error = NEW(ct_error_t);

    va_list args;
    va_start(args, format);
    vsprintf(error->msg, format, args);
    va_end(args);


    error->stage = stage;
    error->column = column;
    error->line = line;

    slice_push(m->ct_errors, error);
}

void dump_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...) {
    ct_error_t *error = NEW(ct_error_t);

    va_list args;
    va_start(args, format);
    vsprintf(error->msg, format, args);
    va_end(args);


    error->stage = stage;
    error->column = column;
    error->line = line;
    slice_push(m->ct_errors, error);

    dump_errors(m);
}

void dump_errors(module_t *m) {
    if (m->ct_errors->count == 0) {
        return;
    }

    // 将所有的错误按格式输出即可
    for (int i = 0; i < m->ct_errors->count; ++i) {
        ct_error_t *error = m->ct_errors->take[i];
#ifdef ASSERT_ERROR
        assertf(false, "%s:%d:%d: %s\n", m->rel_path, error->line, error->column, error->msg);
#else
        printf("%s:%d:%d: %s\n", filepath, error->line, error->column, error->msg);
#endif
    }

    exit(1);
}
