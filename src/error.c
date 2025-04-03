#include "error.h"
#include <stdarg.h>

jmp_buf test_compiler_jmp_buf = {0};
int test_has_compiler_error = 0;
char test_error_msg[2048] = {0};

void push_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...) {
    ct_error_t *error = NEW(ct_error_t);

    va_list args;
    va_start(args, format);
    vsprintf(error->msg, format, args);
    va_end(args);

    error->stage = stage;
    error->column = column;
    error->line = line;

    slice_push(m->errors, error);
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

    // intercept 用于 parser 前瞻错误屏蔽
    if (m->intercept_errors != NULL) {
        slice_push(m->intercept_errors, error);
        return;
    }

    // 直接打印错误并退出，暂未实现错误恢复机制
    slice_push(m->errors, error);
    dump_errors_exit(m);
}

void dump_errors_exit(module_t *m) {
    if (m->errors->count == 0) {
        return;
    }

    // 将所有的错误按格式输出即可
    for (int i = 0; i < m->errors->count; ++i) {
        ct_error_t *error = m->errors->take[i];

#ifdef TEST_MODE
        COMPILER_THROW("%s:%d:%d: %s\n", m->rel_path, error->line, error->column, error->msg);
#endif

#ifdef ASSERT_ERROR
        assertf(false, "%s:%d:%d: %s\n", m->rel_path, error->line, error->column, error->msg);
#else
        printf("%s:%d:%d: %s\n", m->rel_path, error->line, error->column, error->msg);
#endif
    }

    exit(EXIT_FAILURE);
}
