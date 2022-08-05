#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_

#include "src/ast.h"
#include "src/symbol.h"
#include "src/target.h"

#define MAIN_FUNCTION_NAME "main"
#define ENV_IDENT "env"
#define ANONYMOUS_FUNCTION_NAME "fn"

int unique_name_count;
int analysis_line;

// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package name 确定使用 analysis_main 还是 analysis_target
 * @param t
 * @param stmt_list
 */
void analysis(target_t *t, ast_block_stmt stmt_list);

void analysis_main(target_t *t, ast_block_stmt stmt_list);

/**
 * 1. 符号表信息注册
 * 2. 检测 stmt 类型是否合法
 * 3. 将所有的 var_decl 编译到 init closure
 * @param t
 * @param stmt_list
 */
void analysis_target(target_t *t, ast_block_stmt stmt_list);

analysis_function *analysis_current_init(analysis_local_scope *scope, string fn_name);

analysis_local_scope *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope *parent);

// 变量 hash_string 表
void analysis_function_begin();

void analysis_function_end();

void analysis_block(ast_block_stmt *block);

void analysis_var_decl(ast_var_decl *stmt);

void analysis_var_decl_assign(ast_var_decl_assign_stmt *stmt);

ast_closure_decl *analysis_function_decl(ast_new_fn *function_decl, analysis_local_scope *scope);

void analysis_function_decl_ident(ast_new_fn *new_fn);

void analysis_stmt(ast_stmt *stmt);

void analysis_expr(ast_expr *expr);

void analysis_binary(ast_binary_expr *expr);

void analysis_unary(ast_unary_expr *expr);

void analysis_ident(ast_expr *expr);

void analysis_type(type_t *type);

int8_t analysis_resolve_free(analysis_function *current, string*ident);

uint8_t analysis_push_free(analysis_function *f, bool is_local, int8_t index, string ident);

void analysis_call(ast_call *call);

void analysis_assign(ast_assign_stmt *assign);

string analysis_resolve_type(analysis_function *current, string ident);

void analysis_if(ast_if_stmt *stmt);

void analysis_while(ast_while_stmt *stmt);

void analysis_for_in(ast_for_in_stmt *stmt);

void analysis_return(ast_return_stmt *stmt);

void analysis_type_decl(ast_type_decl_stmt *stmt);

void analysis_access(ast_access *expr);

void analysis_select_property(ast_select_property *expr);

void analysis_new_struct(ast_new_struct *expr);

void analysis_new_map(ast_new_map *expr);

void analysis_new_list(ast_new_list *expr);

bool analysis_redeclare_check(string ident);

analysis_local_ident *analysis_new_local(symbol_type type, void *decl, string ident);

string analysis_unique_ident(string name);

void analysis_begin_scope();

void analysis_end_scope();

type_t analysis_function_to_type(ast_new_fn *function_decl);

#endif //NATURE_SRC_AST_ANALYSIS_H_
