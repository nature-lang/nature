#include "module.h"

#include <assert.h>
#include <string.h>

#include "src/error.h"
#include "src/semantic/analyzer.h"
#include "src/syntax/parser.h"
#include "src/syntax/scanner.h"

int64_t global_var_unique_count = 0;

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
        m->label_prefix = import->module_ident;
    }

    m->errors = slice_new();
    m->intercept_errors = NULL;
    m->imports = slice_new();
    m->import_table = table_new();
    m->global_symbols = slice_new();
    m->global_vardef = slice_new(); // ast_vardef_stmt_t
    m->call_init_stmt = NULL;
    m->source_path = source_path;
    m->infer_type_args_stack = stack_new();
    m->ast_fndefs = slice_new();
    m->closures = slice_new();
    m->asm_global_symbols = slice_new(); // 文件全局符号以及 operations 编译过程中产生的局部符号
    m->asm_operations = slice_new();
    m->asm_temp_var_decl_count = 0;
    if (m->package_dir) {
        // source rel_path 需要保留目录名称，取上一级目录作为 base ns, 需要处理特殊情况 /root/main.n 这种情况的编译
        // package dir maybe eqs /root
        char *temp_dir = path_dir(m->package_dir);
        if (str_equal(temp_dir, "") || str_equal(temp_dir, "/")) {
            m->rel_path = m->source_path;
        } else {
            m->rel_path = str_replace(m->source_path, temp_dir, "");
        }
        assert(m->rel_path);

        m->rel_path = ltrim(m->rel_path, "/");
    } else if (strstr(m->source_path, NATURE_ROOT) != NULL) {
        // builtin
        m->rel_path = str_replace(m->source_path, NATURE_ROOT, "");
        assert(m->rel_path);
        m->rel_path = ltrim(m->rel_path, "/");
    } else {
        m->rel_path = m->source_path;
    }

    if (m->label_prefix == NULL) {
        // 去掉 .n
        // / -> .
        char *temp = str_replace(m->rel_path, "/", ".");
        m->label_prefix = str_replace(temp, ".n", "");
    }

    assert(m->label_prefix);
    assertf(file_exists(source_path), "source file=%s not found", source_path);

    m->source = file_read(source_path);
    char *temp = strrchr(source_path, '/');
    m->source_dir = rtrim(source_path, temp);
    m->type = type;

    // scanner
    m->token_list = scanner(m);

    // parser
    m->stmt_list = parser(m, m->token_list);

    // analyzer import 预处理
    for (int i = 0; i < m->stmt_list->count; ++i) {
        ast_stmt_t *stmt = m->stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            break;
        }
        SET_LINE_COLUMN(stmt);

        ast_import_t *ast_import = stmt->value;

        analyzer_import(m, ast_import);

        // 简单处理
        slice_push(m->imports, ast_import);

        // import is_tpl 是全局导入，所以没有 is_tpl
        if (ast_import->as && strlen(ast_import->as) > 0) {
            table_set(m->import_table, ast_import->as, ast_import);
        }
    }

    // register global symbols
    for (int i = 0; i < m->stmt_list->count; ++i) {
        ast_stmt_t *stmt = m->stmt_list->take[i];
        SET_LINE_COLUMN(stmt);

        if (stmt->assert_type == AST_STMT_IMPORT) {
            continue;
        }

        if (stmt->assert_type == AST_STMT_VARDEF) {
            ast_vardef_stmt_t *vardef = stmt->value;
            ast_var_decl_t *var_decl = &vardef->var_decl;
            var_decl->ident = ident_with_prefix(m->ident, var_decl->ident);
            symbol_t *s = symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, false);
            ANALYZER_ASSERTF(s, "ident '%s' redeclared", var_decl->ident);
            continue;
        }

        if (stmt->assert_type == AST_STMT_CONSTDEF) {
            ast_constdef_stmt_t *const_def = stmt->value;
            const_def->ident = ident_with_prefix(m->ident, const_def->ident);
            symbol_t *s = symbol_table_set(const_def->ident, SYMBOL_CONST, const_def, false);
            ANALYZER_ASSERTF(s, "ident '%s' redeclared", const_def->ident);
            continue;
        }

        if (stmt->assert_type == AST_STMT_TYPEDEF) {
            ast_typedef_stmt_t *typedef_stmt = stmt->value;
            typedef_stmt->ident = ident_with_prefix(m->ident, typedef_stmt->ident);
            symbol_t *s = symbol_table_set(typedef_stmt->ident, SYMBOL_TYPE, typedef_stmt, false);
            ANALYZER_ASSERTF(s, "ident '%s' redeclared", typedef_stmt->ident);
            continue;
        }

        if (stmt->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = stmt->value;

            if (fndef->impl_type.kind == 0) {
                fndef->symbol_name = ident_with_prefix(m->ident, fndef->symbol_name); // 全局函数改名
                symbol_t *s = symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, false);
                ANALYZER_ASSERTF(s, "ident '%s' redeclared", fndef->symbol_name);
            } else {
                // Delay to analyzer module and then process it...
            }
            continue;
        }

        ANALYZER_ASSERTF(false, "module stmt must be var_decl/var_def/fn_decl/type_alias")
    }

    return m;
}
