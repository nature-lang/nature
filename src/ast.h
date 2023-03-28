#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "utils/value.h"
#include "utils/slice.h"
#include "utils/ct_list.h"
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

    AST_EXPR_MAP_ACCESS,
    AST_EXPR_LIST_ACCESS,
    AST_EXPR_TUPLE_ACCESS,
    AST_EXPR_STRUCT_ACCESS,

    AST_EXPR_ENV_VALUE,

    AST_EXPR_LIST_NEW, // [1, 2, 3]
    AST_EXPR_CURLY_EMPTY, // 类型推断阶段再去判定对错,TYPE_UNKNOWN
    AST_EXPR_MAP_NEW, // {"a": 1, "b": 2}
    AST_EXPR_SET_NEW, // {1, 2, 3, 4}
    AST_EXPR_TUPLE_NEW, // (1, 1.1, true)
    AST_EXPR_TUPLE_DESTR, // (var_a, var_b)
    AST_EXPR_STRUCT_NEW, // person {a = 1; b = 2}
    AST_EXPR_CATCH, // catch call()


    AST_EXPR_STRUCT_DECL, // struct {int a = 1; int b = 2}
    // 抽象复合类型
    AST_EXPR_ACCESS,
    AST_EXPR_SELECT,
    AST_VAR_DECL,

    // stmt
    AST_STMT_IMPORT,
    AST_STMT_VAR_DECL_ASSIGN,
    AST_STMT_VAR_TUPLE_DESTR,
    AST_STMT_ASSIGN,
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_THROW,
    AST_STMT_FOR_ITERATOR,
    AST_STMT_FOR_COND,
    AST_STMT_FOR_TRADITION,
    AST_STMT_TYPEDEF,
    AST_CALL,
    AST_FN_DECL,
    AST_CLOSURE_NEW,
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

    AST_EXPR_OPERATOR_NOT, // unary !right
    AST_EXPR_OPERATOR_NEG, // unary -right
    AST_EXPR_OPERATOR_IA, // *解引用
} ast_expr_operator_e;

static string ast_expr_op_str[] = {
        [AST_EXPR_OPERATOR_ADD] = "+",
        [AST_EXPR_OPERATOR_SUB] = "-",
        [AST_EXPR_OPERATOR_MUL] = "*",
        [AST_EXPR_OPERATOR_DIV] = "/",

        [AST_EXPR_OPERATOR_LT] = "<",
        [AST_EXPR_OPERATOR_LTE] = "<=",
        [AST_EXPR_OPERATOR_GT] = ">", // >
        [AST_EXPR_OPERATOR_GTE] = ">=",  // >=
        [AST_EXPR_OPERATOR_EQ_EQ] = "==", // ==
        [AST_EXPR_OPERATOR_NOT_EQ] = "!=", // !=

        [AST_EXPR_OPERATOR_NOT] = "!", // unary !right
        [AST_EXPR_OPERATOR_NEG] = "-", // unary -right
};
//
//typedef struct {
//    void *value; // ast_ident(type_decl_ident),ast_map_decl*....
//    type_system code; // base_type, custom_type, function, list, map
//    bool is_origin; // code a = int, code b = a，int is origin
//    uint8_t pointer; // 指针等级, 如果等于0 表示非指针
//} ast_type_t;

typedef struct {
    int line; // 行号
    ast_type_e assert_type; // 声明语句类型
    void *value;
} ast_stmt;

typedef struct {
    int line;
    ast_type_e assert_type; // 表达式断言
    typedecl_t type; // 表达式自身的类型
    typedecl_t target_type; // 表达式赋值的目标的 type
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
//    ast_expr actual_params[UINT8_MAX];
//    uint8_t param_count;
    list_t *actual_params;// ast_expr
    bool spread_param;
} ast_call;

// 值类型
typedef struct {
    type_kind kind;
    string value;
} ast_literal; // 标量值

typedef struct {
    ast_expr left; // a  或 foo.bar.car 或者 d[0]
    ast_expr right;
} ast_assign_stmt;

// int a;
typedef struct {
    string ident;
    typedecl_t type; // type 已经决定了 size
} ast_var_decl;

typedef struct {
    ast_var_decl var_decl; // 左值
    ast_expr right; // 右值
} ast_var_assign_stmt;

// 基于 tuple 解构语法的变量快速赋值
// var (a, b) = (1, 2)
typedef struct {
    list_t *var_decls; // var_decl
    ast_expr right;
} ast_var_tuple_destr_stmt;

