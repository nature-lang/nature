#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "utils/helper.h"
#include "analysis.h"
#include "utils/error.h"
#include "utils/table.h"
#include "utils/slice.h"
#include "src/symbol.h"
#include "src/debug/debug.h"

void analysis_block(module_t *m, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
        // switch 结构导向优化
#ifdef DEBUG_ANALYSIS
        debug_stmt("ANALYSIS", block->tack[i]);
#endif
        analysis_stmt(m, block->take[i]);
    }
}

void analysis_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->type) {
        case AST_VAR_DECL: {
            analysis_var_decl(m, (ast_var_decl *) stmt->stmt);
            break;
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            analysis_var_decl_assign(m, (ast_var_decl_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_ASSIGN: {
            analysis_assign(m, (ast_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_NEW_FN: {
            // 注册 function_decl_type + ident
            analysis_function_decl_ident(m, (ast_new_fn *) stmt->stmt);

            // 函数体添加到 延迟处理
            uint8_t count = m->analysis_current->contains_fn_count++;
            m->analysis_current->contains_fn_decl[count].stmt = stmt;
            m->analysis_current->contains_fn_decl[count].is_stmt = true;
            break;
        }
        case AST_CALL: {
            analysis_call(m, (ast_call *) stmt->stmt);
            break;
        }
        case AST_STMT_IF: {
            analysis_if(m, (ast_if_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_WHILE: {
            analysis_while(m, (ast_while_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_FOR_IN: {
            analysis_for_in(m, (ast_for_in_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_RETURN: {
            analysis_return(m, (ast_return_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_TYPE_DECL: {
            analysis_type_decl(m, (ast_type_decl_stmt *) stmt->stmt);
            break;
        }
        default:
            return;
    }
}

void analysis_if(module_t *m, ast_if_stmt *if_stmt) {
    analysis_expr(m, &if_stmt->condition);

    analysis_begin_scope(m);
    analysis_block(m, if_stmt->consequent);
    analysis_end_scope(m);

    analysis_begin_scope(m);
    analysis_block(m, if_stmt->alternate);
    analysis_end_scope(m);
}

void analysis_assign(module_t *m, ast_assign_stmt *assign) {
    analysis_expr(m, &assign->left);
    analysis_expr(m, &assign->right);
}

void analysis_call(module_t *m, ast_call *call) {
    // 函数地址改写
    analysis_expr(m, &call->left);

    // 实参改写
    for (int i = 0; i < call->actual_param_count; ++i) {
        ast_expr *actual_param = &call->actual_params[i];
        analysis_expr(m, actual_param);
    }
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param stmt
 */
void analysis_var_decl(module_t *m, ast_var_decl *stmt) {
    analysis_redeclare_check(m, stmt->ident);

    analysis_type(m, &stmt->type);

    analysis_local_ident *local = analysis_new_local(m, SYMBOL_TYPE_VAR, stmt, stmt->ident);

    // 改写
    stmt->ident = local->unique_ident;
}

void analysis_var_decl_assign(module_t *m, ast_var_decl_assign_stmt *stmt) {
    analysis_redeclare_check(m, stmt->var_decl->ident);

    analysis_type(m, &stmt->var_decl->type);

    analysis_local_ident *local = analysis_new_local(m, SYMBOL_TYPE_VAR, stmt->var_decl, stmt->var_decl->ident);
    stmt->var_decl->ident = local->unique_ident;

    analysis_expr(m, &stmt->expr);
}

type_t analysis_function_to_type(ast_new_fn *function_decl) {
    type_fn_t *fn_type = NEW(type_fn_t);
    fn_type->return_type = function_decl->return_type;
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        fn_type->formal_param_types[i] = function_decl->formal_params[i]->type;
    }
    fn_type->formal_param_count = function_decl->formal_param_count;
    type_t type = {
            .is_origin = false,
            .base = TYPE_FN,
            .value = fn_type
    };
    return type;
}

void analysis_function_decl_ident(module_t *m, ast_new_fn *new_fn) {
    // 仅 fun 再次定义 as 才需要再次添加到符号表
    if (!str_equal(new_fn->name, MAIN_FN_NAME)) {
        if (strlen(new_fn->name) == 0) {
            // 如果没有函数名称，则添加匿名函数名称
            new_fn->name = analysis_unique_ident(ANONYMOUS_FN_NAME);
        }

        analysis_redeclare_check(m, new_fn->name);

        // 函数名称改写
        analysis_local_ident *local = analysis_new_local(
                m,
                SYMBOL_TYPE_FN,
                new_fn,
                new_fn->name);

        new_fn->name = local->unique_ident;
    }
}

/**
 * wwh
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 *
 * fn<void(int, int)> foo = void (int a, int b) {
 *
 * }
 *
 * 函数此时有两个名称,所以需要添加两次符号表
 * 其中 foo 属于 var[type == fn]
 * bar 属于 label
 * fn<void(int, int)> foo = void bar(int a, int b) {
 *
 * }
 * @param function_decl
 * @return
 */
ast_closure *analysis_new_fn(module_t *m, ast_new_fn *function_decl, analysis_local_scope *scope) {
    ast_closure *closure = malloc(sizeof(ast_closure));
    analysis_type(m, &function_decl->return_type);

    // 初始化
    analysis_current_init(m, scope, function_decl->name);
    // 开启一个新的 function 作用域
    analysis_function_begin(m);

    // 函数形参处理
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        ast_var_decl *param = function_decl->formal_params[i];
        // 注册
        analysis_local_ident *param_local = analysis_new_local(m, SYMBOL_TYPE_VAR, param, param->ident);
        // 改写
        param->ident = param_local->unique_ident;

        analysis_type(m, &param->type);
    }

    // 分析请求体 block, 其中进行了自由变量的捕获/改写和局部变量改写
    analysis_block(m, function_decl->body);

    // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
    // free 是在外部环境构建 env 的。
    // current_function->env_unique_name = unique_var_ident(ENV_IDENT);
    closure->env_name = m->analysis_current->env_unique_name;

    // 构造 env
    for (int i = 0; i < m->analysis_current->free_count; ++i) {
        analysis_free_ident free_var = m->analysis_current->frees[i];
        ast_expr *expr = &closure->env[i];

        // 逃逸变量就在当前作用域中
        if (free_var.is_local) {
            // ast_ident 表达式
            expr->assert_type = AST_EXPR_IDENT;
            expr->expr = ast_new_ident(free_var.ident);
        } else {
            // ast_env_index 表达式
            expr->assert_type = AST_EXPR_ACCESS_ENV;
            ast_access_env *access_env = malloc(sizeof(ast_access_env));
            access_env->env = ast_new_ident(m->analysis_current->parent->env_unique_name);
            access_env->index = free_var.index;
            access_env->unique_ident = free_var.ident;
            expr->expr = access_env;
        }
    }
    closure->env_count = m->analysis_current->free_count;
    closure->function = function_decl;

    // 延迟处理 contains_function_decl
    for (int i = 0; i < m->analysis_current->contains_fn_count; ++i) {
        if (m->analysis_current->contains_fn_decl[i].is_stmt) {
            ast_stmt *stmt = m->analysis_current->contains_fn_decl[i].stmt;
            // 函数注册到符号表已经在函数定义点注册过了
            ast_closure *closure_decl = analysis_new_fn(m, stmt->stmt,
                                                        m->analysis_current->contains_fn_decl[i].scope);
            stmt->type = AST_NEW_CLOSURE;
            stmt->stmt = closure_decl;
        } else {
            ast_expr *expr = m->analysis_current->contains_fn_decl[i].expr;
            ast_closure *closure_decl = analysis_new_fn(m, expr->expr,
                                                        m->analysis_current->contains_fn_decl[i].scope);
            expr->assert_type = AST_NEW_CLOSURE;
            expr->expr = closure_decl;
        };
    }

    analysis_function_end(m); // 退出当前 current
    return closure;
}

/**
 * 块级作用域处理
 */
void analysis_begin_scope(module_t *m) {
    m->analysis_current->scope_depth++;
    analysis_local_scope *current_scope = m->analysis_current->current_scope;
    m->analysis_current->current_scope = analysis_new_local_scope(m->analysis_current->scope_depth, current_scope);
}

void analysis_end_scope(module_t *m) {
    analysis_local_scope *current_scope = m->analysis_current->current_scope;
    m->analysis_current->current_scope = current_scope->parent;
    m->analysis_current->scope_depth--;
}

/**
 * type 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
analysis_local_ident *analysis_new_local(module_t *m, symbol_type type, void *decl, string ident) {
    // unique ident
    string unique_ident = analysis_unique_ident(ident);

    analysis_local_ident *local = malloc(sizeof(analysis_local_ident));
    local->ident = ident;
    local->unique_ident = unique_ident;
    local->scope_depth = m->analysis_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    analysis_local_scope *current_scope = m->analysis_current->current_scope;
    current_scope->idents[current_scope->count++] = local;

    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl, true);

    return local;
}

void analysis_expr(module_t *m, ast_expr *expr) {
    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            analysis_binary(m, (ast_binary_expr *) expr->expr);
            break;
        };
        case AST_EXPR_UNARY: {
            analysis_unary(m, (ast_unary_expr *) expr->expr);
            break;
        };
        case AST_EXPR_NEW_STRUCT: {
            analysis_new_struct(m, (ast_new_struct *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_MAP: {
            analysis_new_map(m, (ast_new_map *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_ARRAY: {
            analysis_new_list(m, (ast_new_list *) expr->expr);
            break;
        }
        case AST_EXPR_ACCESS: {
            analysis_access(m, (ast_access *) expr->expr);
            break;
        };
        case AST_EXPR_SELECT_PROPERTY: {
            // analysis 仅进行了变量重命名，此时作用域不明确，无法进行任何的表达式改写。
            analysis_select_property(m, expr);
            break;
        };
        case AST_EXPR_IDENT: {
            // 核心改写逻辑
            analysis_ident(m, expr);
            break;
        };
        case AST_CALL: {
            analysis_call(m, (ast_call *) expr->expr);
            break;
        }
        case AST_NEW_FN: { // 右值
            analysis_function_decl_ident(m, (ast_new_fn *) expr->expr);

            // 函数体添加到 延迟处理
            uint8_t count = m->analysis_current->contains_fn_count++;
            m->analysis_current->contains_fn_decl[count].expr = expr;
            m->analysis_current->contains_fn_decl[count].is_stmt = false;
            m->analysis_current->contains_fn_decl[count].scope = m->analysis_current->current_scope;
            break;
        }
        default:
            return;
    }
}

/**
 * @param expr
 */
void analysis_ident(module_t *m, ast_expr *expr) {
    ast_ident *ident = expr->expr;

    if (is_print_symbol(ident->literal)) {
        return;
    }

    // 在当前函数作用域中查找变量定义
    analysis_local_scope *current_scope = m->analysis_current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->count; ++i) {
            analysis_local_ident *local = current_scope->idents[i];
            if (strcmp(ident->literal, local->ident) == 0) {
                // 在本地变量中找到,则进行简单改写 (从而可以在符号表中有唯一名称,方便定位)
                expr->expr = ast_new_ident(local->unique_ident);
                return;
            }
        }
        current_scope = current_scope->parent;
    }

    // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
    int8_t free_var_index = analysis_resolve_free(m->analysis_current, &ident->literal);
    if (free_var_index != -1) {
        // 外部作用域变量改写, 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
        expr->assert_type = AST_EXPR_ACCESS_ENV;
        ast_access_env *env_index = malloc(sizeof(ast_access_env));
        env_index->env = ast_new_ident(m->analysis_current->env_unique_name);
        env_index->index = free_var_index;
        env_index->unique_ident = ident->literal;
        expr->expr = env_index;
        return;
    }

    // with path 进行查找
    char *global_ident = ident_with_module_unique_name(m->module_unique_name, ident->literal);
    symbol_t *s = table_get(symbol_table, global_ident);
    if (s != NULL) {
        ident->literal = global_ident;
        return;
    }

    error_ident_not_found(expr->line, ident->literal);
    exit(0);

}

/**
 * 返回 top scope ident index
 * @param current
 * @param ident
 * @return
 */
int8_t analysis_resolve_free(analysis_function *current, string*ident) {
    if (current->parent == NULL) {
        return -1;
    }

    analysis_local_scope *scope = current->parent->current_scope;
    for (int i = 0; i < scope->count; ++i) {
        analysis_local_ident *local = scope->idents[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(*ident, local->ident) == 0) {
            scope->idents[i]->is_capture = true; // 被下级作用域引用
            *ident = local->unique_ident;
            return (int8_t) analysis_push_free(current, true, (int8_t) i, *ident);
        }
    }

    // 继续向上递归查询
    int8_t parent_free_index = analysis_resolve_free(current->parent, ident);
    if (parent_free_index != -1) {
        return (int8_t) analysis_push_free(current, false, parent_free_index, *ident);
    }

    return -1;
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。直接定位唯一名称即可
 * @param type
 */
void analysis_type(module_t *m, type_t *type) {
    // 如果只是简单的 ident,又应该如何改写呢？
    // TODO 如果出现死循环，应该告警退出
    // type foo = int
    // 'foo' is type_decl_ident
    if (type->base == TYPE_DECL_IDENT) {
        // 向上查查查
        ast_ident *ident = type->value;
        string unique_name = analysis_resolve_type(m, m->analysis_current, ident->literal);
        ident->literal = unique_name;
        return;
    }

    if (type->base == TYPE_MAP) {
        ast_map_decl *map_decl = type->value;
        analysis_type(m, &map_decl->key_type);
        analysis_type(m, &map_decl->value_type);
        return;
    }

    if (type->base == TYPE_ARRAY) {
        ast_array_decl *map_decl = type->value;
        analysis_type(m, &map_decl->ast_type);
        return;
    }

    if (type->base == TYPE_FN) {
        type_fn_t *type_fn = type->value;
        analysis_type(m, &type_fn->return_type);
        for (int i = 0; i < type_fn->formal_param_count; ++i) {
            type_t t = type_fn->formal_param_types[i];
            analysis_type(m, &t);
        }
    }

    if (type->base == TYPE_STRUCT) {
        ast_struct_decl *struct_decl = type->value;
        for (int i = 0; i < struct_decl->count; ++i) {
            ast_struct_property item = struct_decl->list[i];
            analysis_type(m, &item.type);
        }
    }

}

void analysis_access(module_t *m, ast_access *access) {
    analysis_expr(m, &access->left);
    analysis_expr(m, &access->key);
}

/*
 * path.test 进行符号改写, 改写成 namespace + module_name
 * struct_a.test
 */
void analysis_select_property(module_t *m, ast_expr *expr) {
    ast_select_property *select = expr->expr;
    if (select->left.assert_type != AST_EXPR_IDENT) {
        analysis_expr(m, &select->left);
        return;
    }
    ast_ident *ident = select->left.expr;
    ast_import *import = table_get(m->import_table, ident->literal);
    if (import == NULL) {
        analysis_expr(m, &select->left);
        return;
    }

    // 改写成 symbol_table 中的名字
    char *unique_ident = ident_with_module_unique_name(import->module_unique_name, select->property);
    expr->assert_type = AST_EXPR_IDENT;
    expr->expr = ast_new_ident(unique_ident);
}

void analysis_binary(module_t *m, ast_binary_expr *expr) {
    analysis_expr(m, &expr->left);
    analysis_expr(m, &expr->right);
}

void analysis_unary(module_t *m, ast_unary_expr *expr) {
    analysis_expr(m, &expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
void analysis_new_struct(module_t *m, ast_new_struct *expr) {
    analysis_type(m, &expr->type);

    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(m, &expr->list[i].value);
    }
}

void analysis_new_map(module_t *m, ast_new_map *expr) {
    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(m, &expr->values[i].key);
        analysis_expr(m, &expr->values[i].value);
    }
}

void analysis_new_list(module_t *m, ast_new_list *expr) {
    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(m, &expr->values[i]);
    }
}

void analysis_while(module_t *m, ast_while_stmt *stmt) {
    analysis_expr(m, &stmt->condition);

    analysis_begin_scope(m);
    analysis_block(m, stmt->body);
    analysis_end_scope(m);
}

void analysis_for_in(module_t *m, ast_for_in_stmt *stmt) {
    analysis_expr(m, &stmt->iterate);

    analysis_begin_scope(m);
    analysis_var_decl(m, stmt->gen_key);
    analysis_var_decl(m, stmt->gen_value);
    analysis_block(m, stmt->body);
    analysis_end_scope(m);
}

void analysis_return(module_t *m, ast_return_stmt *stmt) {
    if (stmt->expr != NULL) {
        analysis_expr(m, stmt->expr);
    }
}

// unique as
// type foo = int
void analysis_type_decl(module_t *m, ast_type_decl_stmt *stmt) {
    analysis_redeclare_check(m, stmt->ident);
    analysis_type(m, &stmt->type);

    analysis_local_ident *local = analysis_new_local(m, SYMBOL_TYPE_CUSTOM, stmt, stmt->ident);
    stmt->ident = local->unique_ident;
}

char *analysis_resolve_type(module_t *m, analysis_function *current, string ident) {
    analysis_local_scope *current_scope = current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->count; ++i) {
            analysis_local_ident *local = current_scope->idents[i];
            if (strcmp(ident, local->ident) == 0) {
                return local->unique_ident;
            }
        }

        current_scope = current_scope->parent;
    }

    if (current->parent == NULL) {
        error_type_not_found(m->analysis_line, ident);
    }

    return analysis_resolve_type(m, current->parent, ident);
}

uint8_t analysis_push_free(analysis_function *current, bool is_local, int8_t index, string ident) {
    analysis_free_ident free = {
            .is_local = is_local,
            .index = index,
            .ident = ident,
    };
    uint8_t free_index = current->free_count++;
    current->frees[free_index] = free;

    return free_index;
}

/**
 * 检查当前作用域及当前 scope 是否重复定义了 ident
 * @param ident
 * @return
 */
bool analysis_redeclare_check(module_t *m, char *ident) {
    analysis_local_scope *current_scope = m->analysis_current->current_scope;
    for (int i = 0; i < current_scope->count; ++i) {
        analysis_local_ident *local = current_scope->idents[i];
        if (strcmp(ident, local->ident) == 0) {
            error_redeclare_ident(m->analysis_line, ident);
            return false;
        }
    }

    return true;
}

/**
 * @param name
 * @return
 */
char *analysis_unique_ident(char *name) {
    return LIR_UNIQUE_NAME(name);
}

void analysis_function_begin(module_t *m) {
    analysis_begin_scope(m);
}

void analysis_function_end(module_t *m) {
    analysis_end_scope(m);

    m->analysis_current = m->analysis_current->parent;
}

analysis_function *analysis_current_init(module_t *m, analysis_local_scope *scope, string fn_name) {
    analysis_function *new = malloc(sizeof(analysis_function));
//  new->local_count = 0;
    new->free_count = 0;
    new->scope_depth = 0;
    new->env_unique_name = str_connect(fn_name, "_env");
    new->contains_fn_count = 0;
    new->current_scope = scope;

// 继承关系
    new->parent = m->analysis_current;
    m->analysis_current = new;

    return m->
            analysis_current;
}

analysis_local_scope *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope *parent) {
    analysis_local_scope *new = malloc(sizeof(analysis_local_scope));
    new->count = 0;
    new->scope_depth = scope_depth;
    new->parent = parent;
    return new;
}

void analysis_module(module_t *m, slice_t *stmt_list) {
    // init
    m->analysis_line = 0;
    int import_end_index = 0;
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->type != AST_STMT_IMPORT) {
            import_end_index = i;
            break;
        }

        ast_import *import = stmt->stmt;
        complete_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
    }

    // var_decl blocks
    slice_t *var_assign_list = slice_new(); // 存放 stmt
    slice_t *fn_list = slice_new();

    for (int i = import_end_index; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->type == AST_VAR_DECL) {
            ast_var_decl *var_decl = stmt->stmt;
            symbol_t *s = NEW(symbol_t);
            s->type = SYMBOL_TYPE_VAR;
            s->ident = ident_with_module_unique_name(m->module_unique_name, var_decl->ident);
            s->decl = var_decl;
            s->is_local = false;
            slice_push(m->symbols, s);
            table_set(symbol_table, s->ident, s);
            continue;
        }

        if (stmt->type == AST_STMT_VAR_DECL_ASSIGN) {
            ast_var_decl_assign_stmt *var_decl_assign = stmt->stmt;
            ast_var_decl *var_decl = var_decl_assign->var_decl;
            symbol_t *s = NEW(symbol_t);
            s->type = SYMBOL_TYPE_VAR;
            s->ident = ident_with_module_unique_name(m->module_unique_name, var_decl->ident);
            s->decl = var_decl;
            s->is_local = false;
            slice_push(m->symbols, s);
            table_set(symbol_table, s->ident, s);

            // 转换成 assign stmt，然后导入到 init 中
            ast_stmt *temp_stmt = NEW(ast_stmt);
            ast_assign_stmt *assign = NEW(ast_assign_stmt);
            assign->left = (ast_expr) {
                    .assert_type = AST_EXPR_IDENT,
                    .expr = ast_new_ident(var_decl->ident),
//                    .type = var_decl->type,
            };
            assign->right = var_decl_assign->expr;
            temp_stmt->type = AST_STMT_ASSIGN;
            temp_stmt->stmt = assign;
            slice_push(var_assign_list, temp_stmt);
            continue;
        }
        if (stmt->type == AST_STMT_TYPE_DECL) {
            ast_type_decl_stmt *type_decl = stmt->stmt;
            symbol_t *s = NEW(symbol_t);
            s->type = SYMBOL_TYPE_CUSTOM;
            s->ident = ident_with_module_unique_name(m->module_unique_name, type_decl->ident);
            s->decl = type_decl;
            s->is_local = false;
            slice_push(m->symbols, s);
            table_set(symbol_table, s->ident, s);
            continue;
        }

        if (stmt->type == AST_NEW_FN) {
            ast_new_fn *new_fn = stmt->stmt;
            symbol_t *s = NEW(symbol_t);
            s->type = SYMBOL_TYPE_FN;
            s->ident = ident_with_module_unique_name(m->module_unique_name, new_fn->name);
            s->decl = new_fn;
            s->is_local = false;
            slice_push(m->symbols, s);
            table_set(symbol_table, s->ident, s);
            slice_push(fn_list, new_fn);
            continue;
        }

        error_exit("[analysis_module] stmt.type not allow, must var_decl/new_fn/type_decl");
    }

    // 添加 init fn
    ast_new_fn *fn_init = NEW(ast_new_fn);
    fn_init->name = INIT_FN_NAME; // TODO unique name
    fn_init->return_type = type_new_base(TYPE_VOID);
    fn_init->formal_param_count = 0;
    fn_init->body = var_assign_list;

    // 加入到全局符号表，等着调用就好了
    symbol_t *s = NEW(symbol_t);
    s->type = SYMBOL_TYPE_FN;
    s->ident = ident_with_module_unique_name(m->module_unique_name, fn_init->name);
    s->decl = fn_init;
    s->is_local = false;
    slice_push(m->symbols, s);
    table_set(symbol_table, s->ident, s);

    slice_push(fn_list, fn_init);

    // 遍历 fn list
    for (int i = 0; i < fn_list->count; ++i) {
        ast_new_fn *fn = fn_list->take[i];
        ast_closure *closure = analysis_new_fn(m, fn, NULL);
        slice_push(m->ast_closures, closure);
    }
}

/**
 * main.c 中定义的所有代码都会被丢到 main closure 中
 * @param t
 * @param stmt_list
 */
void analysis_main(module_t *m, slice_t *stmt_list) {
    // 过滤处 import 部分, 其余部分再包到 main closure 中
    int import_end_index = 0;
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->type != AST_STMT_IMPORT) {
            import_end_index = i;
            break;
        }

        ast_import *import = stmt->stmt;
        complete_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
    }

    // init
    m->analysis_line = 0;

    // block 封装进 function,再封装到 closure 中
    ast_new_fn *new_fn = malloc(sizeof(ast_new_fn));
    new_fn->name = MAIN_FN_NAME;
    new_fn->body = slice_new();
    new_fn->return_type = type_new_base(TYPE_VOID);
    new_fn->formal_param_count = 0;
    for (int i = import_end_index; i < stmt_list->count; ++i) {
        slice_push(new_fn->body, stmt_list->take[i]);
    }

    ast_closure *closure = analysis_new_fn(m, new_fn, NULL);
    slice_push(m->ast_closures, closure);
}

void analysis(module_t *m, slice_t *stmt_list) {
    if (m->entry) {
        analysis_main(m, stmt_list);
    } else {
        analysis_module(m, stmt_list);
    }
}
