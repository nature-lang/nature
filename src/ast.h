#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>

#include "package.h"
#include "types.h"
#include "utils/assertf.h"
#include "utils/ct_list.h"
#include "utils/slice.h"
#include "utils/stack.h"
#include "utils/table.h"
#include "utils/type.h"

typedef enum {
    AST_EXPR_LITERAL = 1,// 常数值 => 预计将存储在 data 段中
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_IDENT,
    AST_EXPR_AS,
    AST_EXPR_IS,

    // marco
    AST_MACRO_EXPR_SIZEOF,
    AST_MACRO_EXPR_REFLECT_HASH,
    AST_MACRO_EXPR_TYPE_EQ,
    AST_MACRO_CO_ASYNC,

    AST_EXPR_NEW,// new person

    AST_EXPR_MAP_ACCESS,
    AST_EXPR_VEC_ACCESS,
    AST_EXPR_ARRAY_ACCESS,
    AST_EXPR_TUPLE_ACCESS,

    AST_EXPR_STRUCT_SELECT,
    AST_EXPR_VEC_SELECT,// [1, 2, 3].push(3)
    AST_EXPR_MAP_SELECT,// [1, 2, 3].push(3)
    AST_EXPR_SET_SELECT,// [1, 2, 3].push(3)

    AST_EXPR_ENV_ACCESS,

    AST_EXPR_VEC_NEW,        // [1, 2, 3]
    AST_EXPR_ARRAY_NEW,      // [1, 2, 3]
    AST_EXPR_EMPTY_CURLY_NEW,// {}
    AST_EXPR_MAP_NEW,        // {"a": 1, "b": 2}
    AST_EXPR_SET_NEW,        // {1, 2, 3, 4}
    AST_EXPR_TUPLE_NEW,      // (1, 1.1, true)
    AST_EXPR_TUPLE_DESTR,    // (var_a, var_b, (var_c, var_d))
    AST_EXPR_STRUCT_NEW,     // person {a = 1; b = 2}
    AST_EXPR_TRY,
    AST_EXPR_BOOM,

    // 抽象复合类型
    AST_EXPR_ACCESS,
    AST_EXPR_SELECT,
    AST_VAR_DECL,

    // stmt
    AST_STMT_EXPR_FAKE,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_IMPORT,
    AST_STMT_VARDEF,
    AST_STMT_VAR_TUPLE_DESTR,
    AST_STMT_ASSIGN,
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_THROW,
    AST_STMT_TRY_CATCH,
    AST_STMT_LET,
    AST_STMT_FOR_ITERATOR,
    AST_STMT_FOR_COND,
    AST_STMT_FOR_TRADITION,
    AST_STMT_TYPE_ALIAS,
    AST_CALL,
    AST_CATCH,
    AST_FNDEF,           // fn def (其包含 body)
    AST_STMT_ENV_CLOSURE,// closure def

    AST_MACRO_CALL,
} ast_type_t;

typedef enum {
    // ARITHMETIC 运算
    AST_OP_ADD,// +
    AST_OP_SUB,// -
    AST_OP_MUL,// *
    AST_OP_DIV,// /
    AST_OP_REM,// %

    // unary
    AST_OP_NOT, // unary bool !right, right must bool
    AST_OP_NEG, // unary number -right
    AST_OP_BNOT,// unary binary ~right, right must int
    AST_OP_LA,  // load addr &var
    AST_OP_IA,  // indirect addr  *解引用

    // 位运算
    AST_OP_AND,
    AST_OP_OR,
    AST_OP_XOR,
    AST_OP_LSHIFT,
    AST_OP_RSHIFT,

    AST_OP_LT,// <
    AST_OP_LE,// <=
    AST_OP_GT,// >
    AST_OP_GE,// >=
    AST_OP_EE,// ==
    AST_OP_NE,// !=

    AST_OP_AND_AND,// &&
    AST_OP_OR_OR,  // ||

} ast_expr_op_t;

