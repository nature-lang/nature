#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "utils/value.h"
#include "utils/slice.h"
#include "utils/table.h"
#include "utils/type.h"

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
    AST_EXPR_ACCESS_MAP,
    AST_EXPR_ENV_VALUE,
    AST_EXPR_LIST_VALUE,

    AST_EXPR_NEW_MAP, // {"a": 1, "b": 2}
    AST_EXPR_NEW_ARRAY, // [1, 2, 3]
    AST_EXPR_STRUCT_DECL, // struct {int a = 1; int b = 2}
    AST_EXPR_NEW_STRUCT, // person {a = 1; b = 2}

    // 抽象复合类型
    AST_EXPR_ACCESS,
    AST_EXPR_SELECT,
    AST_VAR_DECL,

    // stmt
    AST_STMT_IMPORT,
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
} ast_type_e;

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
} ast_expr_operator_e;

string ast_expr_operator_to_string[100];
//
//typedef struct {
//    void *value; // ast_ident(type_decl_ident),ast_map_decl*....
//    type_system code; // base_type, custom_type, function, list, map
//    bool is_origin; // code a = int, code b = a，int is origin
//    uint8_t point; // 指针等级, 如果等于0 表示非指针
//} ast_type_t;

typedef struct {
    int line; // 行号
    ast_type_e assert_type; // 声明语句类型
    void *value;
} ast_stmt;

typedef struct {
    int line;
    ast_type_e assert_type; // 表达式断言
    type_t type; // 表达式自身的类型
    type_t target_type; // 表达式赋值的目标的 type
    void *value;
} ast_expr;

typedef struct {
    size_t count; // 当前数量, unsigned int
    size_t capacity; // 容量
    ast_stmt *list;
} ast_block_stmt;

typedef struct {
    string literal;
} ast_ident;

// 一元表达式
typedef struct {
    ast_expr_operator_e operator; // 取反，取绝对值, 解引用等
    ast_expr operand; // 操作对象
} ast_unary_expr;

// 二元表达式
typedef struct {
    ast_expr_operator_e operator; // +/-/*// 等 二元表达式
    ast_expr right;
    ast_expr left;
} ast_binary_expr;

// 调用函数
typedef struct {
    ast_expr left;
    ast_expr actual_params[UINT8_MAX];
    uint8_t actual_param_count;
    bool spread_param;
} ast_call;

// int a;
typedef struct {
    string ident;
    type_t type; // type 已经决定了 size
} ast_var_decl;

// 值类型
typedef struct {
    type_kind kind;
    string value;
} ast_literal; // 标量值

typedef struct {
    ast_expr left; // a  或 foo.bar.car 或者 d[0]
    ast_expr right;
} ast_assign_stmt;

typedef struct {
    ast_var_decl *var_decl; // 左值
    ast_expr expr; // 右值
} ast_var_decl_assign_stmt;

typedef struct {
    ast_expr condition;
    slice_t *consequent; // ast_stmt
    slice_t *alternate;
} ast_if_stmt;

typedef struct {
    ast_expr condition;
    slice_t *body;
} ast_while_stmt;

/**
 * for (int key[, bool value] in list) {
 *
 * }
 */
typedef struct {
    ast_expr iterate; // list, foo.list, bar[0]
    ast_var_decl *gen_key; // 类型推导, code 可能是 int 或者 string
    ast_var_decl *gen_value; // 类型推导
    slice_t *body;
} ast_for_in_stmt;

typedef struct {
    ast_expr *expr;
} ast_return_stmt;

// import "module_path" module_name alias
typedef struct {
    string path; // import "xxx" 的 xxx 部分
    string as; // import "foo/bar" as xxx 的 xxx 部分  代码中使用都是基于这个 as 的，没有就使用 bra 作为 as

    // 计算得出
    string full_path; // 绝对完整的文件路径
    string module_ident; // 在符号表中的名称前缀,基于 full_path 计算出来
} ast_import;

typedef struct {
    type_t type;
    string key;
    ast_expr value;
    uint8_t size; // byte
} ast_struct_property;

/**
 * 使用了 code, key, size
 * struct {
 *    int a
 *    int b
 *    int c
 * }
 */
typedef struct {
    ast_struct_property list[UINT8_MAX];
    int8_t count;
} ast_struct_decl; // 多个 property 组成一个

/**
 * 虽然共用了 ast_struct_property, 但是使用的字段是不同的，
 * 使用了 key, value
 * people {
 *    a = 1
 *    b = 2
 *    c = 3
 * }
 */
typedef struct {
    type_t type; // 为什么这里声明的是一个类型而不是 ident?
    ast_struct_property list[UINT8_MAX];
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
// code.b = 12; ??
typedef struct {
    ast_expr left; // left is struct
    string property;

    typedecl_struct_t *struct_decl; // 指针引用
    struct_property_t *struct_property; // 指针引用
} ast_select_property;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
    type_t type; // value 的类型吧？
    ast_expr left;
    ast_expr index;
} ast_list_value_t;

typedef struct {
    type_t key_type;
    type_t value_type;

    ast_expr left;
    ast_expr key;
} ast_map_value;

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
    type_t type; // list的类型 (类型推导截断冗余)
} ast_new_list;

// [int,5]
typedef struct {
    type_t type; // 值类型
    uint64_t count; // 可选，初始化声明大小
} ast_list_decl;

// map{int:int}
typedef struct {
    type_t key_type;
    type_t value_type;
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
    type_t key_type; // 类型推导截断冗余
    type_t value_type; // 类型推导截断冗余
} ast_new_map;

typedef struct {
    ast_ident *env;
    uint8_t index;
    string unique_ident;
} ast_env_value;

//typedef struct {
//  string as;
//  // struct items
//} ast_struct_stmt;

/**
 * code my_int = int
 * code my_string =  string
 * code my_my_string =  my_string
 * code my = struct {}
 */
typedef struct {
    string ident; // foo
    type_t type; // int
} ast_type_decl_stmt;

typedef struct {
    string name;
    type_t return_type; // 基础类型 + 动态类型
    ast_var_decl *formal_params[UINT8_MAX]; // 形参列表(约定第一个参数为 env)
    uint8_t formal_param_count;
    bool rest_param;
    slice_t *body; // ast_stmt* 函数体
} ast_new_fn; // 既可以是 expression,也可以是 stmt

typedef struct {
    slice_t *env_list; // ast_expr*

    string env_name; // 唯一标识，可以全局定位
    ast_new_fn *fn;
} ast_closure_t;

//ast_block_stmt ast_new_block_stmt();

ast_ident *ast_new_ident(string literal);

int ast_struct_decl_size(typedecl_struct_t *struct_decl);

int ast_struct_offset(typedecl_struct_t *struct_decl, string property);

type_t select_actual_param(ast_call *call, uint8_t index);

type_t select_formal_param(typedecl_fn_t *formal_fn, uint8_t index);

bool type_compare(type_t a, type_t b);

#endif //NATURE_SRC_AST_H_
