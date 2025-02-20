#include "analyzer.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <src/lir.h>

#include "src/error.h"
#include "utils/helper.h"

static void analyzer_var_decl(module_t *m, ast_var_decl_t *var_decl, bool redeclare_check);

static void analyzer_expr(module_t *m, ast_expr_t *expr);

static void analyzer_stmt(module_t *m, ast_stmt_t *stmt);

static void analyzer_var_tuple_destr(module_t *m, ast_tuple_destr_t *tuple_destr);

static bool analyzer_special_type_rewrite(module_t *m, type_t *type);

static void analyzer_local_fndef(module_t *m, ast_fndef_t *fndef);

static void analyzer_call(module_t *m, ast_call_t *call);

char *analyzer_force_unique_ident(module_t *m) {
    if (m->ident) {
        return str_connect(m->ident, ".n.o");
    }

    // m->source_path = "/tmp/tmp.wKB2qsXqAh/std/builtin/coroutine.n"
    // 1. 截取 coroutine.n 这一截
    // 2. std.builtin.coroutine.n 增加固定前缀 std.builtin.
    // 3. 增加固定后缀 std.builtin.coroutine.n.o
    if (m->type == MODULE_TYPE_BUILTIN) {
        char *temp = strrchr(m->source_path, '/');
        char *ident = ltrim(temp, "/");
        ident = str_replace(ident, ".n", ".n.o");
        ident = str_replace(ident, "/", ".");
        ident = str_connect("std.builtin.", ident);
        return ident;
    }

    assert(false);
}

static void analyzer_import_std(module_t *m, char *package, ast_import_t *import) {
    // /usr/local/nature/std
    ANALYZER_ASSERTF(NATURE_ROOT, "NATURE_ROOT not found");

    char *package_dir = path_join(NATURE_ROOT, "std");
    package_dir = path_join(package_dir, package);

    char *package_conf_path = path_join(package_dir, PACKAGE_TOML);
    ANALYZER_ASSERTF(file_exists(package_conf_path), "package.toml=%s not found", package_conf_path);

    import->use_links = true; // std 默认可以加载其中的 links
    import->package_dir = package_dir;
    import->package_conf = package_parser(package_conf_path);
}

/**
 * 基于 m->package_toml 完善 import
 * @param m
 * @param import
 * @return
 */
static void analyzer_import_dep(module_t *m, char *package, ast_import_t *import) {
    char *type = package_dep_str_in(m->package_conf, package, "type");
    char *package_dir;

    if (str_equal(type, TYPE_GIT)) {
        package_dir = package_dep_git_dir(m->package_conf, package);
    } else if (str_equal(type, TYPE_LOCAL)) {
        package_dir = package_dep_local_dir(m->package_conf, package);
        ANALYZER_ASSERTF(package_dir, "package=%s not found", package);
    } else {
        ANALYZER_ASSERTF(false, "unknown package dep type=%s", type);
        exit(1);
    }

    char *package_conf_path = path_join(package_dir, PACKAGE_TOML);
    ANALYZER_ASSERTF(file_exists(package_conf_path), "package.toml=%s not found", package_conf_path);

    import->use_links = package_dep_bool_in(m->package_conf, package, "use_links");
    import->package_dir = package_dir;
    import->package_conf = package_parser(package_conf_path);
}

static void analyzer_body(module_t *m, slice_t *body) {
    for (int i = 0; i < body->count; ++i) {
        analyzer_stmt(m, body->take[i]);
    }
}

/**
 * 目前必须以 .n 结尾
 * @param importer_dir
 * @param import
 */
void analyzer_import(module_t *m, ast_import_t *import) {
    // - import file
    if (import->file) {
        assert(strlen(import->file) > 0);
        // import->path 必须以 .n 结尾
        ANALYZER_ASSERTF(ends_with(import->file, ".n"), "import file suffix must .n");

        // 不能有以 ./ 或者 / 开头
        ANALYZER_ASSERTF(import->file[0] != '.', "cannot use path=%s begin with '.'", import->file);

        ANALYZER_ASSERTF(import->file[0] != '/', "cannot use absolute path=%s", import->file);

        // 去掉 .n 部分, 作为默认的 module as (可能不包含 /)
        char *temp_as = strrchr(import->file, '/'); // foo/bar.n -> /bar.n
        if (temp_as != NULL) {
            temp_as++;
        } else {
            temp_as = import->file;
        }
        char *module_as = str_replace(temp_as, ".n", "");

        // 基于当前 module 的 source_dir 做相对路径引入 (不支持跨平台引入)
        // import file 模式下直接使用当前 module 携带的 package 即可,可能为 null
        // 链接  /root/base_ns/foo/bar.n
        import->full_path = path_join(m->source_dir, import->file);
        if (!import->as || strlen(import->as) == 0) {
            import->as = module_as;
        }

        import->package_conf = m->package_conf;
        import->package_dir = m->package_dir;
        import->module_ident = module_unique_ident(import);
        import->module_type = MODULE_TYPE_COMMON;
        return;
    }

    ANALYZER_ASSERTF(import->ast_package->count > 0, "import exception");
    char *package = import->ast_package->take[0];
    char *module = import->ast_package->take[import->ast_package->count - 1];

    // - import module
    if (!m->package_conf && is_std_package(package)) {
        analyzer_import_std(m, package, import);
    } else {
        // 一旦引入了包管理, 同名包优先级 current > dep > std
        ANALYZER_ASSERTF(m->package_conf, "cannot 'import %s', not found package.toml", package);

        // package module(这里包含三种情况, 一种就是当前 package 自身 >  一种是 std package > 一种是外部 package)
        if (is_current_package(m->package_conf, package)) {
            // 基于 package_toml 定位到 root dir, 然后进行 file 的拼接操作
            import->package_dir = m->package_dir;
            import->package_conf = m->package_conf;
        } else if (is_dep_package(m->package_conf, package)) {
            analyzer_import_dep(m, package, import);
        } else if (is_std_package(package)) {
            analyzer_import_std(m, package, import);
        } else {
            ANALYZER_ASSERTF(false, "import package '%s' not found", package);
        }
    }

    // import foo.bar => foo is package.name, so import workdir/bar.n
    import->full_path = package_import_fullpath(import->package_conf, import->package_dir, import->ast_package);
    ANALYZER_ASSERTF(file_exists(import->full_path), "cannot import package '%s': not found",
                     ast_import_package_tostr(import->ast_package));
    ANALYZER_ASSERTF(ends_with(import->full_path, ".n"), "import file suffix must .n");

    if (!import->as || strlen(import->as) == 0) {
        import->as = import->ast_package->take[import->ast_package->count - 1];
    }

    import->module_type = MODULE_TYPE_COMMON;
    // package 模式下的 ident 应该基于 package module?
    import->module_ident = module_unique_ident(import);
}

static type_t analyzer_type_fn(ast_fndef_t *fndef) {
    type_fn_t *f = NEW(type_fn_t);
    f->fn_name = fndef->fn_name;
    f->param_types = ct_list_new(sizeof(type_t));
    f->return_type = fndef->return_type;
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *var = ct_list_value(fndef->params, i);
        ct_list_push(f->param_types, &var->type);
    }
    f->is_rest = fndef->rest_param;
    f->is_errable = fndef->is_errable;
    type_t result = type_new(TYPE_FN, f);
    result.status = REDUCTION_STATUS_UNDO;

    fndef->type = result;

    return result;
}

/**
 * type 不像 var 一样可以通过 env 引入进来，type 只有作用域的概念
 * 如果在当前文件的作用域中没有找到当前 type 则返回 NULL, 外部需要根据该符号进行判断
 * @param m
 * @param current
 * @param ident
 * @return
 */
