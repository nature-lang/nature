#ifndef NATURE_SRC_AST_H_
#define NATURE_SRC_AST_H_

#include <stdlib.h>
#include "utils/assertf.h"
#include "utils/slice.h"
#include "utils/ct_list.h"
#include "utils/table.h"
#include "utils/type.h"
#include "package.h"

typedef enum {
    AST_EXPR_LITERAL = 1, // 常数值 => 预计将存储在 data 段中
    AST_EXPR_BINARY,
    AST_EXPR_UNARY,
    AST_EXPR_IDENT,
    AST_EXPR_AS,
    AST_EXPR_IS,

    AST_EXPR_MAP_ACCESS,
    AST_EXPR_LIST_ACCESS,
    AST_EXPR_TUPLE_ACCESS,

    AST_EXPR_STRUCT_SELECT,
    AST_EXPR_LIST_SELECT, // [1, 2, 3].push(3)
    AST_EXPR_MAP_SELECT, // [1, 2, 3].push(3)
    AST_EXPR_SET_SELECT, // [1, 2, 3].push(3)

    AST_EXPR_ENV_ACCESS,

    AST_EXPR_LIST_NEW, // [1, 2, 3]
    AST_EXPR_MAP_NEW, // {"a": 1, "b": 2}
    AST_EXPR_SET_NEW, // {1, 2, 3, 4}
    AST_EXPR_TUPLE_NEW, // (1, 1.1, true)
    AST_EXPR_TUPLE_DESTR, // (var_a, var_b, (var_c, var_d))
    AST_EXPR_STRUCT_NEW, // person {a = 1; b = 2}
    AST_EXPR_TRY,
    AST_EXPR_CATCH,
    AST_EXPR_BOOM,

    // 抽象复合类型
    AST_EXPR_ACCESS,
    AST_EXPR_SELECT,
    AST_VAR_DECL,

    // stmt
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_IMPORT,
    AST_STMT_VARDEF,
    AST_STMT_VAR_TUPLE_DESTR,
    AST_STMT_ASSIGN,
    AST_STMT_RETURN,
    AST_STMT_IF,
    AST_STMT_THROW,
    AST_STMT_LET,
    AST_STMT_FOR_ITERATOR,
    AST_STMT_FOR_COND,
    AST_STMT_FOR_TRADITION,
    AST_STMT_TYPE_ALIAS,
    AST_CALL,
    AST_FNDEF, // fn def (其包含 body)
    AST_STMT_ENV_CLOSURE, // closure def
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
    AST_OP_LA, // load addr &var
    AST_OP_IA, // indirect addr  *解引用


    // 位运算
    AST_OP_AND,
    AST_OP_OR,
    AST_OP_XOR,
    AST_OP_LSHIFT,
    AST_OP_RSHIFT,


    AST_OP_LT, // <
    AST_OP_LE, // <=
    AST_OP_GT, // >
    AST_OP_GE,  // >=
    AST_OP_EE, // ==
    AST_OP_NE, // !=

    AST_OP_AND_AND,
    AST_OP_OR_OR,

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
        [AST_OP_GE] = ">=",  // >=
        [AST_OP_EE] = "==", // ==
        [AST_OP_NE] = "!=", // !=

        [AST_OP_NOT] = "!", // unary !right
        [AST_OP_NEG] = "-", // unary -right
};

typedef struct {
    int line; // 行号
    ast_type_t assert_type; // 声明语句类型
    void *value;
} ast_stmt_t;

typedef struct {
} ast_continue_t;

typedef struct {
} ast_break_t;

typedef struct {
    int line;
    ast_type_t assert_type; // 表达式断言
    type_t type; // 表达式自身的类型
    type_t target_type; // 表达式赋值的目标的 type
    void *value;
} ast_expr_t;

typedef struct {
    size_t count; // 当前数量, unsigned int
    size_t capacity; // 容量
    ast_stmt_t *list;
} ast_block_stmt;

typedef struct {
    char *literal;
} ast_ident;

/**
 * a as int
 */
typedef struct {
    type_t target_type;
    ast_expr_t src_operand; // 将表达式转换成 target_type
} ast_as_expr_t;

/**
 * a is int, a must any or union
 */
typedef struct {
    type_t target_type;
    ast_expr_t src_operand; // 将表达式转换成 target_type
} ast_is_expr_t;

// 一元表达式
typedef struct {
    ast_expr_op_t operator; // 取反，取绝对值, 解引用等,取指针，按位取反
    ast_expr_t operand; // 操作对象
} ast_unary_expr_t;