static string ast_expr_op_str[] = {
        [AST_OP_ADD] = "+",
        [AST_OP_SUB] = "-",
        [AST_OP_MUL] = "*",
        [AST_OP_DIV] = "/",
        [AST_OP_REM] = "%",

        [AST_OP_AND] = "&",
        [AST_OP_OR] = "|",
        [AST_OP_XOR] = "^",
        [AST_OP_BNOT] = "~",
        [AST_OP_LSHIFT] = "<<",
        [AST_OP_RSHIFT] = ">>",

        [AST_OP_LT] = "<",
        [AST_OP_LE] = "<=",
        [AST_OP_GT] = ">", // >
        [AST_OP_GE] = ">=",// >=
        [AST_OP_EE] = "==",// ==
        [AST_OP_NE] = "!=",// !=
        [AST_OP_OR_OR] = "||",
        [AST_OP_AND_AND] = "&&",

        [AST_OP_NOT] = "!",// unary !right
        [AST_OP_NEG] = "-",// unary -right
};

struct ast_stmt_t {
    int line;
    int column;
    bool error;

    ast_type_t assert_type;// 声明语句类型
    void *value;
};

typedef struct {
    int line;
    int column;
    ast_type_t assert_type;// 表达式断言
    type_t type;           // 表达式自身的类型
    type_t target_type;    // 表达式赋值的目标的 type
    void *value;
} ast_expr_t;

typedef struct {
    ast_expr_t *expr;
} ast_continue_t;

typedef struct {
} ast_break_t;

typedef struct {
    char *literal;
} ast_ident;

typedef struct {
    type_t type;
    union {                // type 是 struct 时可以携带一定的参数, 该参数目前不开放给用户，但是可以方便编译器添加默认值
        list_t *properties;// *struct_property_t
    };
} ast_new_expr_t;

/**
 * a as int
 */
typedef struct {
    type_t target_type;
    ast_expr_t src;// 将表达式转换成 target_type
} ast_as_expr_t, ast_is_expr_t;

typedef struct {
    type_t target_type;
} ast_macro_sizeof_expr_t;

typedef struct {
    type_t left_type;
    type_t right_type;
} ast_macro_type_eq_expr_t;

typedef struct {
    type_t target_type;
} ast_macro_reflect_hash_expr_t;

// 调用函数
typedef struct {
    type_t return_type;// call return type 冗余
    ast_expr_t left;

    list_t *generics_args;// type_t

    list_t *args;// *ast_expr
    bool spread;
} ast_call_t;

typedef struct {
    ast_fndef_t *closure_fn;
    ast_fndef_t *closure_fn_void;
    ast_call_t *origin_call;// 未封闭之前的 call

    ast_expr_t *flag_expr;
    type_t return_type;
} ast_macro_co_async_t;

// 一元表达式
typedef struct {
    ast_expr_op_t operator;// 取反，取绝对值, 解引用等,取指针，按位取反
    ast_expr_t operand;    // 操作对象
} ast_unary_expr_t;

// 二元表达式
typedef struct {
    ast_expr_op_t operator;// +/-/*// 等 二元表达式
    ast_expr_t right;
    ast_expr_t left;
} ast_binary_expr_t;

typedef enum {
    MACRO_ARG_KIND_STMT = 1,
    MACRO_ARG_KIND_EXPR,
    MACRO_ARG_KIND_TYPE,
} ast_macro_arg_kind_t;

typedef struct {
    ast_macro_arg_kind_t kind;
    union {
        ast_stmt_t *stmt;
        ast_expr_t *expr;
        type_t *type;
    };
} ast_macro_arg_t;

typedef struct {
    char *ident;
    list_t *args;// ast_macro_arg_t
} ast_macro_call_t;

// 值类型
typedef struct {
    type_kind kind;
    char *value;
} ast_literal_t;// 标量值

// (xx, xx, xx)
typedef struct {
    // var 中, ast_expr 的 type 是  ast_var_decl 和 ast_tuple_destr
    // assign 中 (a, b, (c.e, d[0])) = (1, 2) ast_expr 可能是所有 operand 类型，包括 ast_tuple_destr 自身
    list_t *elements;// ast_expr
} ast_tuple_destr_t;

typedef struct {
    ast_expr_t left;// a  或 foo.bar.car 或者 d[0] 或者 (xx, xx, xx)
    ast_expr_t right;
} ast_assign_stmt_t;

// 仅仅包含了声明
// int a;
typedef struct {
    char *ident;
    type_t type;// type 已经决定了 size
} ast_var_decl_t;

// 包含了声明与赋值，所以统称为定义
typedef struct {
    ast_var_decl_t var_decl;// 左值
    ast_expr_t right;       // 右值
} ast_vardef_stmt_t;

