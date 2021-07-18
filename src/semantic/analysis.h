#ifndef NATURE_SRC_AST_ANALYSIS_H_
#define NATURE_SRC_AST_ANALYSIS_H_
#include "src/ast.h"
#include "src/symbol.h"

#define MAIN_FUNCTION_NAME "main"
#define ENV_IDENT "env"

int unique_name_count;

typedef struct {
//  ast_type type;
  symbol_type belong; // function/var/
  ast_type type;
  string ident; // 原始名称
  string unique_ident; // 唯一名称
  int scope_depth;
  bool is_capture; // 是否被捕获(是否被下级引用)
} analysis_local_ident;

/**
 * free_var 是在 parent function 作用域中被使用,但是被捕获存放在了 current function free_vars 中,
 * 所以这里的 is_local 指的是在 parent 中的位置
 * 如果 is_local 为 true 则 index 为 parent.locals[index]
 * 如果 is_local 为 false 则 index 为参数 env[index]
 */
typedef struct {
  bool is_local;
  uint8_t index;
} analysis_free_ident;

/**
 * 词法作用域
 */
typedef struct analysis_function {
  struct analysis_function *parent;

  analysis_local_ident *locals[UINT8_MAX];
  uint8_t local_count;

  // wwh: 使用了当前作用域之外的变量
  analysis_free_ident frees[UINT8_MAX];
  uint8_t free_count;

  // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
  uint8_t scope_depth;

  // 便于值改写, 放心 env unique name 会注册到字符表的要用
  string env_unique_name;
} analysis_function;

analysis_function *current_function = NULL;

// 符号表收集，类型检查、变量作用域检查（作用域单赋值），闭包转换
ast_closure_decl analysis(ast_block_stmt stmt_list);

analysis_function *analysis_function_init();

// 变量 hash_string 表
void analysis_function_begin();
void analysis_function_end();
void analysis_block(ast_block_stmt *block);

void analysis_var_decl(ast_var_decl *var_decal);

void analysis_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign);
ast_closure_decl *analysis_function_decl(ast_function_decl *function);
void analysis_expr(ast_expr *expr);
void analysis_binary(ast_binary_expr *expr);
void analysis_unary(ast_unary_expr *expr);

void analysis_ident(ast_expr *expr);
void analysis_type(ast_type *type);

int8_t analysis_resolve_free(analysis_function *current, string ident);
uint8_t analysis_push_free(analysis_function *f, bool is_local, int8_t index);
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

bool analysis_redeclared_check(string ident);

analysis_local_ident *analysis_new_local(symbol_type belong, ast_type type, string ident);

string unique_var_ident(string name);
void analysis_begin_scope();
void analysis_end_scope();

#endif //NATURE_SRC_AST_ANALYSIS_H_
