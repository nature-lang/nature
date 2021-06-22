#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "src/value.h"

#define AST_BASE_TYPE_FALSE "false"
#define AST_BASE_TYPE_TRUE "true"

typedef enum {
  AST_BASE_TYPE_INT,
  AST_BASE_TYPE_FLOAT,
  AST_BASE_TYPE_BOOL,
  AST_BASE_TYPE_STRING,
  AST_BASE_TYPE_MAP,
  AST_BASE_TYPE_LIST, // list 本质存储的也是 memory_address 吧？
  AST_BASE_TYPE_CLOSURE,
} ast_base_type;

typedef enum {
  AST_EXPR_TYPE_LITERAL,
  AST_EXPR_TYPE_BINARY,
  AST_EXPR_TYPE_IDENT,
  AST_EXPR_TYPE_ACCESS_STRUCT,
  AST_EXPR_TYPE_ENV_INDEX,
  AST_EXPR_TYPE_ACCESS_MAP,
  AST_EXPR_TYPE_NEW_MAP,
  AST_EXPR_TYPE_ACCESS_LIST,
  AST_EXPR_TYPE_NEW_LIST,
  AST_EXPR_TYPE_LIST_DECL,
  AST_VAR_DECL,
  AST_STMT_VAR_DECL_ASSIGN,
  AST_STMT_ASSIGN,
  AST_STMT_IF,
  AST_STMT_FOR_IN,
  AST_FUNCTION_DECL,
  AST_CALL,
  AST_CLOSURE_DECL,
} ast_stmt_expr_type;

typedef enum {
  AST_EXPR_OPERATOR_ADD,
  AST_EXPR_OPERATOR_SUB,
  AST_EXPR_OPERATOR_MUL,
  AST_EXPR_OPERATOR_DIV
} ast_expr_operator;

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
} ast_literal; // 标量值

// 符合类型值呢？？比如另一个 list

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
  string name;
  ast_expr actual_params[UINT8_MAX];
  uint8_t actual_param_count;
} ast_call;

// int a;
typedef struct {
  string ident;
  string type; // 系统 type 或者自定义 struct
} ast_var_decl;

typedef struct {
  ast_expr left; // a  或 foo.bar.car 或者 d[0]
  ast_expr right;
} ast_assign_stmt;

typedef struct {
  string type; // 系统 type 字符或者自定义 struct 字符
  string ident;
  ast_expr expr;
} ast_var_decl_assign_stmt;

typedef struct {
  ast_expr condition;
  ast_block_stmt consequent;
  ast_block_stmt alternate;
} ast_if_stmt;

typedef struct {
  ast_expr condition;
  ast_block_stmt body;
} ast_while_stmt;

typedef struct {
  ast_expr iterate; // list, foo.list, bar[0]
  ast_var_decl *gen_key; // 类型推导, type 可能是 int 或者 string
  ast_var_decl *gen_value; // 类型推导
  ast_block_stmt body;
} ast_for_in_stmt;

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

// 1. a.b
// 2. a.c.b
// 3. a[1].b
// 4. a().b
// ...
// 中间代码如何表示？a_2233.b = 12
// a_233().b 左值可以编译成
// call a_233;
// type.b = 12; ??
typedef struct {
  ast_expr left;
  string property;
} ast_access_property;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
  string type; // list的类型
  ast_expr left;
  ast_expr index;
} ast_access_list;

// [1,a.b, call()]
typedef struct {
  ast_expr values[UINT8_MAX];
  uint64_t count; // count
  uint64_t capacity; // 初始容量
  string type; // list的类型
} ast_new_list;

// list[int]
typedef struct {
  string type;
} ast_list_decl;

typedef struct {
  string key_type;
  string value_type;
  ast_expr left;
  ast_expr key;
} ast_access_map;

typedef struct {
  ast_expr key;
  ast_expr value;
} ast_map_item;

// {key: value}
typedef struct {
  ast_map_item values[UINT8_MAX];
  uint64_t count; // 默认初始化的数量
  uint64_t capacity; // 初始容量
  string key_type;
  string value_type;
} ast_new_map;

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