static char *analyzer_resolve_type_alias(module_t *m, analyzer_fndef_t *current, string ident) {
    if (current == NULL) {
        // - 当前 module 的全局 type
        // analyzer 在初始化 module 时已经将这些符号全都注册到了全局符号表中 (module_ident + ident)
        // can_import_symbol_table 记录了所有的 package + ident 组成的符号，所以也包括 current module
        // 所以可以用来判断是否 import 了当前 package 中的 symbol
        char *global_ident = ident_with_prefix(m->ident, ident);
        if (table_exist(can_import_symbol_table, global_ident)) {
            return global_ident;
        }

        // - import xxx as * 产生的全局符号
        for (int i = 0; i < m->imports->count; ++i) {
            ast_import_t *import = m->imports->take[i];

            if (str_equal(import->as, "*")) {
                char *temp = ident_with_prefix(import->module_ident, ident);
                if (table_exist(can_import_symbol_table, temp)) {
                    return temp;
                }
            }
        }

        // builtin 中的全局类型
        symbol_t *s = table_get(symbol_table, ident);
        if (s != NULL) {
            // 不需要改写使用的名称了
            return ident;
        }

        // ANALYZER_ASSERTF(false, "type '%s' undeclared \n", ident);
        return NULL;
    }

    // - 优先查找当前作用域中的同名符号
    slice_t *locals = current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];
        if (str_equal(ident, local->ident)) {
            // 找到了同名符号，但是该同名符号不是一个正确的类型
            ANALYZER_ASSERTF(local->type == SYMBOL_TYPE_ALIAS, "ident=%s not type", local->ident);

            // 使用 unique ident
            return local->unique_ident;
        }
    }

    return analyzer_resolve_type_alias(m, current->parent, ident);
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。只需要类型为 typedef ident 时能够定位唯一名称即可
 * @param type
 */
static void analyzer_type(module_t *m, type_t *type) {
    m->current_line = type->line;
    m->current_column = type->column;

    // type foo = int
    // 'foo' is type_alias
    // alias 可能是一些特殊符号，首先检测是否已经定义，如果没有预定义，则使用特殊符号的含义
    // 比如 void_ptr/raw_ptr/ptr
    if (type->kind == TYPE_ALIAS) {
        // import 全局模式 alias 处理, 例如 type a = package.foo, 对 package.foo 必定是其他 module 的 type alias 定义的
        type_alias_t *type_alias = type->alias;
        // foo.bar
        if (type_alias->import_as) {
            ast_import_t *import = table_get(m->import_table, type_alias->import_as);
            ANALYZER_ASSERTF(import, "type left ident = %s not found in import", type_alias->import_as);

            char *unique_ident = ident_with_prefix(import->module_ident, type_alias->ident);
            // 更新 ident 指向
            type_alias->ident = unique_ident;

            type_alias->import_as = NULL;
        } else {
            // local ident 或者当前 module 下的全局 ident, import as * 中的全局 ident
            char *unique_alias_ident = analyzer_resolve_type_alias(m, m->analyzer_current, type_alias->ident);

            // 在没有找到类型的 alias 前提下, 判断是否是内置特殊类型, 这样可以不占用关键字
            if (!unique_alias_ident) {
                if (analyzer_special_type_rewrite(m, type)) {
                    return;
                }

                ANALYZER_ASSERTF(false, "type '%s' undeclared \n", type_alias->ident);
            }
            type_alias->ident = unique_alias_ident;
        }

        // foo<arg1,>
        if (type_alias->args) {
            // actual param 处理
            for (int i = 0; i < type_alias->args->length; ++i) {
                type_t *temp = ct_list_value(type_alias->args, i);
                analyzer_type(m, temp);
            }
        }

        return;
    }

    if (type->kind == TYPE_UNION) {
        type_union_t *u = type->union_;
        if (!u->any) {
            for (int i = 0; i < u->elements->length; ++i) {
                type_t *temp = ct_list_value(u->elements, i);
                analyzer_type(m, temp);
            }
            return;
        }
    }

    if (type->kind == TYPE_MAP) {
        type_map_t *map_decl = type->map;
        analyzer_type(m, &map_decl->key_type);
        analyzer_type(m, &map_decl->value_type);
        return;
    }

    if (type->kind == TYPE_SET) {
        type_set_t *set = type->set;
        analyzer_type(m, &set->element_type);
        return;
    }

    if (type->kind == TYPE_VEC) {
        type_vec_t *list = type->vec;
        analyzer_type(m, &list->element_type);
        return;
    }

    if (type->kind == TYPE_CHAN) {
        type_chan_t *chan = type->chan;
        analyzer_type(m, &chan->element_type);
        return;
    }

    if (type->kind == TYPE_ARR) {
        type_array_t *array = type->array;
        analyzer_type(m, &array->element_type);
        return;
    }

    if (type->kind == TYPE_TUPLE) {
        type_tuple_t *tuple = type->tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            type_t *element_type = ct_list_value(tuple->elements, i);
            analyzer_type(m, element_type);
        }
        return;
    }

    if (type->kind == TYPE_PTR || type->kind == TYPE_RAW_PTR) {
        type_ptr_t *pointer = type->ptr;
        analyzer_type(m, &pointer->value_type);
        return;
    }

    if (type->kind == TYPE_FN) {
        type_fn_t *type_fn = type->fn;
        analyzer_type(m, &type_fn->return_type);
        for (int i = 0; i < type_fn->param_types->length; ++i) {
            type_t *t = ct_list_value(type_fn->param_types, i);
            analyzer_type(m, t);
        }
    }

    if (type->kind == TYPE_STRUCT) {
        type_struct_t *struct_decl = type->struct_;
        for (int i = 0; i < struct_decl->properties->length; ++i) {
            struct_property_t *item = ct_list_value(struct_decl->properties, i);

            analyzer_type(m, &item->type);

            // 可选的右值解析
            if (item->right) {
                // cannot contains ast_fndef 标识
                m->analyzer_has_fndef = false;
                analyzer_expr(m, item->right);

                ANALYZER_ASSERTF(m->analyzer_has_fndef == false,
                                 "struct field default value cannot be a fn def, use fn def ident instead");
            }
        }
    }
}

/**
 * ptr/void_ptr/raw_ptr/all_t/fn_t 不作为关键字，如果用户没有自定义覆盖, 则转换为需要的类型
 */
static bool analyzer_special_type_rewrite(module_t *m, type_t *type) {
    assert(type->kind == TYPE_ALIAS);
    type_alias_t *type_alias = type->alias;
    assert(type->alias->import_as == NULL);

    // void ptr
    if (str_equal(type_alias->ident, type_kind_str[TYPE_VOID_PTR])) {
        type->kind = TYPE_VOID_PTR;
        type->value = NULL;
        type->origin_ident = NULL;

        ANALYZER_ASSERTF(type_alias->args == NULL, "void_ptr cannot contains arg");

        return true;
    }

    if (str_equal(type_alias->ident, type_kind_str[TYPE_RAW_PTR])) {
        type->kind = TYPE_RAW_PTR;
        type->origin_ident = NULL;
        type->origin_type_kind = 0;

        ANALYZER_ASSERTF(type_alias->args && type_alias->args->length == 1, "raw_ptr<...> must contains arg type");

        type_t *arg_type = ct_list_value(type_alias->args, 0);
        analyzer_type(m, arg_type);

        type_raw_ptr_t *raw_ptr = NEW(type_raw_ptr_t);
        raw_ptr->value_type = *arg_type;
        type->value = raw_ptr;

        return true;
    }

    if (str_equal(type_alias->ident, type_kind_str[TYPE_PTR])) {
        type->kind = TYPE_PTR;
        type->origin_ident = NULL;
        type->origin_type_kind = 0;

        ANALYZER_ASSERTF(type_alias->args && type_alias->args->length == 1, "ptr<...> must contains arg type");

        type_t *arg_type = ct_list_value(type_alias->args, 0);
        analyzer_type(m, arg_type);

        type_ptr_t *ptr = NEW(type_ptr_t);
        ptr->value_type = *arg_type;
        type->value = ptr;

        return true;
    }

    if (str_equal(type_alias->ident, type_kind_str[TYPE_ALL_T])) {
        type->kind = TYPE_ALL_T;
        type->value = NULL;

        ANALYZER_ASSERTF(type_alias->args == NULL, "all_t cannot contains arg");
        return true;
    }

    if (str_equal(type_alias->ident, type_kind_str[TYPE_FN_T])) {
        type->kind = TYPE_FN_T;
        type->value = NULL;

        ANALYZER_ASSERTF(type_alias->args == NULL, "fn_t cannot contains arg");
        return true;
    }

    return false;
}

