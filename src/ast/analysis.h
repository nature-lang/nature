#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_
#include "ast.h"

typedef struct {
  string type_name;
  string ident; // 原始名称
  string unique_ident; // 唯一名称
  int scope_depth;
  bool is_capture; // 是否被捕获(是否被下级引用)
} analysis_local_var;

// 如果 is_local 为 true 则 index 为 locals[index]
// 如果 is_local 为 false 则 index 为参数 env[index]
typedef struct {
  bool is_local;
  uint8_t index;
} analysis_free_var;

typedef struct analysis_function {
  struct analysis_function *parent;
  analysis_local_var *locals[UINT8_MAX];
  uint8_t local_count;
  analysis_free_var frees[UINT8_MAX];
  uint8_t free_count;
  uint8_t scope_depth;  // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
  string env_unique_name; // 便于值改写, 放心 env unique name 会注册到字符表的,print x86 要用
} analysis_function;

analysis_function *current_function = NULL;

// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换
ast_closure_decl analysis(ast_block_stmt stmt_list);

// 变量 hash_string 表
void analysis_function_begin();
void analysis_function_end();
void analysis_block(ast_block_stmt *block);

void analysis_var_decl(ast_var_decl *var_decal);

void analysis_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign);
ast_closure_decl *analysis_function_decl(ast_function_decl *function);
void analysis_expr(ast_expr *expr);
void analysis_binary(ast_binary_expr *expr);
void analysis_literal(ast_literal *expr);
void analysis_ident(ast_expr *expr);
int8_t analysis_resolve_free(analysis_function *f, ast_ident *ident);
int8_t analysis_push_free(analysis_function *f, bool is_local, int8_t index);
void analysis_call_function(ast_call *call);
void analysis_assign(ast_assign_stmt *assign);
void analysis_if(ast_if_stmt *);

analysis_local_var *analysis_new_local(string type, string ident);

string unique_var_ident(string name);
void analysis_begin_scope();
void analysis_end_scope();

#endif //NATURE_SRC_AST_ANALYSIS_H_
