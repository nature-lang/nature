#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>

#include "package.h"
#include "types.h"
#include "utils/assertf.h"
#include "utils/ct_list.h"
#include "utils/sc_map.h"
#include "utils/slice.h"
#include "utils/stack.h"
#include "utils/table.h"
#include "utils/type.h"

extern ast_fndef_t *ast_copy_global;

typedef enum {
    AST_EXPR_LITERAL = 1, // 常数值 => 预计将存储在 data 段中
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_IDENT,
    AST_EXPR_AS,
    AST_EXPR_IS,
    AST_EXPR_MATCH_IS,

    // marco
    AST_MACRO_EXPR_SIZEOF,
    AST_MACRO_EXPR_REFLECT_HASH,
    AST_MACRO_EXPR_TYPE_EQ,
    AST_MACRO_EXPR_DEFAULT,
    AST_MACRO_ASYNC,

    AST_EXPR_NEW, // new person

    AST_EXPR_MAP_ACCESS,
    AST_EXPR_VEC_ACCESS,
    AST_EXPR_VEC_SLICE,
    AST_EXPR_ARRAY_ACCESS,
    AST_EXPR_TUPLE_ACCESS,
    AST_EXPR_STRUCT_SELECT,

    AST_EXPR_ENV_ACCESS,

    AST_EXPR_VEC_NEW, // [1, 2, 3]
    AST_EXPR_ARRAY_NEW, // [1, 2, 3]
    AST_EXPR_VEC_REPEAT_NEW, // [0;12]
    AST_EXPR_ARRAY_REPEAT_NEW, // [0;12]
    AST_EXPR_EMPTY_CURLY_NEW, // {}
    AST_EXPR_MAP_NEW, // {"a": 1, "b": 2}
    AST_EXPR_SET_NEW, // {1, 2, 3, 4}
    AST_EXPR_TUPLE_NEW, // (1, 1.1, true)
    AST_EXPR_TUPLE_DESTR, // (var_a, var_b, (var_c, var_d))
    AST_EXPR_STRUCT_NEW, // person {a = 1; b = 2}
    AST_EXPR_BLOCK,

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
    AST_STMT_CONSTDEF,
    AST_STMT_VAR_TUPLE_DESTR,
    AST_STMT_ASSIGN,
    AST_STMT_GLOBAL_ASSIGN,
    AST_STMT_RET,
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_THROW,
    AST_STMT_LET,
    AST_STMT_FOR_ITERATOR,
    AST_STMT_FOR_COND,
    AST_STMT_FOR_TRADITION,
    AST_STMT_TYPEDEF,
    AST_STMT_SELECT,
    AST_CALL,
    AST_STMT_TRY_CATCH,
    AST_CATCH,
    AST_MATCH,
    AST_FNDEF, // fn def (其包含 body)
    AST_STMT_ENV_CLOSURE, // closure def

    AST_MACRO_CALL,
} ast_type_t;

typedef enum {
    // ARITHMETIC 运算
    AST_OP_ADD, // +
    AST_OP_SUB, // -
    AST_OP_MUL, // *
    AST_OP_DIV, // /
    AST_OP_REM, // %

    // unary
    AST_OP_NOT, // unary bool !right, right must bool
    AST_OP_NEG, // unary number -right
    AST_OP_BNOT, // unary binary ~right, right must int
    AST_OP_LA, // &var = load addr var
    AST_OP_SAFE_LA, // @sla(var) safe load addr
    AST_OP_UNSAFE_LA, // @ula(var) unsafe load addr
    AST_OP_IA, // indirect addr  *q

    // 位运算
    AST_OP_AND,
    AST_OP_OR,
    AST_OP_XOR,
    AST_OP_LSHIFT,
    AST_OP_RSHIFT,

    AST_OP_LT, // <
    AST_OP_LE, // <=
    AST_OP_GT, // >
    AST_OP_GE, // >=
    AST_OP_EE, // ==
    AST_OP_NE, // !=

    AST_OP_AND_AND, // &&
    AST_OP_OR_OR, // ||
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
        [AST_OP_GE] = ">=", // >=
        [AST_OP_EE] = "==", // ==
        [AST_OP_NE] = "!=", // !=
        [AST_OP_OR_OR] = "||",
        [AST_OP_AND_AND] = "&&",

        [AST_OP_NOT] = "!", // unary !right
        [AST_OP_NEG] = "-", // unary -right
};

