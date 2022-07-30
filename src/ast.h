#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "src/value.h"
#include "type.h"

#define AST_BASE_TYPE_FALSE "false"
#define AST_BASE_TYPE_TRUE "true"
#define AST_BASE_TYPE_NULL "null"
#define AST_VAR "var"

typedef enum {
    AST_COMPLEX_TYPE_CLOSURE,
    AST_COMPLEX_TYPE_ENV,
} ast_complex_type;

typedef enum {
    AST_EXPR_LITERAL = 1, // 常数值 => 预计将存储在 data 段中
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_IDENT,
    AST_EXPR_SELECT_PROPERTY,
    AST_EXPR_ACCESS_ENV,
    AST_EXPR_ACCESS_MAP,
    AST_EXPR_ACCESS_LIST,

    AST_EXPR_NEW_MAP, // {"a": 1, "b": 2}
    AST_EXPR_NEW_ARRAY, // [1, 2, 3]
    AST_EXPR_STRUCT_DECL, // struct {int a = 1; int b = 2}
    AST_EXPR_NEW_STRUCT, // person {a = 1; b = 2}

    // 抽象复合类型
    AST_EXPR_ACCESS,
    AST_EXPR_SELECT,
    AST_VAR_DECL,

    // stmt
    AST_STMT_VAR_DECL_ASSIGN,
    AST_STMT_ASSIGN,
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_FOR_IN,
    AST_STMT_WHILE,
    AST_STMT_TYPE_DECL,
//  AST_STRUCT_DECL,
    AST_CALL,
    AST_NEW_FN,
    AST_NEW_CLOSURE,
} ast_stmt_expr_type;

typedef enum {
    AST_EXPR_OPERATOR_ADD,
    AST_EXPR_OPERATOR_SUB,
    AST_EXPR_OPERATOR_MUL,
    AST_EXPR_OPERATOR_DIV,

    AST_EXPR_OPERATOR_LT,
    AST_EXPR_OPERATOR_LTE,
    AST_EXPR_OPERATOR_GT, // >
    AST_EXPR_OPERATOR_GTE,  // >=
    AST_EXPR_OPERATOR_EQ_EQ, // ==
    AST_EXPR_OPERATOR_NOT_EQ, // !=

    AST_EXPR_OPERATOR_NOT, // unary !expr
    AST_EXPR_OPERATOR_NEG, // unary -expr
    AST_EXPR_OPERATOR_IA, // *解引用
} ast_expr_operator;

string ast_expr_operator_to_string[100];

typedef struct {
    void *value; // ast_ident(type_decl_ident),ast_map_decl*....
    type_system type; // base_type, custom_type, function, list, map
    bool is_origin; // type a = int, type b = a，int is origin
    uint8_t point; // 指针等级, 如果等于0 表示非指针
} ast_type_t;

typedef struct {
    int line; // 行号
    ast_stmt_expr_type type; // 声明语句类型
    void *stmt;
} ast_stmt;

typedef struct {
    int line;
    ast_stmt_expr_type type; // 表达式类型
    ast_type_t ast_type;
    void *expr;
} ast_expr;

typedef struct {
    size_t count; // 当前数量, unsigned int
    size_t capacity; // 容量
    ast_stmt *list;
} ast_block_stmt;

typedef struct {
    string literal;
} ast_ident;

// TODO 没必要,都去符号表取找吧
//typedef struct {
//  void *type; // 类型引用，主要是结构体引用
//  int size; // 数据结构字长 至少1个字
//  bool built_in; // 是否内置类型
//  string type_name; // 数据类型名称
//  string name; // 标识符名称
//} ast_ident_expr;

// 一元表达式
typedef struct {
    ast_expr_operator operator; // 取反，取绝对值, 解引用等
    ast_expr operand; // 操作对象
} ast_unary_expr;

// 二元表达式
typedef struct {
    ast_expr_operator operator; // +/-/*// 等 二元表达式
    ast_expr right;
    ast_expr left;
} ast_binary_expr;

// 调用函数
typedef struct {
    ast_expr left;
    ast_expr actual_params[UINT8_MAX];
    uint8_t actual_param_count;
} ast_call;

// int a;
typedef struct {
    string ident;
    ast_type_t type; // ast_type 已经决定了 size
} ast_var_decl;

// 值类型
typedef struct {
    type_system type;
    string value;
} ast_literal; // 标量值

