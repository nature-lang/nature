#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "value.h"

#define AST_BASE_TYPE_FALSE "false"
#define AST_BASE_TYPE_TRUE "true"

typedef enum {
  AST_BASE_TYPE_INT,
  AST_BASE_TYPE_FLOAT,
  AST_BASE_TYPE_BOOL,
  AST_BASE_TYPE_STRING
} ast_base_type;

typedef enum {
  AST_EXPR_TYPE_LITERAL,
  AST_EXPR_TYPE_BINARY,
  AST_EXPR_TYPE_IDENT,
  AST_EXPR_TYPE_OBJ_PROPERTY,
  AST_EXPR_TYPE_ENV_INDEX,
} ast_expr_type;

typedef enum {
  AST_EXPR_OPERATOR_ADD,
  AST_EXPR_OPERATOR_SUB,
  AST_EXPR_OPERATOR_MUL,
  AST_EXPR_OPERATOR_DIV
} ast_expr_operator;

typedef enum {
  AST_STMT_VAR_DECL,
  AST_STMT_VAR_DECL_ASSIGN,
  AST_STMT_ASSIGN,
  AST_STMT_IF,
  AST_STMT_FUNCTION_DECL,
  AST_STMT_CLOSURE_DECL,
} ast_stmt_type;

typedef struct {
  int8_t type; // 声明语句类型
  void *stmt;
} ast_stmt;

typedef struct {
  int8_t type; // 表达式类型
  void *expr;
} ast_expr;

typedef struct {
  size_t count; // 当前数量, unsigned int
  size_t capacity; // 容量
  ast_stmt *list;
} ast_block_stmt;
void ast_init_block_stmt(ast_block_stmt *block);
void ast_insert_block_stmt(ast_block_stmt *block, ast_stmt stmt);

// 值类型
typedef struct {
  int8_t type;
  string value;
} ast_literal;

typedef string ast_ident;

// TODO 没必要
//typedef struct {
//  void *type; // 类型引用，主要是结构体引用
//  int size; // 数据结构字长 至少1个字
//  bool built_in; // 是否内置类型
//  string type_name; // 数据类型名称
//  string name; // 标识符名称
//} ast_ident_expr;

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
  string ident;
  ast_expr actual_params[UINT8_MAX];
  uint8_t actual_param_count;
} ast_call_function;

typedef struct {
  ast_expr condition;
  ast_block_stmt consequent;
  ast_block_stmt alternate;
} ast_if_stmt;

// int a;
typedef struct {
  string ident;
  string type; // 系统 type 或者自定义 struct
} ast_var_decl_stmt;

typedef struct {
  ast_expr left; // a  或 foo.bar.car
  ast_expr right;
} ast_assign_stmt;

typedef struct {
  string type; // 系统 type 字符或者自定义 struct 字符
  string ident;
  ast_expr expr;
} ast_var_decl_assign_stmt;

typedef struct {
  ast_expr expr;
} ast_return_stmt;

typedef struct {
  int8_t type;
  string name; // property name
} ast_struct_property;

typedef struct {
  string ident; // struct 名称
  ast_struct_property list[INT8_MAX];
} ast_struct_decl; // 多个 property 组成一个

typedef struct {
  ast_expr obj; // 可以是 identity 也可以是 struct property
  ast_ident property; // identity 的含义是啥
} ast_obj_property;

typedef struct {
  ast_ident env;
  uint8_t index;
} ast_env_index;

//typedef struct {
//  string name;
//  // struct items
//} ast_struct_stmt;

typedef struct {
  string type;
  string ident;
} formal_param;

typedef struct {
  string name;
  int return_type; // 动态类型？数组？
  formal_param formal_params[UINT8_MAX]; // 形参列表(约定第一个参数为 env)
  uint8_t formal_param_count;
  ast_block_stmt body; // 函数体
} ast_function_decl; // 既可以是 expression,也可以是 stmt

typedef struct {
  ast_expr env[UINT8_MAX]; // env[n] 可以是 local var/或者是形参 param_env_2233[n]
  ast_function_decl *function;
} ast_closure_decl;

#endif //NATURE_SRC_AST_H_
