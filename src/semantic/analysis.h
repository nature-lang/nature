#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_

#include "src/ast.h"
#include "src/symbol/symbol.h"
#include "src/module.h"


#define ANONYMOUS_FN_NAME "_"


// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换,import 收集
// 都放在 target 中就行了
/**
 * 根据 package as 确定使用 analysis_main 还是 analysis_module
 * @param t
 * @param stmt_list
 */
void analysis(module_t *m, slice_t *stmt_list);

//void analysis_main(module_t *m, slice_t *stmt_list);

/**
 * 1. 符号表信息注册
 * 2. 检测 stmt 类型是否合法
 * 3. 将所有的 var_decl 编译到 init closure_t
 * @param m
 * @param stmt_list
 */
//void analysis_module(module_t *m, slice_t *stmt_list);

//analysis_function_t *analysis_current_init(module_t *m, analysis_local_scope_t *scope, string fn_name);

//analysis_local_scope_t *analysis_new_local_scope(uint8_t scope_depth, analysis_local_scope_t *parent);
//
//ast_closure_t *analysis_new_fn(module_t *m, ast_fn_decl *function_decl, analysis_local_scope_t *scope);
//
//bool analysis_redeclare_check(module_t *m, string ident);
//
//analysis_local_ident_t *analysis_new_local(module_t *m, symbol_type type, void *decl, string ident);
//
//typeuse_t analysis_fn_to_type(ast_fn_decl *fn_decl);
//
//string analysis_resolve_type(module_t *m, analysis_function_t *current, string ident);
//
//int8_t analysis_resolve_free(analysis_function_t *current, string*ident);
//
//char *analysis_unique_ident(module_t *m, char *name);
//
//char *analysis_unique_ident(module_t *m, char *name);

static void analysis_expr(module_t *m, ast_expr *expr);

static void analysis_stmt(module_t *m, ast_stmt *stmt);

//// 变量 hash_string 表
//void analysis_function_begin(module_t *m);
//
//void analysis_function_end(module_t *m);
//
//void analysis_block(module_t *m, slice_t *block);
//
//void analysis_var_decl(module_t *m, ast_var_decl *stmt);
//
//void analysis_var_decl_assign(module_t *m, ast_var_assign_stmt *stmt);
//
//void analysis_fn_decl_ident(module_t *m, ast_fn_decl *new_fn);
//
//
//
//void analysis_binary(module_t *m, ast_binary_expr *expr);
//
//void analysis_unary(module_t *m, ast_unary_expr *expr);
//
//void analysis_ident(module_t *m, ast_expr *expr);
//
//void analysis_type(module_t *m, typeuse_t *type);
//
//void analysis_call(module_t *m, ast_call *call);
//
//void analysis_assign(module_t *m, ast_assign_stmt *assign);
//
//void analysis_if(module_t *m, ast_if_stmt *stmt);
//
//void analysis_while(module_t *m, ast_for_cond_stmt *stmt);
//
//void analysis_for_in(module_t *m, ast_for_iterator_stmt *stmt);
//
//void analysis_return(module_t *m, ast_return_stmt *stmt);
//
//void analysis_type_decl(module_t *m, ast_typedef_stmt *stmt);
//
//void analysis_access(module_t *m, ast_access *expr);
//
//void analysis_select_property(module_t *m, ast_expr *expr);
//
//void analysis_new_struct(module_t *m, ast_struct_new_t *expr);
//
//void analysis_new_map(module_t *m, ast_map_new *expr);
//
//void analysis_new_list(module_t *m, ast_list_new *expr);
//
//void analysis_begin_scope(module_t *m);
//
//void analysis_end_scope(module_t *m);


#endif //NATURE_SRC_AST_ANALYSIS_H_