/**
 * code 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
local_ident_t *local_ident_new(module_t *m, symbol_type_t type, void *decl, string ident) {
    // unique ident
    string unique_ident = var_unique_ident(m, ident);

    local_ident_t *local = NEW(local_ident_t);
    local->ident = ident;
    local->unique_ident = unique_ident;
    local->depth = m->analyzer_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    slice_push(m->analyzer_current->locals, local);

    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl, true);

    return local;
}

/**
 * 块级作用域处理
 */
static void analyzer_begin_scope(module_t *m) {
    m->analyzer_current->scope_depth++;
}

static void analyzer_end_scope(module_t *m) {
    m->analyzer_current->scope_depth--;
    slice_t *locals = m->analyzer_current->locals;
    while (locals->count > 0) {
        int index = locals->count - 1;
        local_ident_t *local = locals->take[index];
        if (local->depth <= m->analyzer_current->scope_depth) {
            break;
        }

        if (local->is_capture && local->type != SYMBOL_FN) {
            symbol_t *s = symbol_table_get(local->unique_ident);
            assert(s);
            assert(s->type == SYMBOL_VAR);
            ast_var_decl_t *var_decl = s->ast_value;
            var_decl->be_capture = true;

            slice_push(m->analyzer_current->fndef->be_capture_locals, local);
        }

        // 从 locals 中移除该变量
        slice_remove(locals, index);
    }
}

static ast_stmt_t *auto_as_stmt(int line, char *subject_ident, type_t target_type) {
    // var x = x as T
    ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
    vardef->var_decl.ident = strdup(subject_ident);
    vardef->var_decl.type = type_copy(target_type);

    ast_expr_t expr = {
            .line = line,
            .column = 0,
            .assert_type = AST_EXPR_AS,
    };

    ast_as_expr_t *as_expr = NEW(ast_as_expr_t);
    as_expr->src = *ast_ident_expr(expr.line, expr.column, strdup(vardef->var_decl.ident));
    as_expr->target_type = type_copy(target_type);
    expr.value = as_expr;
    vardef->right = expr;

    ast_stmt_t *stmt = NEW(ast_stmt_t);
    stmt->line = line;
    stmt->column = 0;
    stmt->assert_type = AST_STMT_VARDEF;
    stmt->value = vardef;
    return stmt;
}

static ast_expr_t *extract_is_expr(module_t *m, ast_expr_t *expr) {
    if (expr->assert_type == AST_EXPR_IS && ((ast_is_expr_t *) expr->value)->src.assert_type == AST_EXPR_IDENT) {
        return expr;
    }

    if (expr->assert_type == AST_EXPR_BINARY && ((ast_binary_expr_t *) expr->value)->operator == AST_OP_AND_AND) {
        ast_binary_expr_t *binary_expr = expr->value;
        ast_expr_t *left = extract_is_expr(m, &binary_expr->left);
        ast_expr_t *right = extract_is_expr(m, &binary_expr->right);
        if (left && right) {
            ANALYZER_ASSERTF(false, "condition expr cannot contains multiple is expr");
        }
        return left ? left : right;
    }

    return NULL;
}

static void analyzer_if(module_t *m, ast_if_stmt_t *if_stmt) {
    ast_expr_t *is_expr = extract_is_expr(m, &if_stmt->condition);
    if (is_expr) {
        ast_is_expr_t *is_cond = is_expr->value;
        assert(is_cond->src.assert_type == AST_EXPR_IDENT);
        char *ident = ((ast_ident *) is_cond->src.value)->literal;
        type_t target_type = is_cond->target_type;
        ast_stmt_t *as_stmt = auto_as_stmt(is_expr->line, ident, target_type);
        slice_insert(if_stmt->consequent, 0, as_stmt);
    }

    // ident 唯一标识生成
    analyzer_expr(m, &if_stmt->condition);


    analyzer_begin_scope(m);
    analyzer_body(m, if_stmt->consequent);
    analyzer_end_scope(m);

    analyzer_begin_scope(m);
    analyzer_body(m, if_stmt->alternate);
    analyzer_end_scope(m);
}

static void analyzer_assign(module_t *m, ast_assign_stmt_t *assign) {
    analyzer_expr(m, &assign->left);
    analyzer_expr(m, &assign->right);
}

static void analyzer_throw(module_t *m, ast_throw_stmt_t *throw) {
    analyzer_expr(m, &throw->error);
}

static void analyzer_match(module_t *m, ast_match_t *match) {
    char *subject_ident = NULL;
    if (match->subject) {
        if (match->subject->assert_type == AST_EXPR_IDENT) {
            subject_ident = strdup(((ast_ident *) match->subject->value)->literal);
        }

        analyzer_expr(m, match->subject);
    }

    analyzer_begin_scope(m);
    SLICE_FOR(match->cases) {
        ast_match_case_t *match_case = SLICE_VALUE(match->cases);

        bool is_cond = false;
        for (int i = 0; i < match_case->cond_list->length; ++i) {
            ast_expr_t *cond_expr = ct_list_value(match_case->cond_list, i);

            if (cond_expr->assert_type == AST_EXPR_IDENT) {
                ast_ident *ident = cond_expr->value;
                if (str_equal(ident->literal, DEFAULT_IDENT)) {
                    // 'else' entry must be the last one in a 'when' expression.
                    // match 特殊形式处理
                    ANALYZER_ASSERTF(match_case->cond_list->length == 1,
                                     "default case '_' conflict in a 'match' expression");
                    ANALYZER_ASSERTF(_i == match->cases->count - 1,
                                     "default case '_' must be the last one in a 'match' expression");


                    match_case->is_default = true;
                    continue;
                }
            } else if (cond_expr->assert_type == AST_EXPR_MATCH_IS) {
                is_cond = true;
            }

            analyzer_expr(m, cond_expr);
        }

        if (match_case->cond_list->length > 1) {
            is_cond = false; // cond is logic, not is expr
        }

        if (is_cond && subject_ident) {
            // 添加断言 as 表达式到 handle body 中
            ast_expr_t *cond_expr = ct_list_value(match_case->cond_list, 0);
            assert(cond_expr->assert_type == AST_EXPR_MATCH_IS);
            ast_match_is_expr_t *is_cond_expr = cond_expr->value;
            slice_insert(match_case->handle_body, 0,
                         auto_as_stmt(cond_expr->line, subject_ident, is_cond_expr->target_type));
        }

        analyzer_begin_scope(m);
        analyzer_body(m, match_case->handle_body);
        analyzer_end_scope(m);
    }
    analyzer_end_scope(m);
}

static void analyzer_select(module_t *m, ast_select_stmt_t *select) {
    SLICE_FOR(select->cases) {
        ast_select_case_t *select_case = SLICE_VALUE(select->cases);
        if (select_case->on_call) {
            analyzer_call(m, select_case->on_call);
        }
        analyzer_begin_scope(m);
        if (select_case->recv_var) {
            analyzer_var_decl(m, select_case->recv_var, true);
        }
        analyzer_body(m, select_case->handle_body);
        analyzer_end_scope(m);
        if (select_case->is_default) {
            ANALYZER_ASSERTF(_i == select->cases->count - 1,
                             "default case '_' must be the last one in a 'select' expression");
        }
    }
}

/**
 * 函数的递归自调用分为两种类型，一种是包含名称的函数递归自调用, 一种是不包含名称的函数递归自调用。
 * 对于第一种形式， global ident 一定能够找到该 fn 进行调用。
 *
 * 现在是较为复杂的第二种情况(self 关键字现在已经被占用了，所以无法使用),
 * 也就是闭包 fn 的自调用。这
 * @param m
 * @param call
 */