struct ast_stmt_t {
    int line;
    int column;
    bool error;

    ast_type_t assert_type; // 声明语句类型
    void *value;
};

typedef struct {
    int line;
    int column;
    ast_type_t assert_type; // 表达式断言
    type_t type; // 表达式自身的类型
    type_t target_type; // 表达式赋值的目标的 type
    void *value;
} ast_expr_t;

typedef struct {
} ast_continue_t;

typedef struct {
} ast_break_t;

typedef struct {
    char *literal;
} ast_ident;

typedef struct {
    type_t type;

    list_t *properties; // *struct_property_t
    ast_expr_t *default_expr; // scalar expr
} ast_new_expr_t;

/**
 * a as int
 */
typedef struct {
    type_t target_type;
    ast_expr_t src; // 将表达式转换成 target_type
} ast_as_expr_t;

/**
 * a is int v
 */
typedef struct {
    type_t target_type;
    ast_expr_t src;
    char *binding_ident; // 可选的绑定变量名，NULL 表示不绑定
} ast_is_expr_t;

typedef struct {
    type_t target_type;
    char *binding_ident; // 可选的绑定变量名，NULL 表示不绑定
} ast_match_is_expr_t;

typedef struct {
    type_t target_type;
} ast_macro_default_expr_t;

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
    type_t return_type; // call return type
    ast_expr_t left;

    list_t *generics_args; // type_t

    list_t *args; // *ast_expr
    bool spread;
} ast_call_t;

typedef struct {
    slice_t *args_copy_stmts; // ast_stmt_t
    ast_fndef_t *closure_fn;
    ast_fndef_t *closure_fn_void;
    ast_call_t *origin_call; // 未封闭之前的 call

    ast_expr_t *flag_expr;
    type_t return_type;
} ast_macro_async_t;

// 一元表达式
typedef struct {
    ast_expr_op_t op; // 取反，取绝对值, 解引用等,取指针，按位取反
    ast_expr_t operand; // 操作对象
} ast_unary_expr_t;

typedef struct {
    slice_t *body;
} ast_block_expr_t;

// 二元表达式
typedef struct {
    ast_expr_op_t op; // +/-/*// 等 二元表达式
    ast_expr_t right;
    ast_expr_t left;
} ast_binary_expr_t;

typedef enum {
    PARAM_SELF_NULL = 0,
    PARAM_SELF_T,
    PARAM_SELF_RAWPTR_T,
    PARAM_SELF_PTR_T,
} ast_param_self_kind_t;

// 值类型
typedef struct {
    type_kind kind;
    char *value;
    int64_t len; // string 时 len
} ast_literal_t; // 标量值

// (xx, xx, xx)
typedef struct {
    // var 中, ast_expr 的 type 是  ast_var_decl 和 ast_tuple_destr
    // assign 中 (a, b, (c.e, d[0])) = (1, 2) ast_expr 可能是所有 operand 类型，包括 ast_tuple_destr 自身
    list_t *elements; // ast_expr
} ast_tuple_destr_t;

// 仅仅包含了声明
// int a;
typedef struct {
    char *ident;
    type_t type; // type 已经决定了 size

    bool be_capture; // 被 coroutine closure 引用, 必须在堆中分配

    // 当该值不为 null 时，则需要在 linear 进行堆分配以及相关的改写。
    // 变量进行 heap 分配时，指向 heap 的 ident， 后续的使用都需要替换成该 ident, 默认为 null
    // 当前版本只进行了粗糙的实现，将被 closure child fn 引用的所有变量默认进行了 heap_ident 赋值，让其在堆中分配，从而避免可能发生的协程
    // 变量引用问题。
    char *heap_ident;
} ast_var_decl_t;