// 二元表达式
typedef struct {
    ast_expr_op_t operator; // +/-/*// 等 二元表达式
    ast_expr_t right;
    ast_expr_t left;
} ast_binary_expr_t;

// 调用函数
typedef struct {
    type_t return_type; // call return type 冗余
    ast_expr_t left;
    list_t *actual_params;// *ast_expr
    bool catch; // 本次 call 是否被 catch
    bool spread;
} ast_call_t;

// 值类型
typedef struct {
    type_kind kind;
    char *value;
} ast_literal_t; // 标量值

// (xx, xx, xx)
typedef struct {
    // var 中, ast_expr 的 type 是  ast_var_decl 和 ast_tuple_destr
    // assign 中 (a, b, (c.e, d[0])) = (1, 2) ast_expr 可能是所有 operand 类型，包括 ast_tuple_destr 自身
    list_t *elements;  // ast_expr
} ast_tuple_destr_t;

typedef struct {
    ast_expr_t left; // a  或 foo.bar.car 或者 d[0] 或者 (xx, xx, xx)
    ast_expr_t right;
} ast_assign_stmt_t;

// 仅仅包含了声明
// int a;
typedef struct {
    char *ident;
    type_t type; // type 已经决定了 size
} ast_var_decl_t;

// 包含了声明与赋值，所以统称为定义
typedef struct {
    ast_var_decl_t var_decl; // 左值
    ast_expr_t right; // 右值
} ast_vardef_stmt_t;

// 基于 tuple 解构语法的变量快速赋值
// var (a, b, (c, d)) = (1, 2)
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
 * var (res, error) = try call()
 * var (res, error) = try foo[1]
 * var (res, error) = try bar.car.foo()
 * var (res, error) = try bar().car().foo()
 */
typedef struct {
    ast_expr_t expr;
} ast_try_t;

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

// import "module_path" module_name alias
typedef struct {
    // file or package one of the two
    char *file; // import 'xxx' or
    slice_t *package; // a.b.c.d package 字符串数组
    char *as; // import "foo/bar" as xxx, import 别名，没有别名则使用 bar 作为名称

    // 通过上面的 file 或者 package 解析出的完整 package 路径
    // full_path 对应的 module 会属于某一个 package, 需要记录一下对应的 package conf, 否则单凭一个 full_path 还不足以定位到
    // 对应的 package.toml
    char *full_path;
    toml_table_t *package_conf;
    char *package_dir; // 这也是 import module 的 workdir

    char *module_ident; // 在符号表中的名称前缀,基于 full_path 计算出来当 unique ident
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
    ast_expr_t left; // left is struct
    string key;
} ast_select_t;

typedef struct {
    ast_expr_t left;
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
} ast_list_access_t;

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
    ast_expr_t left;
    ast_expr_t key;
} ast_access_t;

// [1,a.b, call()]
typedef struct {
    list_t *elements; // ast_expr
//    type_t type; // list的类型 (类型推导截断冗余)
} ast_list_new_t;

typedef struct {
    ast_expr_t key;
    ast_expr_t value;
} ast_map_element_t;

// {key: value}
typedef struct {
    list_t *elements; // ast_map_element
} ast_map_new_t;

// var s = {1, 2, 3, call(), xxx}
typedef struct {
    list_t *elements; // 值为 ast_expr
} ast_set_new_t;

// var s = (1, 2, 2.14, 1.15, true)
typedef struct {
    list_t *elements; // 值为 ast_expr
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
    string ident; // my_int (自定义的类型名称)
    list_t *formals; // ast_ident*|null
    type_t type; // int (类型)
} ast_type_alias_stmt_t;

