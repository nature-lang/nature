#include "module.h"
#include "utils/table.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analysis.h"
#include "src/build/config.h"
#include <string.h>
#include <assert.h>
#include "utils/assertf.h"

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
    // import 目前必须基于 BASE_NS 开始，不支持相对路径
    if (strncmp(BASE_NS, import->path, strlen(BASE_NS)) == 0) {
        // 基于命名空间 import
        char *relative_dir = import->path + strlen(BASE_NS);
        char *module_name = import->path;
        char *rest = strrchr(import->path, '/');
        if (rest != NULL) {
            // example /foo/bar/car => rest: /car
            module_name = rest + 1;
        }
        // 链接
        char *module_path = str_connect(WORK_DIR, relative_dir);
        char *full_path = module_full_path(module_path, module_name);
        if (full_path == NULL) {
            error_exit("[complete_import] module %s not found", import->path);
        }
        if (import->as == NULL) {
            import->as = module_name;
        }
        import->full_path = full_path;
        import->module_ident = module_unique_ident(full_path);
        return;
    }

    assertf(false, "import grammar only support BASE_NS=%s start, actual=%s", BASE_NS, import->path);
}

char *ident_with_module(char *module_ident, char *ident) {
    char *temp = str_connect(module_ident, ".");
    temp = str_connect(temp, ident);
    return temp;
}

module_t *module_build(char *source_path, bool entry) {
    module_t *m = NEW(module_t);
    m->imports = slice_new();
    m->import_table = table_new();
    m->global_symbols = slice_new();
    m->ast_closures = slice_new();
    m->call_init_stmt = NULL;
    m->source_path = source_path;
    m->closures = slice_new();
    // native 字段初始化
    m->asm_global_symbols = slice_new(); // 文件全局符号以及 operations 编译过程中产生的局部符号
    m->asm_operations = slice_new();
    m->asm_temp_var_decl_count = 0;

    assertf(file_exists(source_path), "source file=%s not found", source_path);

    m->source = file_read(source_path);
    char *temp = strrchr(source_path, '/');
    m->source_dir = rtrim(source_path, strlen(temp));
//    m->namespace = strstr(m->source_dir, BASE_NS); // 总 BASE_NS 开始，截止到目录部分
    m->ident = module_unique_ident(source_path);
    m->entry = entry;

    // scanner
    m->token_list = scanner(m);

    // parser
    m->stmt_list = parser(m, m->token_list);

    // analysis => ast_closures
    analysis(m, m->stmt_list);

    return m;
}

char *module_unique_ident(char *full_path) {
    char *result = str_replace(full_path, WORK_DIR, ""); // 从 BASE_NS 开始，截止到目录部分

    result = str_connect(BASE_NS, result);
    // 去掉结尾的 .n 部分
    result = rtrim(result, strlen(".n"));

    // replace dot
    result = str_replace(result, "/", ".");
    return result;
}