typedef struct {
    char *ident;
    type_t type;
    ast_expr_t *right;
    bool processing;
} ast_constdef_stmt_t;

typedef struct {
    ast_expr_t left; // a  或 foo.bar.car 或者 d[0] 或者 (xx, xx, xx)
    ast_expr_t right;
    ast_var_decl_t *var_decl; // ref global var decl
} ast_global_assign_stmt_t, ast_assign_stmt_t;

// 包含了声明与赋值，所以统称为定义
typedef struct {
    ast_var_decl_t var_decl; // 左值
    ast_expr_t *right; // 右值
} ast_vardef_stmt_t;

typedef struct {
    ast_expr_t expr;
} ast_expr_fake_stmt_t, ast_ret_stmt_t;

// 基于 tuple 解构语法的变量快速赋值
// var (a, b, (c, d)) = (1, 2, (3, 4))
// 通过 tuple destruct 快速定义变量
typedef struct {
    ast_tuple_destr_t *tuple_destr;
    ast_expr_t right;
} ast_var_tuple_def_stmt_t;

typedef struct {
    ast_expr_t condition;
    slice_t *consequent; // ast_stmt
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
    ast_expr_t expr; // must as expr
} ast_let_t;

typedef struct {
    ast_expr_t expr; // expr type must union type
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
 *  stmt
 *  stmt
 * } catch e {
 *    stmt
 *    stmt
 * }
 */
typedef struct {
    slice_t *try_body;
    ast_var_decl_t catch_err;
    slice_t *catch_body;
} ast_try_catch_stmt_t;

/**
 * match nice {
	test() -> 12
	int|string -> {
		int a = 1
		break a
	}
	_ -> 12
}
 */


/**
 * 条件表达式模式和常量两种模式, 其实只有一种模式，主要是看 case 的类型
 */
typedef struct {
    // cond 可以是常量，也可以是具体表达式(包含 match 模式下的特殊类型表达式。比如 n if xxx, is xxx)
    // 当多个常量使用 | 分隔时，进行分解生成 cond
    list_t *cond_list;

    bool is_default; //  使用了 ident 并且 ident 是 _, 在 analyzer 阶段会被解析为 default。default 必须在最后一栏

    slice_t *handle_body;
} ast_match_case_t;

typedef struct {
    ast_expr_t *subject;
    slice_t *cases; // ast_match_case*
} ast_match_t;


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
    ast_expr_t iterate; // list, foo.list, bar[0]
    ast_var_decl_t first; // 类型推导, type 可能是 int 或者 string
    ast_var_decl_t *second; // value 可选，可能为 null
    slice_t *body;
} ast_for_iterator_stmt_t;

typedef struct {
    ast_expr_t *expr;
} ast_return_stmt_t;

// Selective import item: {sqrt, pow, Pi as pi}
typedef struct {
    char *ident; // symbol name to import
    char *alias; // optional alias (NULL if not aliased)
} ast_import_select_item_t;

// import "module_path" module_name alias
typedef struct {
    // file or package one of the two
    char *file; // import 'xxx' or
    slice_t *ast_package; // a.b.c.d package 字符串数组
    char *as; // import "foo/bar" as xxx, import 别名，没有别名则使用 bar 作为名称

    // Selective import support: import math.{sqrt, pow, Pi as pi}
    bool is_selective; // true if using {item1, item2} syntax
    slice_t *select_items; // slice of ast_import_select_item_t*, NULL if not selective

    // 通过上面的 file 或者 package 解析出的完整 package 路径
    // full_path 对应的 module 会属于某一个 package, 需要记录一下对应的 package conf, 否则单凭一个 full_path 还不足以定位到
    // 对应的 package.toml
    uint8_t module_type;
    char *full_path;
    toml_table_t *package_conf;
    char *package_dir; // 这也是 import module 的 workdir
    bool use_links;

    char *module_ident; // 在符号表中的名称前缀,基于 full_path 计算出来当 unique ident, 如果是 main 则默认添加 main.n
} ast_import_t;