typedef struct {
    ast_expr_t expr;
} ast_expr_fake_stmt_t;

// 基于 tuple 解构语法的变量快速赋值
// var (a, b, (c, d)) = (1, 2)
// 通过 tuple destruct 快速定义变量
typedef struct {
    ast_tuple_destr_t *tuple_destr;
    ast_expr_t right;
} ast_var_tuple_def_stmt_t;

typedef struct {
    ast_expr_t condition;
    slice_t *consequent;// ast_stmt
    slice_t *alternate;
} ast_if_stmt_t;

/**
 * for (true) {}
 */
typedef struct {
    ast_expr_t condition;
    slice_t *body;
} ast_for_cond_stmt_t;

/**
 * throw "not found"
 */
typedef struct {
    ast_expr_t error;
} ast_throw_stmt_t;

/**
 * let foo as [i8]
 */
typedef struct {
    ast_expr_t expr;// must as expr
} ast_let_t;

typedef struct {
    ast_expr_t expr;// expr type must union type
} ast_boom_t;

/**
 * int a = b[1].c() catch err {}
 * a.b = d() catch err {}
 *
 * expr() catch err {}
 */
typedef struct {
    ast_expr_t try_expr;
    ast_var_decl_t catch_err;
    slice_t *catch_body;
} ast_catch_t;

/**
 * try {
 *  stmt1()
 *  stmt2()
 *  ...
 * } catch err {
 *  stmt1()
 * }
 *
 *
 *
 */
typedef struct {
    slice_t *try_body;
    ast_var_decl_t catch_err;
    slice_t *catch_handle;
} try_catch_stmt_t;

/**
 * for (int i = 0; i < 100; i++) {}
 */
typedef struct {
    ast_stmt_t *init;
    ast_expr_t cond;
    ast_stmt_t *update;
    slice_t *body;
} ast_for_tradition_stmt_t;

/**
 * for (key,value in list)
 * for (key in list)
 */
typedef struct {
    ast_expr_t iterate;    // list, foo.list, bar[0]
    ast_var_decl_t first;  // 类型推导, type 可能是 int 或者 string
    ast_var_decl_t *second;// value 可选，可能为 null
    slice_t *body;
} ast_for_iterator_stmt_t;

typedef struct {
    ast_expr_t *expr;
} ast_return_stmt_t;

// import "module_path" module_name alias
typedef struct {
    // file or package one of the two
    char *file;          // import 'xxx' or
    slice_t *ast_package;// a.b.c.d package 字符串数组
    char *as;            // import "foo/bar" as xxx, import 别名，没有别名则使用 bar 作为名称

    // 通过上面的 file 或者 package 解析出的完整 package 路径
    // full_path 对应的 module 会属于某一个 package, 需要记录一下对应的 package conf, 否则单凭一个 full_path 还不足以定位到
    // 对应的 package.toml
    uint8_t module_type;
    char *full_path;
    toml_table_t *package_conf;
    char *package_dir;// 这也是 import module 的 workdir
    bool use_links;

    char *module_ident;// 在符号表中的名称前缀,基于 full_path 计算出来当 unique ident
} ast_import_t;

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
    char *ident;// ident 冗余

    // parser 阶段是 typedef ident
    // infer 完成后是 typeuse_struct
    type_t type;

    list_t *properties;// struct_property_t
} ast_struct_new_t;

// 1. a.b
// 2. a.c.b
// 3. a[1].b
// 4. a().b
// ...
// a_233().b 左值可以编译成
// call a_233;
// type.b = 12; ??
// module_name.test
typedef struct {
    ast_expr_t left;// left is struct or package
    string key;
} ast_select_t;

typedef struct {
    ast_expr_t instance;// type is ptr<struct> or struct
    string key;
    struct_property_t *property;// 冗余方便计算
} ast_struct_select_t;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
    type_t element_type;// 访问的 value 的类型
    ast_expr_t left;
    ast_expr_t index;
} ast_vec_access_t, ast_array_access_t;

typedef struct {
    type_t element_type;// index 对应的 value 的 type
    ast_expr_t left;
    uint64_t index;
} ast_tuple_access_t;

typedef struct {
    type_t key_type;
    type_t value_type;

    ast_expr_t left;
    ast_expr_t key;
} ast_map_access_t;

