#ifndef NATURE_SRC_AST_OPTIMIZE_H_
#define NATURE_SRC_AST_OPTIMIZE_H_

#include "src/ast.h"
#include "src/lib/table.h"

#define ENV "env"

/**
 * 类型列表，包括基础类型和自定义类型
 */
typedef struct {
  string name; // 类型名称
  uint16_t size; // 类型字节数
  ast_struct_decl *custom_type; // 自定义类型引用
} type_entry;

table var_table; // 添加内容为 optimize_local_var
table type_table;

typedef struct {
  string type_name;
  string ident; // 原始名称
  string unique_ident; // 唯一名称
  int scope_depth;
  bool is_capture; // 是否被捕获
} optimize_local_var;

// 如果 is_local 为 true 则 index 为 locals[index]
// 如果 is_local 为 false 则 index 为参数 env[index]
typedef struct {
  bool is_local;
  uint8_t index;
} optimize_free_var;

typedef struct optimize_function {
  struct optimize_function *parent;
  optimize_local_var *locals[UINT8_MAX];
  uint8_t local_count;
  optimize_free_var frees[UINT8_MAX];
  uint8_t free_count;
  uint8_t scope_depth;  // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
  string env_unique_name; // 便于值改写, 放心 env unique name 会注册到字符表的,print x86 要用
} optimize_function;

optimize_function *current_function = NULL;

// 变量 hash_string 表
void optimize(ast_function_decl *main);
void optimize_block(ast_block_stmt *block);
void optimize_var_decl(ast_var_decl_stmt *var_decal);
void optimize_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign);
ast_closure_decl *optimize_function_decl(ast_function_decl *function);
void optimize_expr(ast_expr *expr);
void optimize_binary(ast_binary_expr *expr);
void optimize_literal(ast_literal *expr);
void optimize_ident(ast_expr *expr);
int8_t optimize_resolve_free(optimize_function *f, ast_ident *ident);
int8_t optimize_push_free(optimize_function *f, bool is_local, int8_t index);

optimize_local_var *optimize_new_local(string type, string ident);

string unique_var_ident(string name);
void optimize_begin_scope();
void optimize_end_scope();
#endif //NATURE_SRC_AST_OPTIMIZE_H_