// Tracks a selective import reference for symbol resolution
typedef struct {
    char *module_ident; // The module the symbol comes from
    char *original_ident; // Original symbol name in that module
    ast_import_t *import; // Reference to parent import
} ast_import_select_t;

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
    char *ident; // ident 冗余

    // parser 阶段是 typedef ident
    // infer 完成后是 typeuse_struct
    type_t type;

    list_t *properties; // struct_property_t
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
    ast_expr_t left; // left is struct or package
    string key;
} ast_expr_select_t;

/**
select {
    ch.on_recv() -> msg {
        continue
    }
    ch.on_send(msg) -> {
        break
    }
    _ -> {
    }
}
*/
typedef struct {
    ast_call_t *on_call; // 可选
    ast_var_decl_t *recv_var; // 可选
    bool is_recv;
    bool is_default; // default 没有 on_call 和 recv_var
    slice_t *handle_body;
} ast_select_case_t;

typedef struct {
    slice_t *cases; // ast_select_case_t
    bool has_default;
    int16_t send_count;
    int16_t recv_count;
} ast_select_stmt_t;

typedef struct {
    ast_expr_t instance; // type is ptr<struct> or struct
    string key;
    struct_property_t *property; // 冗余方便计算
} ast_struct_select_t;

/**
 * 如何确定 left_type?
 * optimize 表达式阶段生成该值，不行也要行！
 */
typedef struct {
    type_t element_type; // 访问的 value 的类型
    ast_expr_t left;
    ast_expr_t index;
} ast_vec_access_t, ast_array_access_t;

typedef struct {
    type_t element_type; // index 对应的 value 的 type
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
    ast_expr_t left; // string/list/map/set/tuple
    ast_expr_t key;
} ast_access_t;

typedef struct {
    ast_expr_t left;
    ast_expr_t start;
    ast_expr_t end;
} ast_vec_slice_t;

// [1,a.b, call()]
typedef struct {
    list_t *elements; // ast_expr
} ast_array_new_t;

typedef struct {
    ast_expr_t default_element; // ast_expr
    ast_expr_t length_expr;
} ast_array_repeat_new_t, ast_vec_repeat_new_t;

typedef struct {
    list_t *elements; // ast_expr, nullable
} ast_vec_new_t;

typedef struct {
    ast_expr_t key;
    ast_expr_t value;
} ast_map_element_t;

// {key: value}
// var s = {1, 2, 3, call(), xxx}
typedef struct {
    list_t *elements; // ast_map_element or ast_expr
} ast_map_new_t, ast_set_new_t, ast_empty_curly_new_t;

// var s = (1, 2, 2.14, 1.15, true)
typedef struct {
    list_t *elements; // 值为 ast_expr
} ast_tuple_new_t;

typedef struct {
    uint8_t index;
    char *unique_ident;
} ast_env_access_t;

typedef struct {
    list_t *elements;
    bool any;
    bool and;
    bool or ;
} ast_generics_constraints;

typedef struct {
    char *ident; // generic param
    ast_generics_constraints constraints;
} ast_generics_param_t;

/**
 * type my_int = int
 * type my_string =  string
 * type my_my_string =  my_string
 * type my = struct {}
 */
typedef struct {
    string ident; // my_int (自定义的类型名称)
    list_t *params; // *ast_generics_param|null
    type_t type_expr; // int (类型)
    bool is_alias; // 是否仅作为别名
    bool is_interface; // 快速识别
    bool is_enum;
    list_t *impl_interfaces; // type_t, typedef 可以实现多个接口, 对于 interface 来说则是自身扩展
    struct sc_map_sv method_table; // key = ident, value = ast_fndef_t
    int64_t hash;
} ast_typedef_stmt_t;