static void analyzer_call(module_t *m, ast_call_t *call) {
    // 函数地址 unique 改写
    analyzer_expr(m, &call->left);

    if (call->generics_args) {
        for (int i = 0; i < call->generics_args->length; ++i) {
            type_t *arg = ct_list_value(call->generics_args, i);
            analyzer_type(m, arg);
        }
    }

    // 实参 unique 改写
    for (int i = 0; i < call->args->length; ++i) {
        ast_expr_t *arg = ct_list_value(call->args, i);
        analyzer_expr(m, arg);
    }
}

static void analyzer_async_expr(module_t *m, ast_macro_async_t *async) {
    analyzer_local_fndef(m, async->closure_fn);
    analyzer_local_fndef(m, async->closure_fn_void);

    // co closure 对 coder 不可见，为了让 throw 报错信息更加的清晰,直接继承 parent fn name
    async->closure_fn->fn_name_with_pkg = m->analyzer_current->fndef->fn_name_with_pkg;
    async->closure_fn_void->fn_name_with_pkg = m->analyzer_current->fndef->fn_name_with_pkg;
    analyzer_call(m, async->origin_call);

    if (async->flag_expr) {
        analyzer_expr(m, async->flag_expr);
    }
}

/**
 * 检查当前作用域及当前 scope 是否重复定义了 ident
 * @param ident
 * @return
 */
static bool analyzer_redeclare_check(module_t *m, char *ident) {
    if (m->analyzer_current->scope_depth == 0) {
        return true;
    }

    // 从内往外遍历
    slice_t *locals = m->analyzer_current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];

        // 如果找到了更高一级的作用域此时是允许重复定义变量的，直接跳过就行了
        if (local->depth != -1 && local->depth < m->analyzer_current->scope_depth) {
            break;
        }

        if (str_equal(local->ident, ident)) {
            ANALYZER_ASSERTF(false, "redeclare ident '%s'", ident);
        }
    }

    return true;
}

static void analyzer_expr_fake(module_t *m, ast_expr_fake_stmt_t *stmt) {
    analyzer_expr(m, &stmt->expr);
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param var_decl
 */
static void analyzer_var_decl(module_t *m, ast_var_decl_t *var_decl, bool redeclare_check) {
    if (redeclare_check) {
        analyzer_redeclare_check(m, var_decl->ident);
    }

    analyzer_type(m, &var_decl->type);

    // 这里进行了全局符号表的注册
    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, var_decl, var_decl->ident);

    // 改写
    var_decl->ident = local->unique_ident;
}

static void analyzer_vardef(module_t *m, ast_vardef_stmt_t *vardef) {
    analyzer_expr(m, &vardef->right);
    analyzer_redeclare_check(m, vardef->var_decl.ident);
    analyzer_type(m, &vardef->var_decl.type);

    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, &vardef->var_decl, vardef->var_decl.ident);
    vardef->var_decl.ident = local->unique_ident;
}

static void analyzer_var_tuple_destr_item(module_t *m, ast_expr_t *expr) {
    if (expr->assert_type == AST_VAR_DECL) {
        analyzer_var_decl(m, expr->value, true);
    } else if (expr->assert_type == AST_EXPR_TUPLE_DESTR) {
        analyzer_var_tuple_destr(m, expr->value);
    } else {
        ANALYZER_ASSERTF(false, "var tuple destr expr type exception");
    }
}

static void analyzer_var_tuple_destr(module_t *m, ast_tuple_destr_t *tuple_destr) {
    for (int i = 0; i < tuple_destr->elements->length; ++i) {
        ast_expr_t *item = ct_list_value(tuple_destr->elements, i);
        analyzer_var_tuple_destr_item(m, item);
    }
}

/**
 * var (a, b, (c, d)) = xxxx
 * @param m
 * @param stmt
 */
static void analyzer_var_tuple_destr_stmt(module_t *m, ast_var_tuple_def_stmt_t *stmt) {
    analyzer_expr(m, &stmt->right);

    analyzer_var_tuple_destr(m, stmt->tuple_destr);
}

static analyzer_fndef_t *analyzer_current_init(module_t *m, ast_fndef_t *fndef) {
    analyzer_fndef_t *new = NEW(analyzer_fndef_t);
    new->locals = slice_new();
    new->frees = slice_new();
    new->free_table = table_new();

    new->scope_depth = 0;

    fndef->capture_exprs = ct_list_new(sizeof(ast_expr_t));
    fndef->be_capture_locals = slice_new();
    new->fndef = fndef;

    // 继承关系
    new->parent = m->analyzer_current;
    m->analyzer_current = new;

    return m->analyzer_current;
}

/**
 * 全局 fn (main/module fn) 到 name 已经提前加入到了符号表中。不需要重新调整，主要是对 param 和 body 进行处理即可
 * @param m
 * @param fndef
 */
static void analyzer_global_fndef(module_t *m, ast_fndef_t *fndef) {
    analyzer_current_init(m, fndef);
    m->analyzer_global = fndef;
    fndef->is_local = false;
    m->current_line = fndef->line;
    m->current_column = fndef->column;

    // generics fn 需要在 pre_infer 后
    if (fndef->generics_params) {
        fndef->is_generics = true;
    }

    if (fndef->is_tpl) {
        assert(fndef->body == NULL);
    }

    analyzer_type(m, &fndef->return_type);

    analyzer_begin_scope(m);

    // 类型定位，在 analyzer 阶段, alias 类型会被添加上 module 生成新 ident
    // fn vec<T>.vec_len() -> fn vec_len(vec<T> self)
    if (fndef->impl_type.kind > 0) {
        // 更新 alias
        if (fndef->impl_type.kind == TYPE_ALIAS) {
            char *unique_alias_ident = analyzer_resolve_type_alias(m, NULL, fndef->impl_type.impl_ident);
            ANALYZER_ASSERTF(unique_alias_ident != NULL, "type alias '%s' undeclared \n", fndef->impl_type.impl_ident);
            fndef->impl_type.impl_ident = unique_alias_ident;
            fndef->impl_type.alias->ident = unique_alias_ident;
        }

        // 重构关于 param 的位置
        list_t *params = ct_list_new(sizeof(ast_var_decl_t));
        // param 中需要新增一个 impl_type_alias 的参数, 参数的名称为 self, 类型则是 impl_type
        type_t param_type = type_copy(fndef->impl_type);
        ast_var_decl_t param = {
                .ident = FN_SELF_NAME,
                .type = param_type, // TODO ptrof
        };
        ct_list_push(params, &param);

        for (int i = 0; i < fndef->params->length; ++i) {
            ast_var_decl_t *item = ct_list_value(fndef->params, i);
            ct_list_push(params, item);
        }
        fndef->params = params;
    }

    // 函数形参处理
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);

        // type 中引用了 typedef ident 到话,同样需要更新引用
        analyzer_type(m, &param->type);

        // 注册(param->ident 此时已经随 param->type 一起写入到了 symbol 中)
        local_ident_t *param_local = local_ident_new(m, SYMBOL_VAR, param, param->ident);

        // 将 ast 中到 param->ident 进行改写
        param->ident = param_local->unique_ident;
    }

    // tpl fn 没有 fn body
    if (fndef->body) {
        analyzer_body(m, fndef->body);
    }

    analyzer_end_scope(m);
}

static void analyzer_catch(module_t *m, ast_catch_t *catch_expr) {
    analyzer_expr(m, &catch_expr->try_expr);

    analyzer_begin_scope(m);
    analyzer_var_decl(m, &catch_expr->catch_err, true);
    analyzer_body(m, catch_expr->catch_body);
    analyzer_end_scope(m);
}

static void analyzer_as_expr(module_t *m, ast_as_expr_t *as_expr) {
    analyzer_type(m, &as_expr->target_type);
    analyzer_expr(m, &as_expr->src);
}

