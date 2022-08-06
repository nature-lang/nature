#include "module.h"
#include "utils/table.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analysis.h"
#include <string.h>

// TODO
static char *module_full_path(char *path, char *name) {
    char *full_path = str_connect(path, MODULE_SUFFIX);
    // 判断文件是否存在 (import "foo/bar" bar is file)
    if (file_exists(full_path)) {
        return full_path;
    }

    // import "foo/bar" bar is dir, will find foo/bar/bar.n
    full_path = str_connect(path, "/");
    full_path = str_connect(full_path, name);
    full_path = str_connect(full_path, MODULE_SUFFIX); // foo/bar/bar.n
    if (file_exists(full_path)) {
        return full_path;
    }

    return NULL;
}

void complete_import(char *importer_dir, ast_import *import) {
    if (strncmp(base_ns, import->path, strlen(base_ns)) == 0) {
        // 基于命名空间 import
        char *relative_dir = import->path + strlen(base_ns);
        char *module_name = import->path;
        char *rest = strrchr(import->path, '/');
        if (rest != NULL) {
            // example /foo/bar/car => rest: /car
            module_name = rest + 1;
        }
        // 链接
        char *module_path = str_connect(work_dir, relative_dir);
        char *full_path = module_full_path(module_path, module_name);
        if (full_path == NULL) {
            error_exit("[complete_import] module %s not found", import->path);
        }
        if (import->as == NULL) {
            import->as = module_name;
        }
        import->full_path = full_path;
        import->module_unique_name = module_unique_name(full_path);
        return;
    }

    error_exit("[complete_import] only module_path-based(%s) imports are supported", base_ns);
}

char *ident_with_module_unique_name(string unique_name, char *ident) {
    char *temp = str_connect(unique_name, ".");
    temp = str_connect(temp, ident);
    return temp;
}

char *parser_base_ns(char *dir) {
    char *result = dir;
    // 取最后一节
    char *trim_path = strrchr(dir, '/');
    if (trim_path != NULL) {
        result = trim_path + 1;
    }

    return result;
}

module_t *module_new(char *source_path, bool entry) {
    module_t *m = NEW(module_t);
    m->imports = slice_new();
    m->import_table = table_new();
    m->symbols = slice_new();
    m->ast_closures = slice_new();

    m->source_path = source_path;

    if (!file_exists(source_path)) {
        error_exit("[module_new] file %s not found", source_path);
    }
    m->source = file_read(source_path);
    char *temp = strrchr(source_path, '/');
    m->source_dir = rtrim(source_path, strlen(temp));
//    m->namespace = strstr(m->source_dir, base_ns); // 总 base_ns 开始，截止到目录部分
    m->module_unique_name = module_unique_name(source_path);
    m->entry = entry;

    // scanner
    m->token_list = scanner(m);

    // parser
    m->stmt_list = parser(m, m->token_list);

    // analysis => ast_closures
    analysis(m, m->stmt_list);

    return m;
}

char *module_unique_name(char *full_path) {
    // TODO 第三方目录不包含 base_ns
    char *result = strstr(full_path, base_ns); // 总 base_ns 开始，截止到目录部分
    // 去掉结尾的 .n 部分
    result = rtrim(result, strlen(".n"));
    return result;
}
