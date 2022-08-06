#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_

#include "src/ast.h"
#include "src/symbol.h"
#include "src/module.h"

#define MAIN_FN_NAME "main"
#define INIT_FN_NAME "init"
#define ENV_IDENT "env"
#define ANONYMOUS_FN_NAME "_"


// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package as 确定使用 analysis_main 还是 analysis_module
 * @param t
 * @param stmt_list
 */
void analysis(module_t *m, slice_t *stmt_list);

void analysis_main(module_t *m, slice_t *stmt_list);

/**
 * 1. 符号表信息注册
 * 2. 检测 stmt 类型是否合法
 * 3. 将所有的 var_decl 编译到 init closure
 * @param m
 * @param stmt_list
 */
void analysis_module(module_t *m, slice_t *stmt_list);

analysis_function *analysis_current_init(module_t *m, analysis_local_scope *scope, string fn_name);

analysis_local_scope *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope *parent);

// 变量 hash_string 表
void analysis_function_begin(module_t *m);

void analysis_function_end(module_t *m);

void analysis_block(module_t *m, slice_t *block);

void analysis_var_decl(module_t *m, ast_var_decl *stmt);

void analysis_var_decl_assign(module_t *m, ast_var_decl_assign_stmt *stmt);

ast_closure *analysis_new_fn(module_t *m, ast_new_fn *function_decl, analysis_local_scope *scope);

void analysis_function_decl_ident(module_t *m, ast_new_fn *new_fn);

void analysis_stmt(module_t *m, ast_stmt *stmt);

void analysis_expr(module_t *m, ast_expr *expr);

void analysis_binary(module_t *m, ast_binary_expr *expr);

void analysis_unary(module_t *m, ast_unary_expr *expr);

void analysis_ident(module_t *m, ast_expr *expr);

void analysis_type(module_t *m, type_t *type);

int8_t analysis_resolve_free(analysis_function *current, string*ident);

uint8_t analysis_push_free(analysis_function *f, bool is_local, int8_t index, string ident);

void analysis_call(module_t *m, ast_call *call);

void analysis_assign(module_t *m, ast_assign_stmt *assign);

string analysis_resolve_type(module_t *m, analysis_function *current, string ident);

void analysis_if(module_t *m, ast_if_stmt *stmt);

void analysis_while(module_t *m, ast_while_stmt *stmt);

void analysis_for_in(module_t *m, ast_for_in_stmt *stmt);

void analysis_return(module_t *m, ast_return_stmt *stmt);

void analysis_type_decl(module_t *m, ast_type_decl_stmt *stmt);

void analysis_access(module_t *m, ast_access *expr);

void analysis_select_property(module_t *m, ast_expr *expr);

void analysis_new_struct(module_t *m, ast_new_struct *expr);

void analysis_new_map(module_t *m, ast_new_map *expr);

void analysis_new_list(module_t *m, ast_new_list *expr);

bool analysis_redeclare_check(module_t *m, string ident);

analysis_local_ident *analysis_new_local(module_t *m, symbol_type type, void *decl, string ident);

string analysis_unique_ident(string name);

void analysis_begin_scope(module_t *m);

void analysis_end_scope(module_t *m);

type_t analysis_function_to_type(ast_new_fn *function_decl);

#endif //NATURE_SRC_AST_ANALYSIS_H_