// 这里包含 body, 所以属于 def
struct ast_fndef_t {
    // 可执行文件中的 label, symbol 中表现为 FN 类型，但是如果 fn 引用了外部的环境，则不能直接调用该 fn, 需要将 fn 关联的环境一起传递进来
    char *symbol_name;
    // 闭包处理中的的 var name, 可能为 null
    // 其通过 jit 封装了一份完整的执行环境，并将环境通过 last param 传递给 symbol name 对应的函数 body 部分
    char *jit_closure_name;
    type_t return_type;
    list_t *params; // ast_var_decl_t*
    bool rest_param;
    slice_t *body; // ast_stmt* 函数体
    void *closure; // closure 数据冗余

    // 记录当前 tpl fn 接受的泛型参数组合，避免重复生成 special fn
    table_t *generics_hash_table;

    // infer call 时具体为 generics_params 分配的 args, 不需要单独初始化
    table_t *generics_args_table;
    char *generics_args_hash; // 基于 gen args 计算的 hash 值，可以唯一标记当前函数

    // example:
    // fn list_first<T,U>()
    // fn vec<T>.first()
    list_t *generics_params; // ast_generic_param

    type_t impl_type;
    ast_param_self_kind_t self_kind;

    // ast_expr, 当前 fn body 中引用的外部的环境
    // 这是 parent 视角中的表达式，在 parent 中创建 child fn 时，如果发现 child fn 引用当前作用域中的变量
    // 则需要将当前作用域中的变量打包成 env 丢给 child fn
    list_t *capture_exprs;

    // local_ident_t* 当前函数中是否存在被 child 引用的变量
    slice_t *be_capture_locals;

    type_t type; // 类型冗余一份

    // 默认为 null, 当前函数为泛型 fn 时才会有值，local fn 同样有值且和 global fn 同值
    // key is generic->ident, value is *type_t
    table_t *generic_assign;

    // 默认为 null，如果是 local fn, 则指向定义的 global fn
    struct ast_fndef_t *global_parent;

    // 仅当前 fn 为 global fn 时才有可能存在一个或者多个 child_fndefs
    slice_t *local_children;

    // analyzer 时赋值
    bool is_local; // 是否是全局函数
    bool is_tpl; // 是否是 tpl 函数

    bool is_impl; // 是否是 impl fn

    bool is_errable;

    // tpl fn 可以自定义 #linkid 宏, 用来自定义链接符号名称
    char *linkid;

    // 当 self_kind == PARAM_SELF_T 时，interface 存储指针数据但方法期望值类型
    // receiver_wrapper 用于接收指针参数，解引用后调用原函数
    struct ast_fndef_t *receiver_wrapper;

    bool is_generics; // 是否是泛型

    bool is_async; // coroutine closure fn, default is false
    bool is_private;

    // catch err { 12 }
    ct_stack_t *ret_target_types;

    // dump error
    char *fn_name;
    char *fn_name_with_pkg;
    char *rel_path;
    int column;
    int line;
    module_t *module; // module 绑定
};

ast_ident *ast_new_ident(char *literal);

ast_fndef_t *ast_fndef_copy(module_t *m, ast_fndef_t *temp);

ast_expr_t *ast_expr_copy(module_t *m, ast_expr_t *temp);

static bool ast_is_arithmetic_op(ast_expr_op_t op) {
    return op == AST_OP_LSHIFT ||
           op == AST_OP_RSHIFT ||
           op == AST_OP_AND ||
           op == AST_OP_OR ||
           op == AST_OP_XOR ||
           op == AST_OP_REM ||
           op == AST_OP_ADD ||
           op == AST_OP_SUB ||
           op == AST_OP_MUL ||
           op == AST_OP_DIV;
}

