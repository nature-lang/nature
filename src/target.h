#ifndef NATURE_TARGET_H
#define NATURE_TARGET_H

#include "src/syntax/scanner.h"
#include "src/syntax/parser.h"
#include "import.h"
#include "utils/list.h"
#include "value.h"
#include "symbol.h"


typedef struct {
    symbol_type type;
    void *decl; // ast_var_decl,ast_type_decl_stmt,ast_new_fn
    string ident; // 原始名称
    string unique_ident; // 唯一名称
    int scope_depth;
    bool is_capture; // 是否被捕获(是否被下级引用)
} analysis_local_ident;

/**
 * free_var 是在 parent function 作用域中被使用,但是被捕获存放在了 current function free_vars 中,
 * 所以这里的 is_local 指的是在 parent 中的位置
 * 如果 is_local 为 true 则 index 为 parent.locals[index]
 * 如果 is_local 为 false 则 index 为参数 env[index]
 */
typedef struct {
    bool is_local;
    uint8_t index;
    string ident;
} analysis_free_ident;

typedef struct analysis_local_scope {
    struct analysis_local_scope *parent;
    analysis_local_ident *idents[UINT8_MAX];
    uint8_t count;
    uint8_t scope_depth;
} analysis_local_scope;

/**
 * 词法作用域
 */
typedef struct analysis_function {
    struct analysis_function *parent;

    analysis_local_scope *current_scope;

//  analysis_local_ident *locals[UINT8_MAX];
//  uint8_t local_count;

    // wwh: 使用了当前作用域之外的变量
    analysis_free_ident frees[UINT8_MAX];
    uint8_t free_count;

    // 当前函数内的块作用域深度(基于当前函数,所以初始值为 0, 用于块作用域判定)
    uint8_t scope_depth;

    // 便于值改写, 放心 env unique name 会注册到字符表的要用
    string env_unique_name;

    // 函数定义在当前作用域仅加载 function name
    // 函数体的解析则延迟到当前作用域内的所有标识符都定义明确好
    struct {
        // 由于需要延迟处理，所以缓存函数定义时的 scope，在处理时进行还原。
        analysis_local_scope *scope;
        union {
            ast_stmt *stmt;
            ast_expr *expr;
        };
        bool is_stmt;
    } contains_fn_decl[UINT8_MAX];
    uint8_t contains_fn_count;
} analysis_function;

// 记录遍历过的 import(根据 full path 判断)

/**
 * Target district
 */
typedef struct {
    char *source; // 源文件
    string namespace; // 通常配置为完整文件路径
    string package; // 为文件名称

    scanner_cursor s_cursor;
    scanner_error s_error;
    list *token_list; // scanner 结果

    parser_cursor p_cursor;
    ast_block_stmt stmt_list;

    // analysis
    analysis_function *analysis_current;

    // TODO var_decls to init closures? 这样就只需要 compiler closures 就行了
    // 分析阶段(包括 closure 构建,全局符号表构建), 根据是否为 main 生成 import/symbol/var_decls(symbol)/closure_decls
    list *imports; // import_t, 图遍历 imports

    // 对外全局符号 -> 三种类型 var/fn/type_decl
    list *symbols; // symbol_t, 这里只存储全局符号

    list *ast_closure_decls; // 全局的或者非全局的都在这里了
} target_t;

#endif //NATURE_TARGET_H