// foo.bar[key()], bar[]
typedef struct {
    ast_expr_t left;// string/list/map/set/tuple
    ast_expr_t key;
} ast_access_t;

// [1,a.b, call()]
typedef struct {
    list_t *elements;// ast_expr
} ast_array_new_t;

typedef struct {
    list_t *elements;// ast_expr, nullable
    ast_expr_t *len; // ast_expr, nullable
    ast_expr_t *cap; // ast_expr, nullable
} ast_vec_new_t;

typedef struct {
    ast_expr_t key;
    ast_expr_t value;
} ast_map_element_t;

// {key: value}
// var s = {1, 2, 3, call(), xxx}
typedef struct {
    list_t *elements;// ast_map_element or ast_expr
} ast_map_new_t, ast_set_new_t, ast_empty_curly_new_t;

// var s = (1, 2, 2.14, 1.15, true)
typedef struct {
    list_t *elements;// 值为 ast_expr
} ast_tuple_new_t;

typedef struct {
    uint8_t index;
    char *unique_ident;
} ast_env_access_t;

/**
 * type my_int = int
 * type my_string =  string
 * type my_my_string =  my_string
 * type my = struct {}
 */
typedef struct {
    string ident;  // my_int (自定义的类型名称)
    list_t *params;// ast_ident*|null
    type_t type;   // int (类型)
} ast_type_alias_stmt_t;

// 这里包含 body, 所以属于 def
struct ast_fndef_t {
    // 可执行文件中的 label, symbol 中表现为 FN 类型，但是如果 fn 引用了外部的环境，则不能直接调用该 fn, 需要将 fn 关联的环境一起传递进来
    char *symbol_name;
    // 闭包处理中的的 var name, 可能为 null
    // 其通过 jit 封装了一份完整的执行环境，并将环境通过 last param 传递给 symbol name 对应的函数 body 部分
    char *closure_name;
    type_t return_type;
    list_t *params;// ast_var_decl_t*
    bool rest_param;
    slice_t *body;// ast_stmt* 函数体
    void *closure;// closure 数据冗余

    bool is_co_async;// coroutine closure fn, default is false


    // 作为一个 generics fn, 泛型过程中需要分配具体的参数组合，直接使用 key/value type 进行分配即可
    //    slice_t *generics_assigns;// value 是一个 table，保存了具体的 param 对应的 arg 参数
    // 泛型 tpl 函数使用，记录当前 tpl 已经特化了的参数
    table_t *generics_hash_table; // 避免重复写入到 generics_assigns
    slice_t *generics_special_fns;// 当前模板的特化函数列表

    // infer call 时具体为 generics_params 分配的 args, 不需要单独初始化
    table_t *generics_args_table;
    char *generics_args_hash;// 基于 gen args 计算的 hash 值，可以唯一标记当前函数

    // ast_ident 泛型参数, fn list_first<T, U>
    list_t *generics_params;
    type_t impl_type;

    // 由于 infer_fndef 会延迟完成，所以还需要记录一下 type_param_table
    table_t *type_param_table;// 只有顶层 type alias 才能够使用 param, key 是 param_name, value 是具体的类型值

    // ast_expr, 当前 fn body 中引用的外部的环境
    // 这是 parent 视角中的表达式，在 parent 中创建 child fn 时，如果发现 child fn 引用当前作用域中的变量
    // 则需要将当前作用域中的变量打包成 env 丢给 child fn
    list_t *capture_exprs;
    // local_ident_t* 当前函数中是否存在被 child 引用的变量
    slice_t *be_capture_locals;

    // analyzer stage, 当 fn 定义在 struct 中,用于记录 struct type, 其是一个 ptr<struct>
    type_t *self_struct_ptr;

    type_t type;// 类型冗余一份

    // 默认为 null, 当前函数为泛型 fn 时才会有值，local fn 同样有值且和 global fn 同值
    // key is generic->ident, value is *type_t
    table_t *generic_assign;

    // 默认为 null，如果是 local fn, 则指向定义的 global fn
    struct ast_fndef_t *global_parent;

    // 仅当前 fn 为 global fn 时才有可能存在一个或者多个 child_fndefs
    slice_t *local_children;
    // analyzer 时赋值
    bool is_local;   // 是否是全局函数
    bool is_tpl;     // 是否是 tpl 函数
    bool is_generics;// 是否是泛型