static void analyzer_sizeof_expr(module_t *m, ast_macro_sizeof_expr_t *sizeof_expr) {
    analyzer_type(m, &sizeof_expr->target_type);
}

static void analyzer_ula_expr(module_t *m, ast_macro_ula_expr_t *ula_expr) {
    analyzer_expr(m, &ula_expr->src);
}

static void analyzer_reflect_hash_expr(module_t *m, ast_macro_reflect_hash_expr_t *expr) {
    analyzer_type(m, &expr->target_type);
}

static void analyzer_type_eq_expr(module_t *m, ast_macro_type_eq_expr_t *expr) {
    analyzer_type(m, &expr->left_type);
    analyzer_type(m, &expr->right_type);
}

static void analyzer_is_expr(module_t *m, ast_is_expr_t *is_expr) {
    analyzer_type(m, &is_expr->target_type);
    analyzer_expr(m, &is_expr->src);
}

/**
 * 允许在同一作用域内重复定义, 直接将该表达式改写成 vardef 即可
 * @param m
 * @param expr
 */
static void analyzer_let(module_t *m, ast_stmt_t *stmt) {
    ast_let_t *let_stmt = stmt->value;

    ANALYZER_ASSERTF(let_stmt->expr.assert_type == AST_EXPR_AS, "variables must be used for 'as' in the expression");

    ast_as_expr_t *as_expr = let_stmt->expr.value;

    ANALYZER_ASSERTF(as_expr->src.assert_type == AST_EXPR_IDENT, "variables must be used for 'as' in the expression");
    ast_ident *ident = as_expr->src.value;
    char *old = strdup(ident->literal);

    analyzer_expr(m, &let_stmt->expr);

    // 表达式改写
    ast_vardef_stmt_t *vardef = NEW(ast_vardef_stmt_t);
    vardef->var_decl.ident = old;
    vardef->var_decl.type = as_expr->target_type;
    vardef->right = let_stmt->expr;

    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, &vardef->var_decl, vardef->var_decl.ident);
    vardef->var_decl.ident = local->unique_ident;

    // 表达式类型改写
    stmt->assert_type = AST_STMT_VARDEF;
    stmt->value = vardef;
}

/**
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 *
 * fn<void(int, int)> foo = void (int a, int b) {
 *
 * }
 *
 * 函数此时有两个名称,所以需要添加两次符号表
 * 其中 foo 属于 var[code == fn]
 * bar 属于 label
 * fn(int, int) foo = fn bar(int a, int b) {
 *
 * }
 * @param fndef
 * @return
 */
static void analyzer_local_fndef(module_t *m, ast_fndef_t *fndef) {
    assert(m->analyzer_global);
    slice_push(m->analyzer_global->local_children, fndef);
    fndef->global_parent = m->analyzer_global;
    fndef->is_local = true;

    // 闭包函数不能是类型扩展, 泛型
    if (fndef->impl_type.kind > 0 || fndef->generics_params) {
        ANALYZER_ASSERTF(false, "closure fn cannot be generics or impl type alias");
    }

    // 闭包函数不能有 macro ident
    if (fndef->linkid) {
        ANALYZER_ASSERTF(false, "closure fn cannot have #linkid label");
    }

    ANALYZER_ASSERTF(!fndef->is_tpl, "closure fn cannot be template");

    // 更新 m->analyzer_current
    analyzer_current_init(m, fndef);

    // 函数名称重复声明检测
    if (fndef->symbol_name == NULL) {
        fndef->symbol_name = ANONYMOUS_FN_NAME;
        fndef->fn_name = ANONYMOUS_FN_NAME;
        fndef->fn_name_with_pkg = ident_with_prefix(m->ident, ANONYMOUS_FN_NAME);
    } else {
        analyzer_redeclare_check(m, fndef->symbol_name);
    }

    // 当前 fn 内部作用域中注册当前 fn 的 symbol_name 主要用于递归调用, 由于不确定符号的具体类型，所以暂时不注册到符号表中
    string unique_ident = var_unique_ident(m, fndef->symbol_name);
    local_ident_t *fn_local = NEW(local_ident_t);
    fn_local->ident = fndef->symbol_name;
    fn_local->unique_ident = unique_ident;
    fn_local->depth = m->analyzer_current->scope_depth;
    fn_local->decl = fndef;
    fn_local->type = SYMBOL_FN;
    slice_push(m->analyzer_current->locals, fn_local);
    fndef->symbol_name = fn_local->unique_ident;

    analyzer_type(m, &fndef->return_type);

    // 开启一个新的 function 作用域
    analyzer_begin_scope(m);

    // 函数形参处理
    for (int i = 0; i < fndef->params->length; ++i) {
        ast_var_decl_t *param = ct_list_value(fndef->params, i);

        // type 中引用了 typedef ident 到话,同样需要更新引用
        analyzer_type(m, &param->type);

        // 注册(param->ident 此时已经随 param->type 一起写入到了 symbol 中)
        local_ident_t *param_local = local_ident_new(m, SYMBOL_VAR, param, param->ident);

        // 将 ast 中到 param->ident 进行改写
        param->ident = param_local->unique_ident;
    }

    // 分析请求体 block, 其中进行了对外部变量的捕获并改写, 以及 local var 名称 unique
    analyzer_body(m, fndef->body);

    int free_var_count = 0;

    // m->analyzer_current free > 0 表示当前函数引用了外部的环境，此时该函数将被编译成一个 closure
    for (int i = 0; i < m->analyzer_current->frees->count; ++i) {
        // free 中包含了当前环境引用对外部对环境变量.这是基于定义域而不是调用栈对
        // 如果想要访问到当前 fndef, 则一定已经经过了当前 fndef 的定义域
        free_ident_t *free_var = m->analyzer_current->frees->take[i];
        if (free_var->type != SYMBOL_VAR) {
            // fn label 可以直接全局访问到，不需要做 env assign
            continue;
        }
        free_var_count++;

        // 封装成 ast_expr 更利于 compiler
        ast_expr_t expr = {
                .line = fndef->line,
                .column = fndef->column,
        };

        // local 表示引用的 fn 是在 fndef->parent 的 local 变量，而不是自己的 local
        if (free_var->is_local) {
            // ast_ident 表达式
            expr.assert_type = AST_EXPR_IDENT;
            expr.value = ast_new_ident(free_var->ident);
        } else {
            // ast_env_index 表达式, 这里时 parent 再次引用了 parent env 到意思
            expr.assert_type = AST_EXPR_ENV_ACCESS;
            ast_env_access_t *env_access = NEW(ast_env_access_t);
            env_access->index = free_var->env_index;
            env_access->unique_ident = free_var->ident;
            expr.value = env_access;
        }

        ct_list_push(fndef->capture_exprs, &expr);
    }

    analyzer_end_scope(m);

    // - 退出当前 current
    m->analyzer_current = m->analyzer_current->parent;

    // 如果当前函数是顶层层函数，退出后 m->analyzer_current is null
    // 不过顶层函数也不存在 closure 引用的情况，直接注册到符号表中退出就行了
    if (free_var_count > 0) {
        assert(m->analyzer_current);

        fndef->jit_closure_name = fndef->symbol_name;
        fndef->symbol_name = var_ident_with_index(m, str_connect(fndef->symbol_name, "_closure")); // 二进制中的 label name

        // 符号表内容修改为 var_decl
        ast_var_decl_t *var_decl = NEW(ast_var_decl_t);
        var_decl->type = analyzer_type_fn(fndef);
        var_decl->ident = fndef->jit_closure_name;

        symbol_table_set(fndef->jit_closure_name, SYMBOL_VAR, var_decl, true);
        symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, true);

        // 原始名称不变, 可以被 analyzer_resolve 寻找到
        local_ident_t *parent_var_local = NEW(local_ident_t);
        parent_var_local->ident = fn_local->ident;
        parent_var_local->unique_ident = var_decl->ident;
        parent_var_local->depth = m->analyzer_current->scope_depth;
        parent_var_local->decl = var_decl;
        parent_var_local->type = SYMBOL_VAR;
        slice_push(m->analyzer_current->locals, parent_var_local);
    } else {
        // 符号就是普通的 symbol fn 符号，现在可以注册到符号表中了
        symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, true);

        if (m->analyzer_current) {
            // 仅加入到作用域，符号表就不需要重复添加了
            local_ident_t *parent_fn_local = NEW(local_ident_t);
            parent_fn_local->ident = fn_local->ident;
            parent_fn_local->unique_ident = fn_local->unique_ident;
            parent_fn_local->depth = m->analyzer_current->scope_depth;
            parent_fn_local->decl = fn_local->decl;
            parent_fn_local->type = SYMBOL_FN;
            slice_push(m->analyzer_current->locals, parent_fn_local);
        }
    }
}

