#ifndef NATURE_SRC_AST_OPTIMIZE_H_
#define NATURE_SRC_AST_OPTIMIZE_H_

#include "src/ast.h"
#include "src/lib/table.h"

/**
 * 类型列表，包括基础类型和自定义类型
 */
typedef struct {
  string name;
  uint16_t size; // 类型字节数
  ast_struct_decl *custom_type; // 自定义类型引用
} type_entry;

/**
 * 变量列表,包含函数名称，函数参数
 */
typedef struct {
  string ident;
  type_entry *type; // 变量名称与类型
} var_entry;

table var_table;
table type_table;

typedef struct {
  string ident; // 原始名称
  string unique_ident; // 唯一名称
} optimize_var;

typedef struct optimize_scope {
  struct optimize_scope *parent;
  optimize_var vars[UINT8_MAX];
  int var_count;
} optimize_scope;

optimize_scope *current_scope = NULL;

int global_var_count = 0;

// 变量 hash_string 表
void optimize(ast_function_decl *main);
void optimize_block(ast_block_stmt *block);
void optimize_var_decl(ast_var_decl_stmt *var_decal);
void optimize_var_decl_assign(ast_var_decl_assign_stmt *var_decl_assign);
void optimize_function_decl(ast_function_decl *function);
void optimize_expr(ast_expr *expr);
void optimize_binary(ast_binary_expr *expr);
void optimize_literal(ast_literal *expr);
void optimize_ident(ast_ident *ident);

void optimize_new_var(optimize_scope *scope, string ident);

string unique_var_ident(string name);
void optimize_begin_scope();
void optimize_end_scope();
#endif //NATURE_SRC_AST_OPTIMIZE_H_
