#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "value.h"
#include "ast.h"

#define AST_LITERAL_TYPE_INT 0
#define AST_LITERAL_TYPE_FLOAT 1
#define AST_LITERAL_TYPE_STRING 2
#define AST_LITERAL_TYPE_IDENTIFIER 3

typedef enum {
  AST_IN_TYPE_INT
  AST_IN_TYPE_FLOAT
  AST_IN_TYPE_BOOL
  AST_IN_TYPE_STRING
} ast_in_type;

typedef enum {
  AST_EXPR_TYPE_LITERAL,
  AST_EXPR_TYPE_BINARY
} ast_expr_type;

typedef enum {
  AST_EXPR_ADD,
  AST_EXPR_SUBTRACT,
  AST_EXPR_MULTIPLY,
  AST_EXPR_DIVIDE
} ast_expr_operator;

typedef struct {
  iint8_t type; // 声明语句类型
  void *statement;
} ast_stat;

typedef struct {
  iint8_t type; // 表达式类型
  void *expr;
} ast_expr;

typedef struct {
  size_t count; // 当前数量, unsigned int
  size_t capacity; // 容量
  ast_stat *list;
} ast_block_stat;
void ast_init_block_statement(ast_block_stat *block);
void ast_insert_block_statement(ast_block_stat *block, ast_stat statement);

// 标量表达式
typedef struct {
  int8_t type;
  string value;
} ast_literal_expr;

// 一元表达式
typedef struct {
  string operator; // 取反，取绝对值
  ast_expr operand; // 操作对象
} ast_unary_expr;

// 二元表达式
typedef struct {
  int8_t operator; // +/-/*// 等 二元表达式
  ast_expr right;
  ast_expr left;
} ast_binary_expr;

// 调用函数
typedef struct {
  string identifier;
  ast_expr actual_parameters[UINT8_MAX];
} ast_call_function;

typedef struct {
  ast_expr condition;
  ast_block_stat consequent;
  ast_block_stat alternate;
} ast_if_stat;

// int a;
typedef struct {
  string identifier;
  ast_expr type; // 系统 type 或者自定义 struct
} ast_var_decl_stat;

typedef struct {
  ast_expr left; // a  或 foo.bar.car
  ast_expr expr;
} ast_assign_stat;

typedef struct {
  ast_expr type; // 系统 type 或者自定义 struct
  string identifier;
  ast_expr expr;
} ast_var_decl_assign_stat;

typedef struct {
  ast_expr expr;
} ast_return_stat;

typedef struct {

} ast_struct_decl;

typedef struct {
  ast_expr obj; // 可以是 identity 也可以是 struct member
  ast_expr property; // identity 的含义是啥
} ast_access_struct_expr;

typedef struct {
} ast_struct_member;

//typedef struct {
//  string name;
//  // struct items
//} ast_struct_stat;

// 如何解析嵌套又嵌套的结构体？
// a.b.c = 1
// 或者说
// a[b][c] =1
typedef struct {

  ast_expr expr;
} ast_struct_assign_stat;

typedef struct {
  string name;
  int return_type; // 动态类型？数组？
  ast_literal_expr formal_parameters[UINT8_MAX]; // 形参列表
  ast_block_stat body; // 函数体
} ast_function_decl; // 既可以是 expression,也可以是 statement

#endif //NATURE_SRC_AST_H_