/**
 * @param current
 * @param is_local
 * @param index
 * @param ident
 * @return
 */
static int analyzer_push_free(analyzer_fndef_t *current, bool is_local, int index, string ident, symbol_type_t type) {
    // if exists, return exists index
    free_ident_t *free = table_get(current->free_table, ident);
    if (free) {
        return free->index;
    }

    // 新增 free
    free = NEW(free_ident_t);
    free->is_local = is_local;
    free->env_index = index;
    free->ident = ident;
    free->type = type;
    int free_index = slice_push(current->frees, free);
    free->index = free_index;
    table_set(current->free_table, ident, free);

    return free_index;
}

/**
 * 返回 top scope ident index
 * @param current
 * @param ident
 * @return
 */
static int8_t analyzer_resolve_free(analyzer_fndef_t *current, char **ident, symbol_type_t *type) {
    if (current->parent == NULL) {
        return -1;
    }

    slice_t *locals = current->parent->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(*ident, local->ident) == 0) {
            local->is_capture = true;
            *ident = local->unique_ident;
            *type = local->type;

            // fn(没有被 closure) 不需要 push 到 free index 中，可以直接在全局符号表中找到相应的 fn
            if (local->type == SYMBOL_FN) {
                return -1;
            }
            return (int8_t) analyzer_push_free(current, true, i, *ident, *type);
        }
    }

    // 一级 parent 没有找到，则继续向上 parent 递归查询
    int8_t parent_free_index = analyzer_resolve_free(current->parent, ident, type);
    if (parent_free_index >= 0) {
        // 在更高级的某个 parent 中找到了符号，则在 current 中添加对逃逸变量的引用处理
        return (int8_t) analyzer_push_free(current, false, parent_free_index, *ident, *type);
    }

    return -1;
}

static bool analyzer_local_ident(module_t *m, ast_expr_t *expr) {
    ast_ident *temp = expr->value;
    // 避免如果存在两个位置引用了同一 ident 清空下造成同时改写两个地方的异常
    ast_ident *ident = ast_new_ident(temp->literal);
    expr->value = ident;


    // 当前 analyzer 位于 global analyzer, 可能是结构体的右侧值的 anazlyer。
    if (!m->analyzer_current) {
        return false;
    }

    // - 在当前函数作用域中查找变量定义(local 是有清理逻辑的，一旦离开作用域就会被清理, 所以这里不用担心使用了下一级的
    // local)
    slice_t *locals = m->analyzer_current->locals;
    for (int i = locals->count - 1; i >= 0; --i) {
        local_ident_t *local = locals->take[i];
        if (str_equal(local->ident, ident->literal)) {
            ident->literal = local->unique_ident;
            return true;
        }
    }


    // - 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env[free_var_index] 进行改写
    symbol_type_t type = SYMBOL_VAR;
    int8_t free_var_index = analyzer_resolve_free(m->analyzer_current, &ident->literal, &type);
    if (type == SYMBOL_FN) {
        return true;
    } else if (free_var_index >= 0) {
        // 如果使用的 ident 是逃逸的变量，则需要使用 access_env 代替
        // 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
        expr->assert_type = AST_EXPR_ENV_ACCESS;
        ast_env_access_t *env_access = NEW(ast_env_access_t);
        env_access->index = free_var_index;
        // 外部到 ident 可能已经修改了名字,这里进行冗于记录
        env_access->unique_ident = ident->literal;
        expr->value = env_access;
        return true;
    }

    return false;
}

static bool analyzer_as_star_or_builtin_ident(module_t *m, ast_ident *ident) {
    // - import xxx as * 产生的全局符号
    for (int i = 0; i < m->imports->count; ++i) {
        ast_import_t *import = m->imports->take[i];

        if (str_equal(import->as, "*")) {
            char *temp = ident_with_prefix(import->module_ident, ident->literal);
            if (table_exist(can_import_symbol_table, temp)) {
                ident->literal = temp;
                return true;
            }
        }
    }

    // - builtin 产生的全局符号
    symbol_t *s = table_get(symbol_table, ident->literal);
    if (s != NULL) {
        // builtin global Symbol does not require symbol rewriting
        return true;
    }

    return false;
}

/**
 *
 * @param m
 * @param expr
 */
static bool analyzer_ident(module_t *m, ast_expr_t *expr) {
    bool local_analyzer = analyzer_local_ident(m, expr);
    if (local_analyzer) {
        return true;
    }

    // analyzer_local_ident The ident rebuild has already been done, it is good to use it here
    // 避免如果存在两个位置引用了同一 ident 清空下造成同时改写两个地方的异常
    ast_ident *ident = expr->value;

    // - 使用当前 module 中的全局符号是可以省略 module name 的, 但是 module ident 注册时 附加了 module.ident
    // 所以需要为 ident 添加上全局访问符号再看看能不能找到该 ident
    char *temp = ident_with_prefix(m->ident, ident->literal);
    symbol_t *s = table_get(symbol_table, temp);
    if (s != NULL) {
        ident->literal = temp; // 找到了则修改为全局名称
        return true;
    }

    if (analyzer_as_star_or_builtin_ident(m, ident)) {
        return true;
    }

    return false;
}

static void analyzer_access(module_t *m, ast_access_t *access) {
    analyzer_expr(m, &access->left);
    analyzer_expr(m, &access->key);
}

/*
 * 如果是 package.test 则进行符号改写, 改写成 namespace + module_name
 * struct.key , instance? 则不做任何对处理。
 */
static void analyzer_select_expr(module_t *m, ast_expr_t *expr) {
    ast_expr_select_t *select = expr->value;

    // import select 特殊处理, 直接进行符号改写
    if (select->left.assert_type == AST_EXPR_IDENT) {
        // 检测 ident 是否是 local ident, local ident 说明这是一个 local struct
        bool local_analyzer = analyzer_local_ident(m, &select->left);
        if (local_analyzer) {
            return;
        }

        ast_ident *ident = select->left.value;

        char *current_module_ident = ident_with_prefix(m->ident, ident->literal);
        symbol_t *s = table_get(symbol_table, current_module_ident);
        if (s != NULL) {
            ident->literal = current_module_ident; // 找到了则修改为全局名称
            return;
        }

        // import ident
        ast_import_t *import = table_get(m->import_table, ident->literal);
        if (import) {
            // 这里直接将 module.select 改成了全局唯一名称，彻底消灭了select ！
            // (不需要检测 import package 是否存在，这在 linker 中会做的)
            char *unique_ident = ident_with_prefix(import->module_ident, select->key);

            // 检测 import ident 是否存在
            if (!table_exist(can_import_symbol_table, unique_ident)) {
                ANALYZER_ASSERTF(false, "identifier '%s' undeclared \n", unique_ident);
            }

            expr->assert_type = AST_EXPR_IDENT;
            expr->value = ast_new_ident(unique_ident);
            return;
        }

        if (analyzer_as_star_or_builtin_ident(m, ident)) {
            return;
        }


        ANALYZER_ASSERTF(false, "identifier '%s' undeclared \n", ident->literal);
    }

    // foo['car'].bar analyzer ident 会再次处理 left
    // foo.car.bar.dog
    analyzer_expr(m, &select->left);
}