    // catch err { continue 12 }
    ct_stack_t *continue_target_types;

    // dump error
    char *fn_name;
    char *rel_path;
    int column;
    int line;
    module_t *module;// module 绑定
};

ast_ident *ast_new_ident(char *literal);
ast_fndef_t *ast_fndef_copy(module_t *m, ast_fndef_t *temp);
ast_expr_t *ast_expr_copy(module_t *m, ast_expr_t *temp);

static inline ast_expr_t *ast_ident_expr(int line, int column, char *literal) {
    ast_expr_t *expr = NEW(ast_expr_t);
    expr->assert_type = AST_EXPR_IDENT;
    expr->value = ast_new_ident(literal);
    expr->line = line;
    expr->column = column;
    return expr;
}

static inline ast_expr_t *ast_int_expr(int line, int column, uint64_t number) {
    ast_expr_t *expr = NEW(ast_expr_t);
    expr->assert_type = AST_EXPR_LITERAL;
    expr->line = line;
    expr->column = column;
    ast_literal_t *literal = NEW(ast_literal_t);
    literal->kind = TYPE_INT;
    literal->value = itoa(number);
    expr->value = literal;
    return expr;
}

static inline ast_expr_t *ast_bool_expr(int line, int column, bool b) {
    ast_expr_t *expr = NEW(ast_expr_t);
    expr->assert_type = AST_EXPR_LITERAL;
    expr->line = line;
    expr->column = column;
    ast_literal_t *literal = NEW(ast_literal_t);
    literal->kind = TYPE_BOOL;
    if (b) {
        literal->value = "true";
    } else {
        literal->value = "false";
    }
    expr->value = literal;
    return expr;
}

static inline ast_expr_t *ast_unary(ast_expr_t *target, ast_expr_op_t unary_op) {
    ast_expr_t *result = NEW(ast_expr_t);

    ast_unary_expr_t *expr = NEW(ast_unary_expr_t);
    expr->operand = *target;
    expr->operator= unary_op;

    result->line = target->line;
    result->column = target->column;
    result->assert_type = AST_EXPR_UNARY;
    result->value = expr;
    result->type = type_ptrof(target->type);
    return result;
}

/**
 * 已经 infer 过了
 * @param expr
 * @param target_type
 * @return
 */
static inline ast_expr_t ast_type_as(ast_expr_t expr, type_t target_type) {
    assertf(target_type.status == REDUCTION_STATUS_DONE, "target type not reduction");
    ast_expr_t *result = NEW(ast_expr_t);

    ast_as_expr_t *convert = NEW(ast_as_expr_t);
    convert->src = expr;
    convert->target_type = target_type;

    result->assert_type = AST_EXPR_AS;
    result->value = convert;
    result->type = target_type;
    return *result;
}

/**
 * 部分运算符只支持 int 类型数值运算，此时数值类型提升也拯救不了该运算
 * @return
 */
static inline bool is_integer_operator(ast_expr_op_t op) {
    return op == AST_OP_REM || op == AST_OP_LSHIFT || op == AST_OP_RSHIFT || op == AST_OP_AND || op == AST_OP_OR || op == AST_OP_XOR ||
           op == AST_OP_BNOT;
}

static inline bool can_assign(ast_type_t t) {
    if (t == AST_EXPR_IDENT || t == AST_EXPR_ACCESS || t == AST_EXPR_SELECT || t == AST_EXPR_MAP_ACCESS || t == AST_EXPR_VEC_ACCESS ||
        t == AST_EXPR_ENV_ACCESS || t == AST_EXPR_STRUCT_SELECT) {
        return true;
    }
    return false;
}

static inline ast_fndef_t *ast_fndef_new(module_t *m, int line, int column) {
    ast_fndef_t *fndef = NEW(ast_fndef_t);
    fndef->is_co_async = false;
    fndef->module = m;
    fndef->rel_path = m->rel_path;
    fndef->symbol_name = NULL;
    fndef->closure_name = NULL;
    fndef->line = line;
    fndef->column = column;
    fndef->local_children = slice_new();
    fndef->continue_target_types = stack_new();
    fndef->generics_params = NULL;
    return fndef;
}

#endif// NATURE_SRC_AST_H_