typedef struct {
    ast_expr left; // a  或 foo.bar.car 或者 d[0]
    ast_expr right;
} ast_assign_stmt;

typedef struct {
    ast_var_decl *var_decl;
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

/**
 * for (int key[, bool value] in list) {
 *
 * }
 */
typedef struct {
    ast_expr iterate; // list, foo.list, bar[0]
    ast_var_decl *gen_key; // 类型推导, type 可能是 int 或者 string
    ast_var_decl *gen_value; // 类型推导
    ast_block_stmt body;
} ast_for_in_stmt;

typedef struct {
    ast_expr *expr;
} ast_return_stmt;

typedef struct {
    ast_type_t type;
    string key;
    ast_expr value;
    size_t length; // byte
} ast_struct_property;

typedef struct {
    ast_struct_property list[INT8_MAX];
    int8_t count;
} ast_struct_decl; // 多个 property 组成一个

typedef struct {
    ast_type_t type; // 为什么这里声明的是一个类型而不是 ident?
    ast_struct_property list[INT8_MAX];
    int8_t count;
} ast_new_struct;

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
    ast_expr left; // left is struct
    string property;

    ast_struct_decl *struct_decl; // 指针引用
    ast_struct_property *struct_property; // 指针引用
} ast_select_property;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
    ast_type_t type; // list的类型
    ast_expr left;
    ast_expr index;
} ast_access_list;

typedef struct {
    ast_type_t key_type;
    ast_type_t value_type;

    ast_expr left;
    ast_expr key;
} ast_access_map;

typedef struct {
//  string access_type; // map or list
//  string key_type;
//  string value_type;

    ast_expr left;
    ast_expr key;
} ast_access; // foo.bar[key()], bar[]

// [1,a.b, call()]
typedef struct {
    ast_expr values[UINT8_MAX]; // TODO dynamic
    uint64_t count; // count
    ast_type_t ast_type; // list的类型 (类型推导截断冗余)
} ast_new_list;

// [int,5]
typedef struct {
    ast_type_t ast_type;
    uint64_t count; // 可选，初始化声明大小
} ast_array_decl;

// map{int:int}
typedef struct {
    ast_type_t key_type;
    ast_type_t value_type;
} ast_map_decl;

typedef struct {
    ast_expr key;
    ast_expr value;
} ast_map_item;

// {key: value}
typedef struct {
    ast_map_item values[UINT8_MAX];
    uint64_t count; // 默认初始化的数量
    uint64_t capacity; // 初始容量
    ast_type_t key_type; // 类型推导截断冗余
    ast_type_t value_type; // 类型推导截断冗余
} ast_new_map;

// 改写后是否需要添加类型系统支持？需要，不然怎么过类型推导
typedef struct {
    ast_ident *env;
    uint8_t index;
    string unique_ident;
} ast_access_env;

//typedef struct {
//  string name;
//  // struct items
//} ast_struct_stmt;

/**
 * type my_int = int
 * type my_string =  string
 * type my_my_string =  my_string
 * type my = struct {}
 */
typedef struct {
    string ident; // foo
    ast_type_t type; // int
} ast_type_decl_stmt;

typedef struct {
//    string name;
    ast_type_t return_type; // 基础类型 + 动态类型
    ast_var_decl *formal_params[UINT8_MAX]; // 形参列表(约定第一个参数为 env)
    uint8_t formal_param_count;
} ast_function_type_decl;

typedef struct {
    string name;
    ast_type_t return_type; // 基础类型 + 动态类型
    ast_var_decl *formal_params[UINT8_MAX]; // 形参列表(约定第一个参数为 env)
    uint8_t formal_param_count;
    ast_block_stmt body; // 函数体
} ast_new_fn; // 既可以是 expression,也可以是 stmt

typedef struct {
    ast_expr env[UINT8_MAX]; // env[n] 可以是 local var/或者是形参 param_env_2233[n]
    uint8_t env_count;
    string env_name; // 唯一标识，可以全局定位
    ast_new_fn *function;
} ast_closure_decl;

ast_block_stmt ast_new_block_stmt();

void ast_block_stmt_push(ast_block_stmt *block, ast_stmt stmt);

ast_type_t ast_new_simple_type(type_system type);

ast_type_t ast_new_point_type(ast_type_t ast_type, uint8_t point);

ast_ident *ast_new_ident(string literal);

#endif //NATURE_SRC_AST_H_