static void analyzer_binary(module_t *m, ast_binary_expr_t *expr) {
    analyzer_expr(m, &expr->left);
    analyzer_expr(m, &expr->right);
}

static void analyzer_unary(module_t *m, ast_unary_expr_t *expr) {
    analyzer_expr(m, &expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
static void analyzer_struct_new(module_t *m, ast_struct_new_t *expr) {
    analyzer_type(m, &expr->type);

    for (int i = 0; i < expr->properties->length; ++i) {
        struct_property_t *property = ct_list_value(expr->properties, i);
        analyzer_expr(m, property->right);
    }
}

static void analyzer_map_new(module_t *m, ast_map_new_t *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_map_element_t *element = ct_list_value(expr->elements, i);
        analyzer_expr(m, &element->key);
        analyzer_expr(m, &element->value);
    }
}

static void analyzer_set_new(module_t *m, ast_set_new_t *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr_t *key = ct_list_value(expr->elements, i);
        analyzer_expr(m, key);
    }
}

static void analyzer_tuple_new(module_t *m, ast_tuple_new_t *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr_t *element = ct_list_value(expr->elements, i);
        analyzer_expr(m, element);
    }
}

/**
 * tuple_destr = tuple_new
 * (a.b, a, (c, c.d)) = (1, 2)
 * @param m
 * @param expr
 */
static void analyzer_tuple_destr(module_t *m, ast_tuple_destr_t *tuple) {
    for (int i = 0; i < tuple->elements->length; ++i) {
        ast_expr_t *element = ct_list_value(tuple->elements, i);
        analyzer_expr(m, element);
    }
}

static void analyzer_vec_new(module_t *m, ast_vec_new_t *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr_t *value = ct_list_value(expr->elements, i);
        analyzer_expr(m, value);
    }
}

static void analyzer_new_expr(module_t *m, ast_new_expr_t *expr) {
    analyzer_type(m, &expr->type);

    for (int i = 0; i < expr->properties->length; ++i) {
        struct_property_t *property = ct_list_value(expr->properties, i);
        analyzer_expr(m, property->right);
    }
}

static void analyzer_for_cond(module_t *m, ast_for_cond_stmt_t *stmt) {
    analyzer_expr(m, &stmt->condition);

    analyzer_begin_scope(m);
    analyzer_body(m, stmt->body);
    analyzer_end_scope(m);
}

static void analyzer_for_tradition(module_t *m, ast_for_tradition_stmt_t *stmt) {
    analyzer_begin_scope(m);
    analyzer_stmt(m, stmt->init);
    analyzer_expr(m, &stmt->cond);
    analyzer_stmt(m, stmt->update);
    analyzer_body(m, stmt->body);
    analyzer_end_scope(m);
}

static void analyzer_for_iterator(module_t *m, ast_for_iterator_stmt_t *stmt) {
    // iterate 是对变量的使用，所以其在 scope
    analyzer_expr(m, &stmt->iterate);

    analyzer_begin_scope(m); // iterator 中创建的 key 和 value 的所在作用域都应该在当前 for scope 里面

    analyzer_var_decl(m, &stmt->first, true);
    if (stmt->second) {
        analyzer_var_decl(m, stmt->second, true);
    }
    analyzer_body(m, stmt->body);

    analyzer_end_scope(m);
}

static void analyzer_break(module_t *m, ast_break_t *stmt) {
    if (stmt->expr != NULL) {
        analyzer_expr(m, stmt->expr);
    }
}

static void analyzer_return(module_t *m, ast_return_stmt_t *stmt) {
    if (stmt->expr != NULL) {
        analyzer_expr(m, stmt->expr);
    }
}

// type foo = int
static void analyzer_type_alias_stmt(module_t *m, ast_type_alias_stmt_t *stmt) {
    // local type alias 不允许携带 param
    if (stmt->params && stmt->params->length > 0) {
        ANALYZER_ASSERTF(false, "local type alias cannot with params");
    }

    analyzer_redeclare_check(m, stmt->ident);
    analyzer_type(m, &stmt->type_expr);

    local_ident_t *local = local_ident_new(m, SYMBOL_TYPE_ALIAS, stmt, stmt->ident);
    stmt->ident = local->unique_ident;
}

