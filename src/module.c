#include "module.h"
#include "utils/table.h"
#include "utils/helper.h"
#include "src/error.h"
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
    m->global_vardef = slice_new(); // ast_vardef_stmt_t
    m->call_init_stmt = NULL;
    m->source_path = source_path;
    m->ast_fndefs = slice_new();
    m->closures = slice_new();
    m->asm_global_symbols = slice_new(); // 文件全局符号以及 operations 编译过程中产生的局部符号
    m->asm_operations = slice_new();
    m->asm_temp_var_decl_count = 0;
    if (m->package_dir) {
        char *temp_dir = path_dir(m->package_dir);
        m->rel_path = str_replace(m->source_path, temp_dir, "");
        m->rel_path = ltrim(m->rel_path, "/");
    } else {
        m->rel_path = m->source_path;
    }

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
        assert(ast_import->as);
//        assert(ast_import->package_dir);

        // 简单处理
        slice_push(m->imports, ast_import);
        table_set(m->import_table, ast_import->as, ast_import);
    }

    if (type == MODULE_TYPE_MAIN) {
        return m;
    }

    if (type == MODULE_TYPE_TEMP) {
        table_t *temp_symbol_table = table_new();
        for (int i = 0; i < m->stmt_list->count; ++i) {
            ast_stmt_t *stmt = m->stmt_list->take[i];
            SET_LINE_COLUMN(stmt);

            if (stmt->assert_type == AST_STMT_TYPE_ALIAS) {
                ast_type_alias_stmt_t *type_alias = stmt->value;

                char *global_ident = ident_with_module(m->ident, type_alias->ident);
                table_set(temp_symbol_table, global_ident, type_alias);
                continue;
            }

            if (stmt->assert_type == AST_FNDEF) {
                ast_fndef_t *fndef = stmt->value;
                // 由于存在函数的重载，所以同一个 module 下会存在多个同名的 global fn symbol_name
                char *global_ident = ident_with_module(m->ident, fndef->symbol_name); // 全局函数改名
                table_set(temp_symbol_table, global_ident, fndef);
                continue;
            }

            ANALYZER_ASSERTF(false, "module stmt must be var_decl/var_def/fn_decl/type_alias")
        }

        table_set(import_temp_symbol_table, m->source_path, temp_symbol_table);
    }

    if (type == MODULE_TYPE_COMMON) {
        // 全局 table 记录 import 下的所有符号, type 为 common 时才进行记录
        // import handle
        for (int i = 0; i < m->stmt_list->count; ++i) {
            ast_stmt_t *stmt = m->stmt_list->take[i];
            SET_LINE_COLUMN(stmt);

            if (stmt->assert_type == AST_STMT_IMPORT) {
                continue;
            }

            if (stmt->assert_type == AST_VAR_DECL) {
                ast_var_decl_t *var_decl = stmt->value;
                char *global_ident = ident_with_module(m->ident, var_decl->ident);
                table_set(can_import_symbol_table, global_ident, var_decl);
                continue;
            }

            if (stmt->assert_type == AST_STMT_VARDEF) {
                ast_vardef_stmt_t *vardef = stmt->value;
                ast_var_decl_t *var_decl = &vardef->var_decl;
                char *global_ident = ident_with_module(m->ident, var_decl->ident);
                table_set(can_import_symbol_table, global_ident, var_decl);
                continue;
            }

            if (stmt->assert_type == AST_STMT_TYPE_ALIAS) {
                ast_type_alias_stmt_t *type_alias = stmt->value;
                char *global_ident = ident_with_module(m->ident, type_alias->ident);
                table_set(can_import_symbol_table, global_ident, type_alias);
                continue;
            }

            if (stmt->assert_type == AST_FNDEF) {
                ast_fndef_t *fndef = stmt->value;
                // 由于存在函数的重载，所以同一个 module 下会存在多个同名的 global fn symbol_name
                char *global_ident = ident_with_module(m->ident, fndef->symbol_name); // 全局函数改名
                table_set(can_import_symbol_table, global_ident, fndef);
                continue;
            }

            ANALYZER_ASSERTF(false, "module stmt must be var_decl/var_def/fn_decl/type_alias")
        }
    }

    return m;
}

