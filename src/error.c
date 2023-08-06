#include "error.h"
#include <stdarg.h>

void push_errorf(module_t *m, ct_stage stage, int line, int column, char *format, ...) {
    ct_error_t *error = NEW(ct_error_t);

    va_list args;
    va_start(args, format);
    vsprintf(error->msg, format, args);
    va_end(args);

    char *temp_dir = path_dir(m->package_dir);
    char *filepath = str_replace(m->source_path, temp_dir, "");
    filepath = ltrim(filepath, "/");

    error->stage = stage;
    error->column = column;
    error->line = line;
}

void dump_errors(module_t *m) {
    // 将所有的错误按格式输出即可

    exit(1);
}