static void analyzer_expr(module_t *m, ast_expr_t *expr) {
    m->current_line = expr->line;
    m->current_column = expr->column;

    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            return analyzer_binary(m, expr->value);
        }
        case AST_EXPR_UNARY: {
            return analyzer_unary(m, expr->value);
        }
        case AST_CATCH: {
            return analyzer_catch(m, expr->value);
        }
        case AST_EXPR_AS: {
            return analyzer_as_expr(m, expr->value);
        }
        case AST_EXPR_IS: {
            return analyzer_is_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_SIZEOF: {
            return analyzer_sizeof_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_ULA: {
            return analyzer_ula_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_REFLECT_HASH: {
            return analyzer_reflect_hash_expr(m, expr->value);
        }
        case AST_MACRO_EXPR_TYPE_EQ: {
            return analyzer_type_eq_expr(m, expr->value);
        }
        case AST_EXPR_NEW: {
            return analyzer_new_expr(m, expr->value);
        }
        case AST_EXPR_STRUCT_NEW: {
            return analyzer_struct_new(m, expr->value);
        }
        case AST_EXPR_MAP_NEW: {
            return analyzer_map_new(m, expr->value);
        }
        case AST_EXPR_SET_NEW: {
            return analyzer_set_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_NEW: {
            return analyzer_tuple_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_DESTR: {
            return analyzer_tuple_destr(m, expr->value);
        }
        case AST_EXPR_VEC_NEW: {
            return analyzer_vec_new(m, expr->value);
        }
        case AST_EXPR_ACCESS: {
            return analyzer_access(m, expr->value);
        }
        case AST_EXPR_SELECT: {
            // analyzer 仅进行了变量重命名
            // 此时作用域不明确，无法进行任何的表达式改写。
            return analyzer_select_expr(m, expr);
        }
        case AST_EXPR_IDENT: {
            // ident unique 改写并注册到符号表中
            bool result = analyzer_ident(m, expr);
            if (!result) {
                ANALYZER_ASSERTF(false, "identifier '%s' undeclared \n", ((ast_ident *) expr->value)->literal);
            }
            return;
        }
        case AST_MATCH: {
            return analyzer_match(m, expr->value);
        }
        case AST_CALL: {
            return analyzer_call(m, expr->value);
        }
        case AST_MACRO_ASYNC: {
            return analyzer_async_expr(m, expr->value);
        }
        case AST_FNDEF: {
            assertf(!m->analyzer_has_fndef, "");
            return analyzer_local_fndef(m, expr->value);
        }
        default:
            return;
    }
}

/**
 * 包含在 fndef body 中等各种表达式
 * @param m
 * @param stmt
 */
static void analyzer_stmt(module_t *m, ast_stmt_t *stmt) {
    m->current_line = stmt->line;
    m->current_column = stmt->column;

    switch (stmt->assert_type) {
        case AST_STMT_EXPR_FAKE: {
            return analyzer_expr_fake(m, stmt->value);
        }
        case AST_VAR_DECL: {
            return analyzer_var_decl(m, stmt->value, true);
        }
        case AST_STMT_LET: {
            return analyzer_let(m, stmt);
        }
        case AST_STMT_VARDEF: {
            return analyzer_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return analyzer_var_tuple_destr_stmt(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return analyzer_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            return analyzer_local_fndef(m, stmt->value);
        }
        case AST_CALL: {
            return analyzer_call(m, stmt->value);
        }
        case AST_CATCH: {
            return analyzer_catch(m, stmt->value);
        }
        case AST_STMT_SELECT: {
            return analyzer_select(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return analyzer_throw(m, stmt->value);
        }
        case AST_STMT_IF: {
            return analyzer_if(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return analyzer_for_cond(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return analyzer_for_tradition(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return analyzer_for_iterator(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return analyzer_return(m, stmt->value);
        }
        case AST_STMT_BREAK: {
            return analyzer_break(m, stmt->value);
        }
        case AST_STMT_TYPE_ALIAS: {
            return analyzer_type_alias_stmt(m, stmt->value);
        }
        default: {
            return;
        }
    }
}

/**
 * - 模块中的函数都是全局函数，将会在全局函数维度支持泛型函数与函数重载，由于在 analyzer 阶段还在收集所有的符号
 *   所以无法确定全局的 unique ident，因此将会用一个链表结构，将所有的在当前作用域下的同名的函数都 append 进去
 * @param m
 * @param stmt_list
 */
static void analyzer_module(module_t *m, slice_t *stmt_list) {
    // var_decl blocks
    slice_t *var_assign_list = slice_new(); // 存放 stmt
    slice_t *fn_list = slice_new();

    // 跳过 import 语句开始计算, 不直接使用 analyzer stmt, 因为 module 中不需要这么多表达式
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt_t *stmt = stmt_list->take[i];

        if (stmt->assert_type == AST_STMT_IMPORT) {
            continue;
        }

        if (stmt->assert_type == AST_STMT_VARDEF) {
            ast_vardef_stmt_t *vardef = stmt->value;
            ast_var_decl_t *var_decl = &vardef->var_decl;
            analyzer_type(m, &var_decl->type);
            var_decl->ident = ident_with_prefix(m->ident, var_decl->ident);
            slice_push(m->global_vardef, vardef);
            symbol_t *s = symbol_table_set(var_decl->ident, SYMBOL_VAR, var_decl, false);
            ANALYZER_ASSERTF(s, "var '%s' redeclared", var_decl->ident);
            slice_push(m->global_symbols, s);

            // 将 vardef 转换成 assign stmt，然后导入到 fn init 中进行初始化
            ast_stmt_t *assign_stmt = NEW(ast_stmt_t);
            ast_assign_stmt_t *assign = NEW(ast_assign_stmt_t);
            assign->left = (ast_expr_t) {
                    .line = stmt->line,
                    .column = stmt->column,
                    .assert_type = AST_EXPR_IDENT,
                    .value = ast_new_ident(var_decl->ident),
            };
            assign->right = vardef->right;
            assign_stmt->line = stmt->line;
            assign_stmt->column = stmt->column;
            assign_stmt->assert_type = AST_STMT_ASSIGN;
            assign_stmt->value = assign;
            slice_push(var_assign_list, assign_stmt);
            continue;
        }

        if (stmt->assert_type == AST_STMT_TYPE_ALIAS) {
            ast_type_alias_stmt_t *type_alias = stmt->value;
            assert(type_alias->type_expr.kind > 0);

            type_alias->ident = ident_with_prefix(m->ident, type_alias->ident);
            symbol_t *s = symbol_table_set(type_alias->ident, SYMBOL_TYPE_ALIAS, type_alias, false);
            ANALYZER_ASSERTF(s, "type alias '%s' redeclared", type_alias->ident);
            slice_push(m->global_symbols, s);

            if (type_alias->params && type_alias->params->length > 0) {
                for (int j = 0; j < type_alias->params->length; ++j) {
                    ast_generics_param_t *param = ct_list_value(type_alias->params, j);
                    if (!param->constraints.any) {
                        for (int k = 0; k < param->constraints.elements->length; ++k) {
                            type_t *constraint = ct_list_value(param->constraints.elements, k);
                            analyzer_type(m, constraint);
                        }
                    }
                }
            }

            analyzer_type(m, &type_alias->type_expr);
            continue;
        }

        if (stmt->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = stmt->value;

            char *symbol_name = fndef->symbol_name;

            // fn string<T>.len() -> fn <T>.string_len to symbol_table
            if (fndef->impl_type.kind > 0) {
                assert(fndef->impl_type.impl_ident);
                symbol_name = str_connect_by(fndef->impl_type.impl_ident, symbol_name, "_");
            }

            if (is_impl_builtin_type(fndef->impl_type.kind)) {
                fndef->symbol_name = symbol_name; // built in type impl 不需要添加 module ident
            } else {
                fndef->symbol_name = ident_with_prefix(m->ident, symbol_name); // 全局函数改名
            }
            slice_push(fn_list, fndef);

            // 泛型允许重载，所以此处会有同名 symbol 被覆盖写入, 在 pre_infer 对泛型进行展开后再次覆盖注册到 symbol
            symbol_t *s = symbol_table_set(fndef->symbol_name, SYMBOL_FN, fndef, false);
            if (!fndef->generics_params) {
                ANALYZER_ASSERTF(s, "fn '%s' redeclared", fndef->fn_name);
            }

            continue;
        }

        ANALYZER_ASSERTF(false, "non-declaration statement outside fn body")
    }

    // 添加 module init 函数
    if (var_assign_list->count > 0) {
        ast_fndef_t *fn_init = ast_fndef_new(m, 0, 0);
        fn_init->symbol_name = ident_with_prefix(analyzer_force_unique_ident(m), FN_INIT_NAME);
        fn_init->fn_name = fn_init->symbol_name;
        fn_init->fn_name_with_pkg = ident_with_prefix(m->ident, fn_init->symbol_name);
        fn_init->return_type = type_kind_new(TYPE_VOID);
        fn_init->params = ct_list_new(sizeof(ast_var_decl_t));
        fn_init->body = var_assign_list;

        // 讲 module init 函数添加到全局符号中
        symbol_t *s = symbol_table_set(fn_init->symbol_name, SYMBOL_FN, fn_init, false);
        ANALYZER_ASSERTF(s, "fn '%s' redeclared", fn_init->symbol_name);
        slice_push(m->global_symbols, s);
        slice_push(fn_list, fn_init);

        // 添加调用指令(后续 root module 会将这条指令添加到 main body 中)
        ast_stmt_t *call_stmt = NEW(ast_stmt_t);
        ast_call_t *call = NEW(ast_call_t);
        call->left = (ast_expr_t) {
                .assert_type = AST_EXPR_IDENT,
                .value = ast_new_ident(s->ident), // module.init
                .line = 1,
                .column = 0,
        };
        call->args = ct_list_new(sizeof(ast_expr_t));
        call_stmt->assert_type = AST_CALL;
        call_stmt->value = call;
        call_stmt->line = 1;
        call_stmt->column = 0;
        m->call_init_stmt = call_stmt;
    }

    // 此时所有对符号都已经主要到了全局变量表中，vardef 的右值则注册到了 fn.init 中，下面对 fndef body
    // 进行符号定位与改写
    for (int i = 0; i < fn_list->count; ++i) {
        ast_fndef_t *fndef = fn_list->take[i];

        // m->ast_fndefs 包含了当前 module 中的所有函数，嵌套定义的函数都进行了平铺
        slice_push(m->ast_fndefs, fndef);

        analyzer_global_fndef(m, fndef);
    }
}

static void analyzer_main(module_t *m) {
    ast_fndef_t *main_fn = NULL;
    // must have main fn
    SLICE_FOR(m->ast_fndefs) {
        ast_fndef_t *fn = SLICE_VALUE(m->ast_fndefs);
        if (!fn->is_local && str_equal(fn->fn_name, FN_MAIN_NAME)) {
            main_fn = fn;
            break;
        }
    }

    // 将 module init 添加到 fn main 的最前端进行初始化

    ANALYZER_ASSERTF(main_fn, "fn 'main' is undeclared in the main package");

    // func main must have no arguments and no return values
    ANALYZER_ASSERTF(main_fn->params->length == 0,
                     "fn main must have no arguments and no return values, example: fn main() {...");

    ANALYZER_ASSERTF(main_fn->return_type.kind == TYPE_VOID,
                     "fn main must have no arguments and no return values, example: fn main() {...");
}

void analyzer(module_t *m, slice_t *stmt_list) {
    m->current_line = 0;
    m->current_column = 0;

    analyzer_module(m, stmt_list);

    if (m->type == MODULE_TYPE_MAIN) {
        analyzer_main(m);
    }
}