static bool ast_is_logic_op(ast_expr_op_t op) {
    return op == AST_OP_OR_OR ||
           op == AST_OP_AND_AND ||
           op == AST_OP_LT ||
           op == AST_OP_LE ||
           op == AST_OP_GT ||
           op == AST_OP_GE ||
           op == AST_OP_EE ||
           op == AST_OP_NE;
}

static inline ast_generics_param_t *ast_generics_param_new(int line, int column, char *ident) {
    ast_generics_param_t *param = NEW(ast_generics_param_t);
    param->ident = ident;

    param->constraints.elements = ct_list_new(sizeof(type_t));
    param->constraints.any = true;
    param->constraints.and = false;
    param->constraints.or = false;
    return param;
}

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

static inline ast_expr_t *ast_load_addr(ast_expr_t *target) {
    ast_expr_t *result = NEW(ast_expr_t);

    ast_unary_expr_t *expr = NEW(ast_unary_expr_t);
    expr->operand = *target;
    expr->op = AST_OP_LA;

    result->line = target->line;
    result->column = target->column;
    result->assert_type = AST_EXPR_UNARY;
    result->value = expr;
    result->type = type_rawptrof(target->type);
    return result;
}

static inline ast_expr_t *ast_indirect_addr(ast_expr_t *target) {
    ast_expr_t *result = NEW(ast_expr_t);

    ast_unary_expr_t *expr = NEW(ast_unary_expr_t);
    expr->operand = *target;
    expr->op = AST_OP_IA;

    assert(target->type.kind == TYPE_RAWPTR || target->type.kind == TYPE_PTR);

    result->line = target->line;
    result->column = target->column;
    result->assert_type = AST_EXPR_UNARY;
    result->value = expr;
    result->type = target->type.ptr->value_type;
    return result;
}

static inline ast_expr_t *ast_safe_load_addr(ast_expr_t *target) {
    ast_expr_t *result = ast_load_addr(target);
    ast_unary_expr_t *expr = result->value;
    expr->op = AST_OP_SAFE_LA;
    result->type.kind = 0;
    result->type.status = 0;
    result->target_type.kind = 0;
    result->target_type.status = 0;
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

static inline bool is_bool_operand_operator(ast_expr_op_t op) {
    return op == AST_OP_AND_AND || op == AST_OP_OR_OR;
}

/**
 * 部分运算符只支持 int 类型数值运算，此时数值类型提升也拯救不了该运算
 * @return
 */
static inline bool is_integer_operator(ast_expr_op_t op) {
    return op == AST_OP_REM || op == AST_OP_LSHIFT || op == AST_OP_RSHIFT || op == AST_OP_AND || op == AST_OP_OR ||
           op == AST_OP_XOR ||
           op == AST_OP_BNOT;
}

static inline bool can_assign(ast_type_t t) {
    if (t == AST_EXPR_IDENT || t == AST_EXPR_ACCESS || t == AST_EXPR_SELECT || t == AST_EXPR_MAP_ACCESS ||
        t == AST_EXPR_VEC_ACCESS ||
        t == AST_EXPR_ENV_ACCESS || t == AST_EXPR_STRUCT_SELECT) {
        return true;
    }
    return false;
}

static inline ast_fndef_t *ast_fndef_new(module_t *m, int line, int column) {
    ast_fndef_t *fndef = NEW(ast_fndef_t);
    fndef->is_async = false;
    fndef->is_private = false;
    fndef->module = m;
    fndef->rel_path = m->rel_path;
    fndef->symbol_name = NULL;
    fndef->linkid = NULL;
    fndef->jit_closure_name = NULL;
    fndef->line = line;
    fndef->column = column;
    fndef->local_children = slice_new();
    fndef->ret_target_types = stack_new();
    fndef->generics_params = NULL;
    fndef->is_impl = false;
    fndef->capture_exprs = ct_list_new(sizeof(ast_expr_t));
    fndef->be_capture_locals = slice_new();

    return fndef;
}

#endif // NATURE_SRC_AST_H_
