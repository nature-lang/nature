#include "module.h"

#include <assert.h>
#include <string.h>

#include "src/error.h"
#include "src/module_index.h"
#include "src/semantic/analyzer.h"
#include "src/syntax/parser.h"
#include "src/syntax/scanner.h"

int64_t global_var_unique_count = 0;

/**
 * Switch the module's processing focus to one source part
 * scanner/parser/analyzer all work off these module_t fields
 */
void module_set_current_source(module_t *m, source_file_t *sf) {
    m->current_source = sf;
    m->source = sf->source;
    m->source_path = sf->path;
    m->source_dir = sf->dir;
    m->rel_path = sf->rel_path;
    m->token_list = sf->token_list;
    m->stmt_list = sf->stmt_list;
    m->imports = sf->imports;
    m->import_table = sf->import_table;
    m->selective_import_table = sf->selective_import_table;
}

/**
 * source rel_path 需要保留目录名称，取上一级目录作为 base ns, 需要处理特殊情况 /root/main.n 这种情况的编译
 */
static char *module_source_rel(module_t *m, char *source_path) {
    if (m->package_dir) {
        // package dir maybe eqs /root
        char *temp_dir = path_dir(m->package_dir);
        char *rel_path;
        if (str_equal(temp_dir, "") || str_equal(temp_dir, "/")) {
            rel_path = source_path;
        } else {
            rel_path = str_replace(source_path, temp_dir, "");
        }
        free(temp_dir);
        assert(rel_path);

        return ltrim(rel_path, "/");
    }

    if (strstr(source_path, NATURE_ROOT) != NULL) {
        // builtin
        char *rel_path = str_replace(source_path, NATURE_ROOT, "");
        assert(rel_path);
        return ltrim(rel_path, "/");
    }

    return source_path;
}

static source_file_t *module_source_new(module_t *m, char *source_path) {
    assertf(file_exists(source_path), "source file=%s not found", source_path);

    source_file_t *sf = NEW(source_file_t);
    sf->path = source_path;
    sf->dir = path_dir(source_path);
    sf->rel_path = module_source_rel(m, source_path);
    sf->source = file_read(source_path);
    sf->imports = slice_new();
    sf->import_table = table_new();
    sf->selective_import_table = table_new();
    sf->fn_list = slice_new();

    return sf;
}

/**
 * scanner + parser, also handling the leading mod declaration
 */
static void module_source_parse(module_t *m, source_file_t *sf) {
    module_set_current_source(m, sf);

    sf->token_list = scanner(m);
    m->token_list = sf->token_list;

    sf->stmt_list = parser(m, sf->token_list);
    m->stmt_list = sf->stmt_list;

    if (sf->stmt_list->count == 0) {
        return;
    }

    ast_stmt_t *first = sf->stmt_list->take[0];
    if (first->assert_type != AST_STMT_MOD) {
        return;
    }

    ast_mod_stmt_t *mod_stmt = first->value;
    SET_LINE_COLUMN(first);

    ANALYZER_ASSERTF(m->package_conf, "cannot use 'mod' without package.toml");

    // the package scan already read the mod header, the two must agree
    ANALYZER_ASSERTF(m->mod_ident && str_equal(m->mod_ident, mod_stmt->ident),
                     "'mod %s' declaration is not at the head of the file", mod_stmt->ident);

    // the mod declaration itself takes no part in declaration collection
    slice_remove(sf->stmt_list, 0);
}

/**
 * analyzer import pre-pass, imports only take effect within the current file
 */
static void module_source_imports(module_t *m, source_file_t *sf) {
    module_set_current_source(m, sf);

    for (int i = 0; i < sf->stmt_list->count; ++i) {
        ast_stmt_t *stmt = sf->stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            break;
        }
        SET_LINE_COLUMN(stmt);

        ast_import_t *ast_import = stmt->value;

        analyzer_import(m, ast_import);

        // 简单处理
        slice_push(sf->imports, ast_import);
        slice_push(m->module_imports, ast_import);

        // Handle selective imports: import math.{sqrt, pow, Pi as pi}
        if (ast_import->is_selective) {
            for (int j = 0; j < ast_import->select_items->count; ++j) {
                ast_import_select_item_t *item = ast_import->select_items->take[j];
                char *local_name = item->alias ? item->alias : item->ident;

                // Check for redeclaration
                if (table_exist(sf->selective_import_table, local_name) ||
                    table_exist(sf->import_table, local_name)) {
                    ANALYZER_ASSERTF(false, "import item '%s' conflicts with existing import", local_name);
                }

                // Create selective import reference
                ast_import_select_t *select_ref = NEW(ast_import_select_t);
                select_ref->module_ident = ast_import->module_ident;
                select_ref->original_ident = item->ident;
                select_ref->import = ast_import;

                table_set(sf->selective_import_table, local_name, select_ref);
            }
        } else if (ast_import->as && strlen(ast_import->as) > 0) {
            // import is_tpl 是全局导入，所以没有 is_tpl
            table_set(sf->import_table, ast_import->as, ast_import);
        }
    }
}

