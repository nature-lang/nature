#include "module.h"
#include "utils/table.h"
#include "utils/helper.h"
#include "utils/error.h"
#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "src/semantic/analyzer.h"
#include "src/semantic/generic.h"
#include "src/build/config.h"
#include <string.h>
#include <assert.h>
#include "utils/assertf.h"

/**
 * @param source_path
 * @param type
 * @return
 */
module_t *module_build(ast_import_t *import, char *source_path, module_type_t type) {
    module_t *m = NEW(module_t);

    if (import) {
        m->package_dir = import->package_dir;
        m->package_conf = import->package_conf;
        m->ident = import->module_ident;
    }

    m->ct_errors = slice_new();
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
    m->type = type;

    // scanner
    m->token_list = scanner(m);

    // parser
    m->stmt_list = parser(m, m->token_list);

    // analyzer => ast_fndefs(global)
    analyzer(m, m->stmt_list);

    // generic => ast_fndef(global+local flat)
    generic(m);

    return m;
}

