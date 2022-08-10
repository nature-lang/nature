#ifndef NATURE_MODULE_H
#define NATURE_MODULE_H

#include "utils/list.h"
#include "value.h"
#include "symbol.h"
#include "src/lir/lir.h"

#define MODULE_SUFFIX ".n"

typedef struct {
    char *source;
    char *current;
    char *guard;
    int length;
    int line; // 当前所在代码行，用于代码报错提示

    bool has_newline;
    char space_prev;
    char space_next;
} scanner_cursor;

typedef struct {
    bool has;
    char *message;
} scanner_error;


typedef struct {
    list_node *current;
} parser_cursor;

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

    // 便于值改写, 放心 env unique as 会注册到字符表的要用
    string env_unique_name;

    // 函数定义在当前作用域仅加载 function as
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

/**
 * path 基于 import 编译， import 能提供完整的 full_path 以及 module_name
 * Target district
 */
typedef struct {
    char *source; // 文件内容
    char *source_path; // 文件完整路径(外面丢进来的)
    char *source_dir; // 文件所在目录,去掉 xxx.n
//    string namespace; // is dir, 从 base_ns 算起的 source_dir
    string module_unique_name; // 符号表中都使用这个前缀 /code/nature/foo/bar.n => unique_name: nature/foo/bar

    bool entry; // 入口

    scanner_cursor s_cursor;
    scanner_error s_error;
    list *token_list; // scanner 结果

    parser_cursor p_cursor;
    slice_t *stmt_list;

    // analysis
    analysis_function *analysis_current;
    int analysis_line;

    // call init stmt
    ast_stmt *call_init_stmt;  // analysis 阶段写入

    // TODO var_decls to init closures? 这样就只需要 compiler closures 就行了
    // 分析阶段(包括 closure 构建,全局符号表构建), 根据是否为 main 生成 import/symbol/var_decls(symbol)/closure_decls
    slice_t *imports; // import_t, 图遍历 imports
    table *import_table; // 使用处做符号改写使用

    // 对外全局符号 -> 三种类型 var/fn/type_decl
    slice_t *symbols; // symbol_t, 这里只存储全局符号

    // infer 阶段得到
    slice_t *ast_closures; // 全局的或者非全局的都在这里了

    // compiler 阶段得到
    slice_t *compiler_closures; // 包含 lir

    // lower -> asm_insts
    list *asm_insts; // 和架构相关
    list *var_decl_list; // 和架构无关

    // elf target.o
    uint64_t elf_count;
    uint8_t *elf_binary;
    string linker_file_name;
} module_t;

// module_path + path + ident
char *ident_with_module_unique_name(string unique_name, char *ident);

/**
 * @param importer_dir importer 所在的目录, 用来计算相对路径引入
 * @param import
 * @return
 */
void complete_import(string importer_dir, ast_import *import);

char *parser_base_ns(char *dir);

module_t *module_front_build(string source_path, bool entry);

/**
 * 从 base_ns 开始，去掉结尾的 .n 部分
 * @param full_path
 * @return
 */
char *module_unique_name(string full_path);

#endif //NATURE_MODULE_H