/**
 * Top-level declarations are shared by the whole module, so every part's declarations are collected
 * before any function body or initializer is analyzed
 */
static void module_source_register_symbols(module_t *m, source_file_t *sf) {
    module_set_current_source(m, sf);

    for (int i = 0; i < sf->stmt_list->count; ++i) {
        ast_stmt_t *stmt = sf->stmt_list->take[i];
        SET_LINE_COLUMN(stmt);

        if (stmt->assert_type == AST_STMT_IMPORT) {
            continue;
        }

        if (stmt->assert_type == AST_STMT_MOD) {
            ANALYZER_ASSERTF(false, "'mod' declaration must be the first statement of the file");
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
}

/**
 * @param import  the import for this module (may be NULL for a builtin module)
 * @param source_paths char*, all active source parts of the module
 * @param type
 * @return
 */
module_t *module_build_sources(ast_import_t *import, slice_t *source_paths, module_type_t type) {
    assert(source_paths && source_paths->count > 0);

    module_t *m = NEW(module_t);

    if (import) {
        m->package_dir = import->package_dir;
        m->package_conf = import->package_conf;
        m->ident = import->module_ident;
        m->mod_ident = import->module_unit ? import->module_unit->mod_ident : NULL;
        m->label_prefix = import->module_ident;
    }

    m->errors = slice_new();
    m->intercept_errors = NULL;
    m->sources = slice_new();
    m->module_imports = slice_new();
    m->imports = slice_new();
    m->import_table = table_new();
    m->selective_import_table = table_new();
    m->global_symbol_table = table_new();
    m->global_symbols = slice_new();
    m->global_vardef = slice_new(); // ast_vardef_stmt_t
    m->call_init_stmt = NULL;
    m->infer_type_args_stack = stack_new();
    m->ast_fndefs = slice_new();
    m->ast_typedefs = slice_new();
    m->closures = slice_new();
    m->asm_global_symbols = slice_new(); // 文件全局符号以及 operations 编译过程中产生的局部符号
    m->asm_operations = slice_new();
    m->asm_temp_var_decl_count = 0;
    m->type = type;

    // primary source, used as the label_prefix fallback and in diagnostics
    m->source_path = source_paths->take[0];

    for (int i = 0; i < source_paths->count; ++i) {
        source_file_t *sf = module_source_new(m, source_paths->take[i]);
        slice_push(m->sources, sf);
    }

    source_file_t *primary = m->sources->take[0];

    if (m->label_prefix == NULL) {
        // 去掉 .n
        // / -> .
        char *temp = str_replace(primary->rel_path, "/", ".");
        m->label_prefix = str_replace(temp, ".n", "");
    }
    assert(m->label_prefix);

    module_set_current_source(m, primary);

    // 1. scanner + parser
    for (int i = 0; i < m->sources->count; ++i) {
        module_source_parse(m, m->sources->take[i]);
    }

    // 2. resolve imports, an import only takes effect in the file declaring it
    for (int i = 0; i < m->sources->count; ++i) {
        module_source_imports(m, m->sources->take[i]);
    }

    // 3. register top-level declarations, shared by the whole module
    for (int i = 0; i < m->sources->count; ++i) {
        module_source_register_symbols(m, m->sources->take[i]);
    }

    module_set_current_source(m, primary);

    return m;
}

module_t *module_build(ast_import_t *import, char *source_path, module_type_t type) {
    slice_t *source_paths = slice_new();
    slice_push(source_paths, source_path);
    return module_build_sources(import, source_paths, type);
}