typedef struct {
    ast_expr condition;
    slice_t *consequent; // ast_stmt
    slice_t *alternate;
} ast_if_stmt;

/**
 * for (true) {}
 */
typedef struct {
    ast_expr condition;
    slice_t *body;
} ast_for_cond_stmt;

/**
 * throw "not found"
 */
typedef struct {
    ast_expr error;
} ast_throw_stmt;


/**
 * var (res, error) = catch call()
 */
typedef struct {
    ast_call *call; // 必须接上 call
} ast_catch;

/**
 * for (int i = 0; i < 100; i++) {}
 */
typedef struct {
    ast_stmt *init;
    ast_expr cond;
    ast_stmt *update;
    slice_t *body;
} ast_for_tradition_stmt;

/**
 * for (key,value in list) {}
 */
typedef struct {
    ast_expr iterate; // list, foo.list, bar[0]
    ast_var_decl key; // 类型推导, type 可能是 int 或者 string
    ast_var_decl value; // 类型推导
    slice_t *body;
} ast_for_iterator_stmt;

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
    typedecl_t type;
    string key;
    ast_expr value;
    uint8_t size; // byte
} ast_struct_property;

/**
 * 虽然共用了 ast_struct_property, 但是使用的字段是不同的，
 * 使用了 key, value
 * people {
 *    a = 1,
 *    b = 2,
 *    c = 3
 * }
 */
typedef struct {
    // parser 阶段是 typedef ident
    // infer 完成后是 typedecl_struct
    typedecl_t type;
//    ast_struct_property properties[UINT8_MAX];
//    uint8_t count;
    list_t *properties; // ast_struct_property
} ast_struct_new_t;

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
    string key;

    // infer 时在这里冗余一份,计算 size 或者 type 的时候都比较方便
    typedecl_struct_property_t property;
} ast_struct_access;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
    typedecl_t type; // value 的类型吧？
    ast_expr left;
    ast_expr index;
} ast_list_access_t;

typedef struct {
    typedecl_t key_type;
    typedecl_t value_type;

    ast_expr left;
    ast_expr key;
} ast_map_access;

typedef struct {
//  string access_type; // map or list
//  string key_type;
//  string value_type;

    ast_expr left;
    ast_expr key;
} ast_access; // foo.bar[key()], bar[]

// [1,a.b, call()]
typedef struct {
    list_t *values; // ast_expr
    typedecl_t type; // list的类型 (类型推导截断冗余)
} ast_list_new;

typedef struct {
    ast_expr key;
    ast_expr value;
} ast_map_element;

// {key: value}
typedef struct {
    list_t *elements; // ast_map_element
//    typedecl_t key_type; // 类型推导截断冗余
//    typedecl_t value_type; // 类型推导截断冗余
} ast_map_new;

// var s = {1, 2, 3, call(), xxx}
typedef struct {
    list_t *keys; // 值为 ast_expr
} ast_set_new;

// var s = (1, 2, 2.14, 1.15, true)
typedef struct {
    list_t *elements; // 值为 ast_expr
} ast_tuple_new;

// (a, b) = (1, 2)
typedef struct {
    list_t *elements; // 值为 ast_expr
} ast_tuple_destr; // destructuring

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
 * type my_int = int
 * type my_string =  string
 * type my_my_string =  my_string
 * type my = struct {}
 */
typedef struct {
    string ident; // my_int (自定义的类型名称)
    typedecl_t type; // int (类型)
} ast_typedef_stmt;

typedef struct {
    char *name;
    typedecl_t return_type; // 基础类型 + 动态类型
    list_t *formals; // ast_var_decl
    bool rest_param;
    slice_t *body; // ast_stmt* 函数体
    void *closure; // 全局 closure 冗余
} ast_fn_decl; // 既可以是 expression,也可以是 stmt

typedef struct {
//    slice_t *env_list; // ast_expr*
    list_t *env_list; // ast_expr

    string env_name; // 唯一标识，可以全局定位
    ast_fn_decl *fn;
} ast_closure_t;

//ast_block_stmt ast_new_block_stmt();

ast_ident *ast_new_ident(string literal);

typedecl_t select_actual_param(ast_call *call, uint8_t index);

typedecl_t select_formal_param(typedecl_fn_t *formal_fn, uint8_t index);

bool type_compare(typedecl_t a, typedecl_t b);

#endif //NATURE_SRC_AST_H_
