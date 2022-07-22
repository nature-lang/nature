#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "src/lib/helper.h"
#include "analysis.h"
#include "src/lib/error.h"
#include "src/lib/table.h"
#include "src/symbol.h"
#include "src/debug/debug.h"

ast_closure_decl analysis(ast_block_stmt block_stmt) {
    // init
    symbol_ident_table_init();
    unique_name_count = 0;
    analysis_line = 0;

    // block 封装进 function,再封装到 closure 中
    ast_new_fn *function_decl = malloc(sizeof(ast_new_fn));
    function_decl->name = MAIN_FUNCTION_NAME;
    function_decl->body = block_stmt;
    function_decl->return_type = ast_new_simple_type(TYPE_VOID);
    function_decl->formal_param_count = 0;

    ast_closure_decl *closure_decl = analysis_function_decl(function_decl, NULL);
    return *closure_decl;
}

void analysis_block(ast_block_stmt *block) {
    for (int i = 0; i < block->count; ++i) {
        analysis_line = block->list[i].line;
        // switch 结构导向优化
#ifdef DEBUG_ANALYSIS
        debug_stmt("ANALYSIS", block->list[i]);
#endif
        analysis_stmt(&block->list[i]);
    }
}

void analysis_stmt(ast_stmt *stmt) {
    switch (stmt->type) {
        case AST_VAR_DECL: {
            analysis_var_decl((ast_var_decl *) stmt->stmt);
            break;
        }
        case AST_STMT_VAR_DECL_ASSIGN: {
            analysis_var_decl_assign((ast_var_decl_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_ASSIGN: {
            analysis_assign((ast_assign_stmt *) stmt->stmt);
            break;
        }
        case AST_NEW_FN: {
            // 注册 function_decl_type + ident
            analysis_function_decl_ident((ast_new_fn *) stmt->stmt);

            // 函数体添加到 延迟处理
            uint8_t count = analysis_current->contains_fn_count++;
            analysis_current->contains_fn_decl[count].stmt = stmt;
            analysis_current->contains_fn_decl[count].is_stmt = true;
            break;
        }
        case AST_CALL: {
            analysis_call((ast_call *) stmt->stmt);
            break;
        }
        case AST_STMT_IF: {
            analysis_if((ast_if_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_WHILE: {
            analysis_while((ast_while_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_FOR_IN: {
            analysis_for_in((ast_for_in_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_RETURN: {
            analysis_return((ast_return_stmt *) stmt->stmt);
            break;
        }
        case AST_STMT_TYPE_DECL: {
            analysis_type_decl((ast_type_decl_stmt *) stmt->stmt);
            break;
        }
        default:
            return;
    }
}

void analysis_if(ast_if_stmt *if_stmt) {
    analysis_expr(&if_stmt->condition);

    analysis_begin_scope();
    analysis_block(&if_stmt->consequent);
    analysis_end_scope();

    analysis_begin_scope();
    analysis_block(&if_stmt->alternate);
    analysis_end_scope();
}

void analysis_assign(ast_assign_stmt *assign) {
    analysis_expr(&assign->left);
    analysis_expr(&assign->right);
}

void analysis_call(ast_call *call) {
    // 函数地址改写
    analysis_expr(&call->left);

    // 实参改写
    for (int i = 0; i < call->actual_param_count; ++i) {
        ast_expr *actual_param = &call->actual_params[i];
        analysis_expr(actual_param);
    }
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param stmt
 */
void analysis_var_decl(ast_var_decl *stmt) {
    analysis_redeclare_check(stmt->ident);

    analysis_type(&stmt->type);

    analysis_local_ident *local = analysis_new_local(SYMBOL_TYPE_VAR, stmt, stmt->ident);

    // 改写
    stmt->ident = local->unique_ident;
}

void analysis_var_decl_assign(ast_var_decl_assign_stmt *stmt) {
    analysis_redeclare_check(stmt->var_decl->ident);

    analysis_type(&stmt->var_decl->type);

    analysis_local_ident *local = analysis_new_local(SYMBOL_TYPE_VAR, stmt->var_decl, stmt->var_decl->ident);
    stmt->var_decl->ident = local->unique_ident;

    analysis_expr(&stmt->expr);
}

ast_type analysis_function_to_type(ast_new_fn *function_decl) {
    ast_function_type_decl *function_type_decl = malloc(sizeof(ast_function_type_decl));
    function_type_decl->return_type = function_decl->return_type;
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        function_type_decl->formal_params[i] = function_decl->formal_params[i];
    }
    function_type_decl->formal_param_count = function_decl->formal_param_count;
    ast_type type = {
            .is_origin = false,
            .category = TYPE_FN,
            .value = function_type_decl
    };
    return type;
}

void analysis_function_decl_ident(ast_new_fn *new_fn) {
    // 仅 fun 再次定义 name 才需要再次添加到符号表
    if (strcmp(new_fn->name, MAIN_FUNCTION_NAME) != 0) {
        if (strlen(new_fn->name) == 0) {
            // 如果没有函数名称，则添加匿名函数名称
            new_fn->name = analysis_unique_ident(ANONYMOUS_FUNCTION_NAME);
        }

        analysis_redeclare_check(new_fn->name);

        // 函数名称改写
        analysis_local_ident *local = analysis_new_local(
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
ast_closure_decl *analysis_function_decl(ast_new_fn *function_decl, analysis_local_scope *scope) {
    ast_closure_decl *closure = malloc(sizeof(ast_closure_decl));
    analysis_type(&function_decl->return_type);

    // 初始化
    analysis_current_init(scope);
    // 开启一个新的 function 作用域
    analysis_function_begin();

    // 函数形参处理
    for (int i = 0; i < function_decl->formal_param_count; ++i) {
        ast_var_decl *param = function_decl->formal_params[i];
        // 注册
        analysis_local_ident *param_local = analysis_new_local(SYMBOL_TYPE_VAR, param, param->ident);
        // 改写
        param->ident = param_local->unique_ident;

        analysis_type(&param->type);
    }

    // 分析请求体 block, 其中进行了自由变量的捕获/改写和局部变量改写
    analysis_block(&function_decl->body);

    // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
    // free 是在外部环境构建 env 的。
    // current_function->env_unique_name = unique_var_ident(ENV_IDENT);
    closure->env_name = analysis_current->env_unique_name;

    // 构造 env
    for (int i = 0; i < analysis_current->free_count; ++i) {
        analysis_free_ident free_var = analysis_current->frees[i];
        ast_expr expr = closure->env[i];

        // 逃逸变量就在当前环境中
        if (free_var.is_local) {
            // ast_ident 表达式
            expr.type = AST_EXPR_IDENT;
            expr.expr = ast_new_ident(analysis_current->parent->current_scope->idents[free_var.index]->unique_ident);
        } else {
            // ast_env_index 表达式
            expr.type = AST_EXPR_ACCESS_ENV;
            ast_access_env *access_env = malloc(sizeof(ast_access_env));
            access_env->env = ast_new_ident(analysis_current->parent->env_unique_name);
            access_env->index = free_var.index;
            expr.expr = access_env;
        }
    }
    closure->env_count = analysis_current->free_count;
    closure->function = function_decl;

    // 延迟处理 contains_function_decl
    for (int i = 0; i < analysis_current->contains_fn_count; ++i) {
        if (analysis_current->contains_fn_decl[i].is_stmt) {
            ast_stmt *stmt = analysis_current->contains_fn_decl[i].stmt;
            // 函数注册到符号表已经在函数定义点注册过了
            ast_closure_decl *closure_decl = analysis_function_decl(stmt->stmt,
                                                                    analysis_current->contains_fn_decl[i].scope);
            stmt->type = AST_NEW_CLOSURE;
            stmt->stmt = closure_decl;
        } else {
            ast_expr *expr = analysis_current->contains_fn_decl[i].expr;
            ast_closure_decl *closure_decl = analysis_function_decl(expr->expr,
                                                                    analysis_current->contains_fn_decl[i].scope);
            expr->type = AST_NEW_CLOSURE;
            expr->expr = closure_decl;
        };
    }

    analysis_function_end(); // 退出当前 current
    return closure;
}

/**
 * 块级作用域处理
 */
void analysis_begin_scope() {
    analysis_current->scope_depth++;
    analysis_local_scope *current_scope = analysis_current->current_scope;
    analysis_current->current_scope = analysis_new_local_scope(analysis_current->scope_depth, current_scope);
}

void analysis_end_scope() {
    analysis_local_scope *current_scope = analysis_current->current_scope;
    analysis_current->current_scope = current_scope->parent;
    analysis_current->scope_depth--;
}

/**
 * type 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
analysis_local_ident *analysis_new_local(symbol_type type, void *decl, string ident) {
    // unique ident
    string unique_ident = analysis_unique_ident(ident);

    analysis_local_ident *local = malloc(sizeof(analysis_local_ident));
    local->ident = ident;
    local->unique_ident = unique_ident;
    local->scope_depth = analysis_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    analysis_local_scope *current_scope = analysis_current->current_scope;
    current_scope->idents[current_scope->count++] = local;

    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl);

    return local;
}

void analysis_expr(ast_expr *expr) {
    switch (expr->type) {
        case AST_EXPR_BINARY: {
            analysis_binary((ast_binary_expr *) expr->expr);
            break;
        };
        case AST_EXPR_UNARY: {
            analysis_unary((ast_unary_expr *) expr->expr);
            break;
        };
        case AST_EXPR_NEW_STRUCT: {
            analysis_new_struct((ast_new_struct *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_MAP: {
            analysis_new_map((ast_new_map *) expr->expr);
            break;
        }
        case AST_EXPR_NEW_LIST: {
            analysis_new_list((ast_new_list *) expr->expr);
            break;
        }
        case AST_EXPR_ACCESS: {
            analysis_access((ast_access *) expr->expr);
            break;
        };
        case AST_EXPR_SELECT_PROPERTY: {
            analysis_select_property((ast_select_property *) expr->expr);
            break;
        };
        case AST_EXPR_IDENT: {
            // 核心改写逻辑
            analysis_ident(expr);
            break;
        };
        case AST_CALL: {
            analysis_call((ast_call *) expr->expr);
            break;
        }
        case AST_NEW_FN: { // 右值
            analysis_function_decl_ident((ast_new_fn *) expr->expr);

            // 函数体添加到 延迟处理
            uint8_t count = analysis_current->contains_fn_count++;
            analysis_current->contains_fn_decl[count].expr = expr;
            analysis_current->contains_fn_decl[count].is_stmt = false;
            analysis_current->contains_fn_decl[count].scope = analysis_current->current_scope;
            break;
        }
        default:
            return;
    }
}

/**
 * @param expr
 */
void analysis_ident(ast_expr *expr) {
    ast_ident *ident = expr->expr;

    // if ident is external symbol，not analysis and rename
    if (is_debug_symbol(ident->literal)) {
        return;
    }

    // 在当前函数作用域中查找变量定义
    analysis_local_scope *current_scope = analysis_current->current_scope;
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
    int8_t free_var_index = analysis_resolve_free(analysis_current, ident->literal);
    if (free_var_index == -1) {
        error_ident_not_found(expr->line, ident->literal);
        exit(0);
    }

    // 外部作用域变量改写, 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
    expr->type = AST_EXPR_ACCESS_ENV;
    ast_access_env *env_index = malloc(sizeof(ast_access_env));
    env_index->env = ast_new_ident(analysis_current->env_unique_name);
    env_index->index = free_var_index;
    expr->expr = env_index;
}

/**
 * 返回 top scope ident index
 * @param current
 * @param ident
 * @return
 */
int8_t analysis_resolve_free(analysis_function *current, string ident) {
    if (current->parent == NULL) {
        return -1;
    }

    analysis_local_scope *scope = current->parent->current_scope;
    for (int i = 0; i < scope->count; ++i) {
        analysis_local_ident *local = scope->idents[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(ident, local->ident) == 0) {
            scope->idents[i]->is_capture = true; // 被下级作用域引用

            return (int8_t) analysis_push_free(current, true, (int8_t) i);
        }
    }

    // 继续向上递归查询
    int8_t parent_free_index = analysis_resolve_free(current->parent, ident);
    if (parent_free_index != -1) {
        return (int8_t) analysis_push_free(current, false, parent_free_index);
    }

    return -1;
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。直接定位唯一名称即可
 * @param type
 */
void analysis_type(ast_type *type) {
    // 如果只是简单的 ident,又应该如何改写呢？
    if (type->category == TYPE_DECL_IDENT) {
        // 向上查查查
        ast_ident *ident = type->value;
        string unique_name = analysis_resolve_type(analysis_current, ident->literal);
        ident->literal = unique_name;
        return;
    }

    if (type->category == TYPE_MAP) {
        ast_map_decl *map_decl = type->value;
        analysis_type(&map_decl->key_type);
        analysis_type(&map_decl->value_type);
        return;
    }

    if (type->category == TYPE_LIST) {
        ast_list_decl *map_decl = type->value;
        analysis_type(&map_decl->type);
        return;
    }

    if (type->category == TYPE_FN) {
        ast_function_type_decl *function_type_decl = type->value;
        analysis_type(&function_type_decl->return_type);
        for (int i = 0; i < function_type_decl->formal_param_count; ++i) {
            ast_var_decl *param = function_type_decl->formal_params[i];
            analysis_type(&param->type);
        }
    }

    if (type->category == TYPE_STRUCT) {
        ast_struct_decl *struct_decl = type->value;
        for (int i = 0; i < struct_decl->count; ++i) {
            ast_struct_property item = struct_decl->list[i];
            analysis_type(&item.type);
        }
    }

}

void analysis_access(ast_access *access) {
    analysis_expr(&access->left);
    analysis_expr(&access->key);
}

void analysis_select_property(ast_select_property *select) {
    analysis_expr(&select->left);
}

void analysis_binary(ast_binary_expr *expr) {
    analysis_expr(&expr->left);
    analysis_expr(&expr->right);
}

void analysis_unary(ast_unary_expr *expr) {
    analysis_expr(&expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
void analysis_new_struct(ast_new_struct *expr) {
    analysis_type(&expr->type);

    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(&expr->list[i].value);
    }
}

void analysis_new_map(ast_new_map *expr) {
    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(&expr->values[i].key);
        analysis_expr(&expr->values[i].value);
    }
}

void analysis_new_list(ast_new_list *expr) {
    for (int i = 0; i < expr->count; ++i) {
        analysis_expr(&expr->values[i]);
    }
}

void analysis_while(ast_while_stmt *stmt) {
    analysis_expr(&stmt->condition);

    analysis_begin_scope();
    analysis_block(&stmt->body);
    analysis_end_scope();
}

void analysis_for_in(ast_for_in_stmt *stmt) {
    analysis_expr(&stmt->iterate);

    analysis_begin_scope();
    analysis_var_decl(stmt->gen_key);
    analysis_var_decl(stmt->gen_value);
    analysis_block(&stmt->body);
    analysis_end_scope();
}

void analysis_return(ast_return_stmt *stmt) {
    analysis_expr(&stmt->expr);
}

// unique name
void analysis_type_decl(ast_type_decl_stmt *stmt) {
    analysis_redeclare_check(stmt->ident);
    analysis_type(&stmt->type);

    analysis_local_ident
            *local = analysis_new_local(SYMBOL_TYPE_CUSTOM, stmt, stmt->ident);
    stmt->ident = local->unique_ident;
}

char *analysis_resolve_type(analysis_function *current, string ident) {
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
        error_type_not_found(analysis_line, ident);
    }

    return analysis_resolve_type(current->parent, ident);
}

uint8_t analysis_push_free(analysis_function *current, bool is_local, int8_t index) {
    analysis_free_ident free = {
            .is_local = is_local,
            .index = index
    };
    uint8_t free_index = current->free_count++;
    current->frees[free_index] = free;

    return free_index;
}

/**
 * 检查当前作用域及当前 scope
 * @param ident
 * @return
 */
bool analysis_redeclare_check(char *ident) {
    analysis_local_scope *current_scope = analysis_current->current_scope;
    for (int i = 0; i < current_scope->count; ++i) {
        analysis_local_ident *local = current_scope->idents[i];
        if (strcmp(ident, local->ident) == 0) {
            error_redeclare_ident(analysis_line, ident);
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
    // +2: _ and '\0'
//  char *unique_name = malloc(strlen(name) + sizeof(int) + 2);
//  sprintf(unique_name, "%d-%s", unique_name_count++, name);
//  return unique_name;
    return LIR_UNIQUE_NAME(name);
}

void analysis_function_begin() {
    analysis_begin_scope();
}

void analysis_function_end() {
    analysis_end_scope();

    analysis_current = analysis_current->parent;
}

analysis_function *analysis_current_init(analysis_local_scope *scope) {
    analysis_function *new = malloc(sizeof(analysis_function));
//  new->local_count = 0;
    new->free_count = 0;
    new->scope_depth = 0;
    new->env_unique_name = analysis_unique_ident(ENV_IDENT);
    new->contains_fn_count = 0;
    new->current_scope = scope;

    // 继承关系
    new->parent = analysis_current;
    analysis_current = new;

    return analysis_current;
}

analysis_local_scope *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope *parent) {
    analysis_local_scope *new = malloc(sizeof(analysis_local_scope));
    new->count = 0;
    new->scope_depth = scope_depth;
    new->parent = parent;
    return new;
}
