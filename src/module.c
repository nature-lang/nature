#include "module.h"
#include "utils/table.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analyzer.h"
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

/**
 * 目前必须以 .n 结尾
 * @param importer_dir
 * @param import
 */
void full_import(char *importer_dir, ast_import *import) {
    // import->path 必须以 .n 结尾
    assertf(ends_with(import->path, ".n"), "import file suffix must .n");
    // 不能有以 ./ 或者 / 开头
    assertf(import->path[0] != '.', "cannot use  path=%s begin with '.'", import->path);
    assertf(import->path[0] != '/', "cannot use absolute path=%s", import->path);


    // 去掉 .n 部分, 作为默认的 module as (可能不包含 /)
    char *temp_as = strrchr(import->path, '/'); // foo/bar.n -> /bar.n
    if (temp_as != NULL) {
        temp_as++;
    } else {
        temp_as = import->path;
    }
    char *module_as = str_replace(temp_as, ".n", "");


    // 基于 importer_dir 做相对路径引入
    char *full_path = str_connect("/", import->path);
    full_path = str_connect(importer_dir, full_path);

    // 链接  /root/base_ns/foo/bar.n
    import->full_path = full_path;
    if (import->as == NULL) {
        import->as = module_as;
    }

    import->module_ident = module_unique_ident(import->full_path);
}

module_t *module_build(char *source_path, module_type_t type) {
    module_t *m = NEW(module_t);
    m->imports = slice_new();
    m->import_table = table_new();
    m->global_symbols = slice_new();
    m->call_init_stmt = NULL;
    m->source_path = source_path;
    m->ast_fndefs = slice_new();
    m->closures = slice_new();
    m->asm_global_symbols = slice_new(); // 文件全局符号以及 operations 编译过程中产生的局部符号
    m->asm_operations = slice_new();
    m->asm_temp_var_decl_count = 0;

    assertf(file_exists(source_path), "source file=%s not found", source_path);

    m->source = file_read(source_path);
    char *temp = strrchr(source_path, '/');
    m->source_dir = rtrim(source_path, temp);
    if (type == MODULE_TYPE_COMMON) {
        m->ident = module_unique_ident(source_path);
    } else if (type == MODULE_TYPE_MAIN) {
        m->ident = FN_MAIN_NAME;
    }
    m->type = type;

    // scanner
    m->token_list = scanner(m);

    // parser
    m->stmt_list = parser(m, m->token_list);

    // analyzer => ast_closures
    analyzer(m, m->stmt_list);

    return m;
}