// 这里包含 body, 所以属于 def
typedef struct ast_fndef_t {
    // 可执行文件中的 label, symbol 中表现为 FN 类型，但是如果 fn 引用了外部的环境，则不能直接调用该 fn, 需要将 fn 关联的环境一起传递进来
    char *symbol_name;
    // 闭包处理中的的 var name, 可能为 null
    // 其通过 jit 封装了一份完整的执行环境，并将环境通过 last param 传递给 symbol name 对应的函数 body 部分
    char *closure_name;
    type_t return_type;
    list_t *formals; // ast_var_decl_t*
    bool rest_param;
    slice_t *body; // ast_stmt* 函数体
    void *closure; // closure 数据冗余

    // ast_expr, 当前 fn body 中引用的外部的环境
    // 这是 parent 视角中的表达式，在 parent 中创建 child fn 时，如果发现 child fn 引用当前作用域中的变量
    // 则需要将当前作用域中的变量打包成 env 丢给 child fn
    list_t *capture_exprs;
    // local_ident_t* 当前函数中是否存在被 child 引用的变量
    slice_t *be_capture_locals;

    /**
     * 由于 global 函数能够进行重载，以及泛型，所以在一个模块下可能会存在多个同名的 global 函数
     * 虽然经过 analyzer 会将 local fn ident 添加唯一标识，但是在 generic 模式下所有的生成函数中的 local fn 依旧是同名的函数
     * 这里的同名主要体现在 symbol table 中。
     *
     * params_hash 是基于 global fn params 计算出的唯一标识，所有的 global fn 的唯一标识都将 with params_hash，然后将 with 后的
     * 唯一标识添加到 symbol table 中，该唯一标识将会影响最终生成的 elf 文件的 label 的名称。
     */
    char *params_hash;

    // analyzer stage, 当 fn 定义在 struct 中,用于记录 struct type
    type_t *self_struct;
    type_t type; // 类型冗余一份

    // 泛型解析时临时使用
    slice_t *generic_params; // ast_typedef_stmt
    table_t *exists_generic_params;  // 避免 generic_types 重复写入

    // 默认为 null, 当前函数为泛型 fn 时才会有值，local fn 同样有值且和 global fn 同值
    // key is generic->ident, value is *type_t
    table_t *generic_assign;
    // 默认为 null，如果是 local fn, 则指向定义的 global fn
    struct ast_fndef_t *global_parent;
    // 仅当前 fn 为 global fn 时才有可能存在 child_fndefs
    slice_t *local_children;
    // analyzer 时赋值
    bool is_local; // 是否是全局函数
} ast_fndef_t; // 既可以是 expression,也可以是 stmt

type_t *select_formal_param(type_fn_t *type_fn, uint8_t index);

//bool type_compare(type_t left, type_t right);

ast_ident *ast_new_ident(char *literal);

ast_fndef_t *ast_fndef_copy(ast_fndef_t *temp);

type_t type_copy(type_t temp);

static slice_t *ast_body_copy(slice_t *body);

static ast_stmt_t *ast_stmt_copy(ast_stmt_t *temp);

static ast_expr_t *ast_expr_copy(ast_expr_t *temp);

static ast_call_t *ast_call_copy(ast_call_t *temp);

static inline ast_expr_t *ast_ident_expr(char *literal) {
    ast_expr_t *expr = NEW(ast_expr_t);
    expr->assert_type = AST_EXPR_IDENT;
    expr->value = ast_new_ident(literal);
    return expr;
}

static inline ast_expr_t *ast_int_expr(uint64_t number) {
    ast_expr_t *expr = NEW(ast_expr_t);
    expr->assert_type = AST_EXPR_LITERAL;
    ast_literal_t *literal = NEW(ast_literal_t);
    literal->kind = TYPE_INT;
    literal->value = itoa(number);
    expr->value = literal;
    return expr;
}

static inline ast_expr_t *ast_unary(ast_expr_t *target, ast_expr_op_t unary_op) {
    ast_expr_t *result = NEW(ast_expr_t);

    ast_unary_expr_t *expr = NEW(ast_unary_expr_t);
    expr->operand = *target;
    expr->operator = unary_op;

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
    convert->src_operand = expr;
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
    return op == AST_OP_REM ||
           op == AST_OP_LSHIFT ||
           op == AST_OP_RSHIFT ||
           op == AST_OP_AND ||
           op == AST_OP_OR ||
           op == AST_OP_XOR ||
           op == AST_OP_BNOT;
}

static inline bool can_assign(ast_type_t t) {
    if (t == AST_EXPR_IDENT ||
        t == AST_EXPR_ACCESS ||
        t == AST_EXPR_SELECT ||
        t == AST_EXPR_MAP_ACCESS ||
        t == AST_EXPR_LIST_ACCESS ||
        t == AST_EXPR_ENV_ACCESS ||
        t == AST_EXPR_STRUCT_SELECT) {
        return true;
    }
    return false;
}

static inline ast_fndef_t *ast_fndef_new() {
    ast_fndef_t *fndef = NEW(ast_fndef_t);
    fndef->symbol_name = NULL;
    fndef->closure_name = NULL;
    fndef->local_children = slice_new();
    return fndef;
}

#endif //NATURE_SRC_AST_H_
